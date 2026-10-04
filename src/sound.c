// PICO-8 tracker data player for the Game Boy APU.
//
// The original cart's 64 sfx (32 notes each) and music patterns are played
// directly. PICO-8 has 4 equal channels; the GB has 2 pulse, 1 wave and 1
// noise channel, so each PICO-8 voice is routed per note:
//   music ch0 -> wave, ch2 -> pulse1, ch1/ch3 -> pulse2 (ch3 wins),
//   sfx voices -> pulse1/pulse2 (preempting music), noise notes -> noise.
#pragma bank 255
#include <gb/gb.h>
#include <gb/hardware.h>
#include "gen/assets.h"
#include "sound.h"
#include "gen/audio_data.h"

BANKREF(sound_bank)

#define NV 6          // 0-3 music, 4-5 sfx
#define TICK 127      // 1/256 frames per PICO-8 tick (183 samples @22050Hz)

typedef struct {
    const uint8_t *s;   // sfx data (68 bytes)
    uint8_t on;
    uint8_t idx;        // current note index
    uint8_t played;     // notes started
    uint8_t last;       // last audible note index (sfx voices stop after it)
    uint16_t acc, dur;  // timing in 1/256 frames
    uint8_t pitch, wave, vol, fx, cust;
    uint8_t ppitch, pvol;
    uint8_t t, nd;      // frames into note, note length in frames
    uint8_t et;         // instrument envelope time (sustains across equal notes)
    uint8_t newnote;
    // per-note precomputed rendering state (so per-frame work is cheap)
    uint8_t mode;       // 0 static, 1 pitch ramp (slide/drop), 2 vibrato, 3 arpeggio
    uint8_t arpsh;      // arpeggio speed shift
    uint8_t w;          // output waveform after instrument mapping
    uint8_t env;        // custom instrument envelope / arp id
    int16_t pbase;      // pitch*16 incl. instrument offset
    int32_t pacc, pstep;
    uint16_t vacc;      // volume 8.8
    int16_t vstep;
    int16_t arp[4];
    // output for this frame
    int16_t op;         // pitch * 16
    uint8_t ov;         // volume 0..15
    uint8_t ow;         // waveform id 0..7, 6 = noise
} voice_t;

static voice_t V[NV];
static int8_t pat = -1;
static uint8_t lvlmask = 0x0F;
static uint8_t leader;
static uint8_t chown[4] = {255, 255, 255, 255};
static uint8_t chvol[4], chwave[4];
static uint16_t chfreq[4];
static int16_t chop[4] = {-1, -1, -1, -1};
static uint8_t wave_loaded = 255;
static uint8_t sfxnext;

static const uint8_t waves[7][16] = {
    {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10}, // tri
    {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0xFD,0xB9,0x75,0x31,0x00,0x00,0x00,0x00}, // tilted saw
    {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}, // saw
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // square
    {0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // pulse
    {0x8B,0xDE,0xEE,0xDB,0x86,0x43,0x34,0x68,0x9A,0xAA,0x97,0x54,0x33,0x46,0x8A,0xBC}, // organ
    {0x02,0x46,0x8A,0xCE,0xFF,0xEC,0xA8,0x64,0x8A,0xCE,0xFF,0xEC,0xA8,0x64,0x20,0x00}, // phaser
};
static const uint8_t duty_of[8] = {2, 1, 1, 2, 1, 1, 2, 0};
static const int8_t arp_c2[4] = {-5, 0, 3, 0};
static const int8_t arp_c7[16] = {-8, -5, 0, 4, 7, 12, 14, 16, 19, 16, 14, 12, 7, 4, 2, 0};

static void voice_start(uint8_t v, uint8_t n) {
    voice_t *p = &V[v];
    uint8_t i;
    p->s = p8_sfx + (uint16_t)n * 68;
    p->on = 1;
    p->idx = 255;
    p->played = 0;
    p->dur = (uint16_t)(p->s[65] ? p->s[65] : 1) * TICK;
    p->acc = p->dur;  // start first note immediately
    p->nd = (uint8_t)(p->dur >> 8);
    if (!p->nd) p->nd = 1;
    p->last = 0;
    for (i = 0; i < 32; i++)
        if (p->s[i * 2 + 1] & 0x0E) p->last = i;
    p->vol = 0;
    p->pitch = 0;
    p->et = 0;
}

static void start_pattern(int8_t n) {
    uint8_t c, b, lead = 255, first = 255;
    pat = n;
    for (c = 0; c < 4; c++) {
        b = p8_music[n * 4 + c];
        V[c].on = 0;
        if (b & 0x40) continue;
        if (n >= 1 && n <= 8 && !(lvlmask & (1 << c))) continue;
        voice_start(c, b & 0x3F);
        if (first == 255) first = c;
        if (lead == 255 && !(V[c].s[66] < V[c].s[67])) lead = c;
    }
    if (lead == 255) lead = first;
    leader = lead;
    if (lead == 255) pat = -1;
}

static void next_pattern(void) {
    int8_t n = pat;
    if (p8_music[n * 4 + 1] & 0x80) {
        while (n > 0 && !(p8_music[n * 4] & 0x80)) n--;
    } else if (p8_music[n * 4 + 2] & 0x80) {
        n = -1;
    } else {
        n++;
        if (n >= 64) n = -1;
    }
    if (n < 0) {
        pat = -1;
        V[0].on = V[1].on = V[2].on = V[3].on = 0;
        return;
    }
    start_pattern(n);
}

void music(int8_t n) BANKED {
    __critical {
        if (n < 0) {
            pat = -1;
            V[0].on = V[1].on = V[2].on = V[3].on = 0;
        } else {
            start_pattern(n);
        }
    }
}

void musiclvl(uint8_t lvl) BANKED {
    uint8_t c;
    __critical {
        lvlmask = 1;
        for (c = 1; c <= 3; c++)
            if (c < lvl + 1) lvlmask |= 1 << c;
        // apply to running patterns 1..8 immediately
        if (pat >= 1 && pat <= 8) {
            for (c = 1; c < 4; c++) {
                uint8_t b = p8_music[pat * 4 + c];
                if (!(lvlmask & (1 << c))) V[c].on = 0;
                else if (!V[c].on && !(b & 0x40)) {
                    // join in sync with the leader
                    voice_start(c, b & 0x3F);
                    V[c].idx = V[leader].idx;
                    V[c].played = V[leader].played;
                    V[c].acc = V[leader].acc;
                }
            }
        }
    }
}

void sfx(uint8_t n) BANKED {
    uint8_t v;
    __critical {
        if (!V[4].on) v = 4;
        else if (!V[5].on) v = 5;
        else { v = 4 + sfxnext; sfxnext ^= 1; }
        voice_start(v, n);
    }
}

void sfx_stop(void) BANKED {
    V[4].on = V[5].on = 0;
}

static const int8_t cust_off[8] = {-12, 24, 0, 0, 0, 0, 24, 0};
static const uint8_t cust_w[8] = {1, 6, 5, 3, 0, 5, 2, 3};
static const uint8_t c6_env[12] = {16, 15, 13, 12, 11, 9, 8, 7, 5, 4, 3, 1};

static void read_note(voice_t *p) {
    const uint8_t *d = p->s + p->idx * 2;
    uint8_t lo = d[0], hi = d[1];
    uint8_t opitch = p->pitch, owave = p->wave, ocust = p->cust, ovol = p->vol;
    uint8_t nd = p->nd, v15, pv15;
    int16_t off;
    p->ppitch = opitch;
    p->pvol = ovol;
    p->pitch = lo & 63;
    p->wave = ((lo >> 6) | (hi << 2)) & 7;
    p->vol = (hi >> 1) & 7;
    p->fx = (hi >> 4) & 7;
    p->cust = hi >> 7;
    p->t = 0;
    p->newnote = 1;
    if (!(p->vol && ovol && p->pitch == opitch && p->wave == owave && p->cust == ocust)) p->et = 0;

    off = 0;
    p->w = p->wave;
    p->env = 255;
    if (p->cust) {
        off = (int16_t)cust_off[p->wave] << 4;
        p->w = cust_w[p->wave];
        p->env = p->wave;
    }
    p->pbase = ((int16_t)p->pitch << 4) + off;
    v15 = p->vol ? p->vol * 2 + 1 : 0;
    p->vacc = (uint16_t)v15 << 8;
    p->vstep = 0;
    p->mode = 0;
    switch (p->fx) {
    case 1:
        if (ovol) {
            int16_t pp = ((int16_t)opitch << 4) + off;
            pv15 = ovol * 2 + 1;
            p->mode = 1;
            p->pacc = (int32_t)pp << 8;
            p->pstep = ((int32_t)(p->pbase - pp) << 8) / nd;
            p->vacc = (uint16_t)pv15 << 8;
            p->vstep = (int16_t)(((int16_t)v15 - pv15) << 8) / nd;
        }
        break;
    case 2:
        p->mode = 2;
        break;
    case 3:
        p->mode = 1;
        p->pacc = (int32_t)p->pbase << 8;
        p->pstep = -((int32_t)p->pbase << 8) / nd;
        break;
    case 4:
        p->vacc = 0;
        p->vstep = (int16_t)((uint16_t)v15 << 8) / nd;
        break;
    case 5:
        p->vstep = -(int16_t)(((uint16_t)v15 << 8) / nd);
        break;
    default: {  // 6, 7: arpeggio over this group of 4 notes
        uint8_t g = p->idx & ~3, k;
        if (p->fx < 6) break;
        p->mode = 3;
        p->arpsh = p->fx == 6 ? 1 : 2;
        for (k = 0; k < 4; k++) p->arp[k] = ((int16_t)(p->s[(g + k) * 2] & 63) << 4) + off;
    }
    }
}

static void advance(uint8_t vi) {
    voice_t *p = &V[vi];
    p->acc += 256;
    while (p->acc >= p->dur) {
        p->acc -= p->dur;
        p->idx++;
        p->played++;
        if (vi < 4 && vi == leader && p->played > 32) {
            next_pattern();
            return;
        }
        {
            uint8_t ls = p->s[66], le = p->s[67];
            if (ls < le && p->idx >= le) p->idx = ls;
        }
        if (p->idx >= 32) {
            if (vi >= 4) { p->on = 0; return; }
            p->idx = 31;  // hold silence until the leader ends the pattern
            p->vol = 0;
            p->vacc = 0;
            p->vstep = 0;
            continue;
        }
        if (vi >= 4 && p->idx > p->last && !(p->s[66] < p->s[67])) { p->on = 0; return; }
        read_note(p);
    }
}

static const int8_t vib[8] = {-4, -2, 0, 2, 4, 2, 0, -2};

// compute op/ov/ow for this frame (additions only; divisions happen per note)
static void render(voice_t *p) {
    int16_t pitch;
    uint8_t vol, et;
    switch (p->mode) {
    case 1:
        p->pacc += p->pstep;
        pitch = (int16_t)(p->pacc >> 8);
        break;
    case 2:
        pitch = p->pbase + vib[p->t & 7];
        break;
    case 3:
        pitch = p->arp[(p->t >> p->arpsh) & 3];
        break;
    default:
        pitch = p->pbase;
    }
    vol = p->vacc >> 8;
    if (p->vstep) {
        int16_t nv = (int16_t)p->vacc + p->vstep;
        if (nv < 0) nv = 0;
        if (nv > (15 << 8)) nv = 15 << 8;
        p->vacc = (uint16_t)nv;
    }
    et = p->et;
    switch (p->env) {
    case 1: vol = et < 8 ? (uint8_t)(vol * (uint8_t)(16 - et * 2)) >> 4 : 0; break;
    case 2: pitch += arp_c2[(et >> 1) & 3] << 4; break;
    case 3: vol = et < 24 ? (uint8_t)(vol * (uint8_t)((32 - et) >> 1)) >> 4 : vol >> 2; break;
    case 4: if (et & 4) vol = vol >> 1; break;
    case 5: if (et < 30) vol = (uint8_t)(vol * (uint8_t)((et + 2) >> 1)) >> 4; break;
    case 6: vol = et < 12 ? (uint8_t)(vol * c6_env[et]) >> 4 : 0; break;
    case 7: pitch += arp_c7[(et >> 2) & 15] << 4; break;
    }
    if (pitch < 0) pitch = 0;
    p->op = pitch;
    p->ov = vol > 15 ? 15 : vol;
    p->ow = p->w;
}

static uint16_t freq_of(int16_t p16, uint8_t up12) {
    uint8_t i = (uint8_t)(p16 >> 4) + 12 + up12;
    uint8_t f = p16 & 15;
    uint16_t a, b;
    if (i >= 107) return pitch_tab[107];
    a = pitch_tab[i];
    b = pitch_tab[i + 1];
    return a + (uint16_t)(((uint16_t)(b - a) * f) >> 4);
}

static void ch_out(uint8_t ch, voice_t *p, uint8_t trig) {
    uint8_t vol = p->ov, w = p->ow;
    uint16_t fr;
    if (ch == 3) {
        uint8_t nr = noise_tab[(p->op >> 4) & 63];
        if (trig || vol != chvol[3] || nr != chfreq[3]) {
            NR42_REG = vol << 4;
            NR43_REG = nr;
            NR44_REG = 0x80;
            chvol[3] = vol;
            chfreq[3] = nr;
        }
        return;
    }
    if (ch == 2) {
        uint8_t code = vol == 0 ? 0 : vol < 5 ? 3 : vol < 10 ? 2 : 1;
        if (p->op == chop[2] && !trig) fr = chfreq[2];
        else { fr = freq_of(p->op, 12); chop[2] = p->op; }
        if (w != wave_loaded) {
            uint8_t i;
            const uint8_t *src = waves[w == 6 ? 2 : (w > 6 ? 6 : w)];
            NR30_REG = 0;
            for (i = 0; i < 16; i++) (&AUD3WAVE[0])[i] = src[i];
            wave_loaded = w;
            trig = 1;
        }
        if (trig || code != chvol[2]) {
            NR30_REG = 0x80;
            NR32_REG = code << 5;
            NR33_REG = fr & 255;
            NR34_REG = 0x80 | (fr >> 8);
            chvol[2] = code;
        } else if (fr != chfreq[2]) {
            NR33_REG = fr & 255;
            NR34_REG = fr >> 8;
        }
        chfreq[2] = fr;
        return;
    }
    if (p->op == chop[ch] && !trig) fr = chfreq[ch];
    else { fr = freq_of(p->op, 0); chop[ch] = p->op; }
    if (trig || vol != chvol[ch] || w != chwave[ch]) {
        if (ch == 0) {
            NR10_REG = 0;
            NR11_REG = duty_of[w & 7] << 6;
            NR12_REG = vol << 4;
            NR13_REG = fr & 255;
            NR14_REG = 0x80 | (fr >> 8);
        } else {
            NR21_REG = duty_of[w & 7] << 6;
            NR22_REG = vol << 4;
            NR23_REG = fr & 255;
            NR24_REG = 0x80 | (fr >> 8);
        }
        chvol[ch] = vol;
        chwave[ch] = w;
    } else if (fr != chfreq[ch]) {
        if (ch == 0) { NR13_REG = fr & 255; NR14_REG = fr >> 8; }
        else { NR23_REG = fr & 255; NR24_REG = fr >> 8; }
    }
    chfreq[ch] = fr;
}

static void ch_silence(uint8_t ch) {
    if (chown[ch] == 255) return;
    chown[ch] = 255;
    switch (ch) {
    case 0: NR12_REG = 0; NR14_REG = 0x80; break;
    case 1: NR22_REG = 0; NR24_REG = 0x80; break;
    case 2: NR30_REG = 0; break;
    case 3: NR42_REG = 0; NR44_REG = 0x80; break;
    }
    chvol[ch] = 0;
}

#define SOUNDING(v) (V[v].on && V[v].ov)
#define TONAL(v) (SOUNDING(v) && V[v].ow != 6)
#define NOISY(v) (SOUNDING(v) && V[v].ow == 6)

static void assign(uint8_t ch, uint8_t v) {
    if (v == 255) { ch_silence(ch); return; }
    {
        uint8_t trig = V[v].newnote || chown[ch] != v;
        chown[ch] = v;
        ch_out(ch, &V[v], trig);
    }
}

void snd_update(void) {
    uint8_t v, o;
    for (v = 0; v < NV; v++) {
        if (!V[v].on) continue;
        V[v].newnote = 0;
        advance(v);
        if (!V[v].on) continue;
        render(&V[v]);
        V[v].t++;
        if (V[v].et < 255) V[v].et++;
    }
    // pulse1: sfx4 > music2
    o = TONAL(4) ? 4 : TONAL(2) ? 2 : 255;
    assign(0, o);
    // pulse2: sfx5 > music3 > music1
    o = TONAL(5) ? 5 : TONAL(3) ? 3 : TONAL(1) ? 1 : 255;
    assign(1, o);
    // wave: music0 (or an sfx when both pulses are taken by sfx)
    o = TONAL(0) ? 0 : 255;
    assign(2, o);
    // noise
    o = NOISY(4) ? 4 : NOISY(5) ? 5 : NOISY(1) ? 1 : NOISY(3) ? 3 : NOISY(2) ? 2 : NOISY(0) ? 0 : 255;
    assign(3, o);
}

void snd_init(void) BANKED {
    uint8_t i;
    NR52_REG = 0x80;
    NR50_REG = 0x77;
    NR51_REG = 0xFF;
    for (i = 0; i < NV; i++) V[i].on = 0;
    for (i = 0; i < 4; i++) chown[i] = 0, ch_silence(i);
}
