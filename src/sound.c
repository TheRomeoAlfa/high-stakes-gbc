// PICO-8 tracker data player for the Game Boy APU.
//
// The original cart's 64 sfx (32 notes each) and music patterns are played
// directly. PICO-8 has 4 equal channels; the GB has 2 pulse, 1 wave and 1
// noise channel, so each PICO-8 voice is routed per note:
//   music ch0 -> wave, ch2 -> pulse1, ch1/ch3 -> pulse2 (ch3 wins),
//   sfx voices -> pulse1/pulse2 (preempting music), noise notes -> noise.
//
// Faithfulness notes:
// - Custom instruments (note flag "c", instruments 0-7 = sfx 0-7) are played
//   as real sub-sfx: their notes run at the instrument's own speed, transposed
//   by (note pitch - C2), volume = note volume * instrument volume / 7.
// - Volume changes (fades, slides, instrument envelopes) use the GB hardware
//   envelope. Pulse/noise channels are only retriggered at note starts or when
//   the hardware level drifts from the wanted level, so fades don't buzz.
//   The wave channel's level is updated live (NR32) without retriggering.
#pragma bank 255
#include <gb/gb.h>
#include <gb/hardware.h>
#include "gen/assets.h"
#include "sound.h"
#include "gen/audio_data.h"

BANKREF(sound_bank)

#define NV 6          // 0-3 music, 4-5 sfx
#define TICK 127      // 1/256 frames per PICO-8 tick (183 samples @22050Hz)

// one playing note (used for the sfx note and for a custom instrument's note)
typedef struct {
    uint8_t pitch, wave, vol, fx;
    uint8_t mode;       // 0 static, 1 pitch ramp (slide/drop), 2 vibrato, 3 arpeggio
    uint8_t arpsh;
    uint8_t t;          // frames into the note
    int16_t pbase;      // pitch * 16
    int16_t pacc, pstep;   // pitch ramp, 12.4 of pitch*16
    uint16_t vacc;      // volume 0..7 in 8.8
    int16_t vstep;
    int16_t arp[4];
} note_t;

// sequencer state for one sfx stream (the voice's sfx or its instrument)
typedef struct {
    const uint8_t *s;
    uint8_t idx, played, nd;
    uint16_t acc, dur;
    uint8_t done;
} seq_t;

typedef struct {
    uint8_t on;
    uint8_t last;       // last audible note index (sfx voices stop after it)
    uint8_t cust;       // current note uses a custom instrument
    uint8_t newnote;    // retrigger request this frame
    seq_t sq;           // the sfx itself
    note_t n;           // its current note
    seq_t isq;          // custom instrument sub-sfx
    note_t in;          // current instrument note
    // output for this frame
    int16_t op;         // pitch * 16
    uint8_t ov;         // volume * 16 (0..240)
    int8_t odv;         // expected volume change per frame (same units)
    uint8_t ow;         // waveform 0..7 (6 = noise)
} voice_t;

static voice_t V[NV];
static int8_t pat = -1;
static uint8_t lvlmask = 0x0F;
static uint8_t leader;
static uint8_t sfxnext;

// per-GB-channel state
static uint8_t chown[4] = {255, 255, 255, 255};
static uint8_t chwave[4];
static uint16_t chfreq[4];
static int16_t chop[4] = {-1, -1, -1, -1};
static uint8_t hwvol[4];        // simulated hardware level * 16
static int8_t hwstep[4];        // simulated envelope change per frame
static uint8_t wave_loaded = 255, wave_level = 255;

static const uint8_t waves[7][16] = {
    {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10}, // tri
    {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0xFD,0xB9,0x75,0x31,0x00,0x00,0x00,0x00}, // tilted saw
    {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}, // saw
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // square
    {0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // pulse
    {0x8B,0xDE,0xEE,0xDB,0x86,0x43,0x34,0x68,0x9A,0xAA,0x97,0x54,0x33,0x46,0x8A,0xBC}, // organ
    {0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xFF,0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x11}, // phaser (soft)
};
// pulse duty per PICO-8 waveform: 0=12.5% 1=25% 2=50% (50% has the fewest harmonics)
static const uint8_t duty_of[8] = {2, 2, 1, 2, 1, 2, 2, 1};
// mix balance per GB channel (x/16): PICO-8's mix is bass heavy, GB pulses/noise are bright
#ifndef GAIN_PULSE
#define GAIN_PULSE 10
#endif
#ifndef GAIN_NOISE
#define GAIN_NOISE 7
#endif
// relative loudness of PICO-8 waveforms vs a GB pulse/wave at the same level (x/16)
static const uint8_t wloud[8] = {13, 13, 12, 10, 11, 13, 9, 12};
// hardware envelope: change per frame (vol*16 units) for periods 1..7 (1 step = 1/64 s)
static const uint8_t envstep[8] = {0, 17, 9, 6, 4, 3, 3, 2};
static const int8_t vib[8] = {-4, -2, 0, 2, 4, 2, 0, -2};

// ------------------------------------------------------------------ sequencing
static void seq_start(seq_t *q, const uint8_t *s) {
    q->s = s;
    q->idx = 255;
    q->played = 0;
    q->done = 0;
    q->dur = (uint16_t)(s[65] ? s[65] : 1) * TICK;
    q->acc = q->dur;  // first note starts immediately
    q->nd = (uint8_t)(q->dur >> 8);
    if (!q->nd) q->nd = 1;
}

// (v << 8) / nd without a division: recip_tab[nd-1] = 65536 / nd
static int16_t vol_step(int8_t v, uint8_t nd) {
    uint16_t r = recip_tab[nd - 1] >> 4;
    uint16_t m = (uint16_t)(v < 0 ? -v : v) * r;
    int16_t st = (int16_t)(m >> 4);
    return v < 0 ? -st : st;
}

// set up note idx of sequence q into n (prev = the previous note, for slides)
static void note_start(note_t *n, const seq_t *q, uint8_t lo, uint8_t hi) {
    uint8_t ppitch = n->pitch, pvol = n->vol, nd = q->nd;
    n->pitch = lo & 63;
    n->wave = ((lo >> 6) | (hi << 2)) & 7;
    n->vol = (hi >> 1) & 7;
    n->fx = (hi >> 4) & 7;
    n->t = 0;
    n->pbase = (int16_t)n->pitch << 4;
    n->vacc = (uint16_t)n->vol << 8;
    n->vstep = 0;
    n->mode = 0;
    switch (n->fx) {
    case 1:  // slide from the previous note's pitch and volume
        if (pvol) {
            int16_t pp = (int16_t)ppitch << 4;
            n->mode = 1;
            n->pacc = pp << 4;
            n->pstep = (int16_t)((n->pbase - pp) << 4) / (int16_t)nd;
            n->vacc = (uint16_t)pvol << 8;
            n->vstep = vol_step((int8_t)n->vol - (int8_t)pvol, nd);
        }
        break;
    case 2:
        n->mode = 2;
        break;
    case 3:  // drop
        n->mode = 1;
        n->pacc = n->pbase << 4;
        n->pstep = -(int16_t)((n->pbase << 4) / (int16_t)nd);
        break;
    case 4:  // fade in
        n->vacc = 0;
        n->vstep = vol_step(n->vol, nd);
        break;
    case 5:  // fade out
        n->vstep = -vol_step(n->vol, nd);
        break;
    default: {  // 6, 7: arpeggio over this group of 4 notes
        uint8_t g, k;
        if (n->fx < 6) break;
        g = q->idx & ~3;
        n->mode = 3;
        // fast: every 4 ticks (2 frames), slow: every 8 ticks; halved when speed <= 8
        n->arpsh = (n->fx == 6 ? 1 : 2) - (q->s[65] <= 8 ? 1 : 0);
        for (k = 0; k < 4; k++) n->arp[k] = (int16_t)(q->s[(g + k) * 2] & 63) << 4;
    }
    }
}

// pitch*16 for this frame, and advance the volume ramp
static int16_t note_pitch(note_t *n) {
    switch (n->mode) {
    case 1:
        n->pacc += n->pstep;
        return n->pacc >> 4;
    case 2:
        return n->pbase + vib[n->t & 7];
    case 3:
        return n->arp[(n->t >> n->arpsh) & 3];
    }
    return n->pbase;
}

static void note_vol_step(note_t *n) {
    if (n->vstep) {
        int16_t nv = (int16_t)n->vacc + n->vstep;
        if (nv < 0) nv = 0;
        if (nv > (7 << 8)) nv = 7 << 8;
        n->vacc = (uint16_t)nv;
    }
    n->t++;
}

// advance a sequence by one frame; returns 1 when a new note started
// (note data in *lo/*hi), sets q->done when it ran off the end
static uint8_t seq_advance(seq_t *q, uint8_t *lo, uint8_t *hi) {
    uint8_t started = 0;
    q->acc += 256;
    while (q->acc >= q->dur) {
        uint8_t ls = q->s[66], le = q->s[67];
        q->acc -= q->dur;
        q->idx++;
        q->played++;
        if (ls < le && q->idx >= le) q->idx = ls;
        if (q->idx >= 32) {
            q->idx = 31;
            q->done = 1;
            return started;
        }
        *lo = q->s[q->idx * 2];
        *hi = q->s[q->idx * 2 + 1];
        started = 1;
    }
    return started;
}

static void voice_start(uint8_t v, uint8_t n) {
    voice_t *p = &V[v];
    uint8_t i;
    const uint8_t *hi;
    seq_start(&p->sq, p8_sfx + (uint16_t)n * 68);
    p->on = 1;
    p->cust = 0;
    p->last = 0;
    hi = p->sq.s + 1;
    for (i = 0; i < 32; i++, hi += 2)
        if (*hi & 0x0E) p->last = i;
    p->n.vol = 0;
    p->n.pitch = 0;
    p->ov = 0;
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
        if (lead == 255 && !(V[c].sq.s[66] < V[c].sq.s[67])) lead = c;
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
    if (n < 0) {
        pat = -1;
        V[0].on = V[1].on = V[2].on = V[3].on = 0;
    } else {
        start_pattern(n);
    }
}

void musiclvl(uint8_t lvl) BANKED {
    uint8_t c;
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
                V[c].sq.idx = V[leader].sq.idx;
                V[c].sq.played = V[leader].sq.played;
                V[c].sq.acc = V[leader].sq.acc;
            }
        }
    }
}

void sfx(uint8_t n) BANKED {
    uint8_t v;
    if (!V[4].on) v = 4;
    else if (!V[5].on) v = 5;
    else { v = 4 + sfxnext; sfxnext ^= 1; }
    voice_start(v, n);
}

void sfx_stop(void) BANKED {
    V[4].on = V[5].on = 0;
}

// ------------------------------------------------------------------ per-frame voice update
// combined volume (vol*16, 0..240) from a 0..7 8.8 outer volume and optional instrument volume
static uint8_t mix_vol(uint16_t ovacc, uint16_t ivacc, uint8_t cust) {
    if (!cust) return (uint8_t)((ovacc * 34u) >> 8);          // *240/(7*256)
    return (uint8_t)(((ovacc >> 4) * (ivacc >> 4) * 5u) >> 8);  // *240/49/256
}

static uint16_t next_vacc(const note_t *n) {
    int16_t v = (int16_t)n->vacc + n->vstep;
    if (v < 0) v = 0;
    if (v > (7 << 8)) v = 7 << 8;
    return (uint16_t)v;
}

static void voice_frame(uint8_t vi) {
    voice_t *p = &V[vi];
    uint8_t lo, hi, nv;
    int16_t pitch;
    p->newnote = 0;
    {
        uint8_t started = seq_advance(&p->sq, &lo, &hi);
        if (vi < 4 && vi == leader && p->sq.played > 32) {
            next_pattern();
            return;
        }
        if (started && !p->sq.done) {
            uint8_t was_cust = p->cust, owave = p->n.wave;
            if (vi >= 4 && p->sq.idx > p->last && !(p->sq.s[66] < p->sq.s[67])) { p->on = 0; return; }
            note_start(&p->n, &p->sq, lo, hi);
            p->cust = hi >> 7;
            if (p->cust && p->n.vol) {
                // restart the instrument unless this note slides on from the same one
                if (!(was_cust && p->n.fx == 1 && owave == p->n.wave)) {
                    seq_start(&p->isq, p8_sfx + (uint16_t)p->n.wave * 68);
                    p->in.vol = 0;
                    p->in.pitch = 0;
                }
            }
            p->newnote = p->n.vol != 0;
        } else if (p->sq.done) {
            if (vi >= 4) { p->on = 0; return; }
            p->n.vol = 0;      // hold silence until the leader ends the pattern
            p->n.vacc = 0;
            p->n.vstep = 0;
            p->cust = 0;
        }
    }

    pitch = note_pitch(&p->n);
    if (p->cust && p->n.vol) {
        int16_t ip;
        if (seq_advance(&p->isq, &lo, &hi)) {
            uint8_t ow = p->in.wave, ov = p->in.vol, op = p->in.pitch;
            note_start(&p->in, &p->isq, lo, hi);
            // instrument notes that change pitch/wave or come in from silence retrigger
            if (!ov || ow != p->in.wave || (op != p->in.pitch && p->in.fx != 1)) p->newnote = 1;
        }
        if (p->isq.done) p->in.vacc = p->in.vstep = 0;
        ip = note_pitch(&p->in);
        pitch += ip - 24 * 16;          // instrument notes are relative to C2
        p->ow = p->in.wave;
        p->ov = mix_vol(p->n.vacc, p->in.vacc, 1);
        note_vol_step(&p->in);
        nv = mix_vol(next_vacc(&p->n), p->in.vacc, 1);
    } else {
        p->ow = p->n.wave;
        p->ov = mix_vol(p->n.vacc, 0, 0);
        nv = mix_vol(next_vacc(&p->n), 0, 0);
    }
    note_vol_step(&p->n);
    // perceptual compression, then per-waveform loudness
    {
        uint8_t a = vol_comp[p->ov >> 2], b = vol_comp[nv >> 2];
        p->ov = a ? (uint8_t)(((uint16_t)a * wloud[p->ow]) >> 4) : 0;
        p->odv = (int8_t)(((int16_t)b - a) * wloud[p->ow] >> 4);
    }
    if (pitch < 0) pitch = 0;
    p->op = pitch;
}

// ------------------------------------------------------------------ GB channel output
static uint16_t freq_of(int16_t p16, uint8_t up12) {
    uint8_t i = (uint8_t)(p16 >> 4) + 12 + up12;
    uint8_t f = p16 & 15;
    uint16_t a, b;
    if (i >= 107) return pitch_tab[107];
    a = pitch_tab[i];
    b = pitch_tab[i + 1];
    return a + (uint16_t)(((uint16_t)(b - a) * f) >> 4);
}

// envelope register value for target level v (vol*16) changing by dv per frame
static uint8_t env_reg(uint8_t ch, uint8_t v, int8_t dv) {
    uint8_t lvl = (v + 8) >> 4, p = 0, a;
    if (lvl > 15) lvl = 15;
    a = dv < 0 ? (uint8_t)(-dv) : (uint8_t)dv;
    if (a) {
        p = (uint8_t)((17 + (a >> 1)) / a);   // frames per level ~ 0.93 * period
        if (p > 7) p = 0;
        else if (!p) p = 1;
    }
    hwvol[ch] = lvl << 4;
    hwstep[ch] = p ? (dv < 0 ? -(int8_t)envstep[p] : (int8_t)envstep[p]) : 0;
    return (lvl << 4) | (dv > 0 && p ? 8 : 0) | p;
}

static void hw_tick(uint8_t ch) {
    int16_t h = (int16_t)hwvol[ch] + hwstep[ch];
    if (h < 0) h = 0;
    if (h > 240) h = 240;
    hwvol[ch] = (uint8_t)h;
}

static uint8_t drift(uint8_t ch, uint8_t v) {
    int16_t d = (int16_t)hwvol[ch] - v;
    if ((hwvol[ch] < 8) != (v < 8)) return 1;   // silent vs audible mismatch
    return d > 40 || d < -40;                  // off by more than 2.5 levels
}

static uint8_t gv;    // channel-gained level for this output
static int8_t gdv;

static void gain(voice_t *p, uint8_t g) {
    gv = (uint8_t)(((uint16_t)p->ov * g) >> 4);
    gdv = (int8_t)(((int16_t)p->odv * g) >> 4);
}

static void pulse_out(uint8_t ch, voice_t *p, uint8_t trig) {
    uint16_t fr;
    uint8_t w = p->ow;
    gain(p, GAIN_PULSE);
    if (p->op == chop[ch] && !trig) fr = chfreq[ch];
    else { fr = freq_of(p->op, 0); chop[ch] = p->op; }
    hw_tick(ch);
    if (trig || duty_of[w] != duty_of[chwave[ch]] || drift(ch, gv)) {
        uint8_t env = env_reg(ch, gv, gdv);
        if (ch == 0) {
            NR10_REG = 0;
            NR11_REG = duty_of[w] << 6;
            NR12_REG = env;
            NR13_REG = fr & 255;
            NR14_REG = 0x80 | (fr >> 8);
        } else {
            NR21_REG = duty_of[w] << 6;
            NR22_REG = env;
            NR23_REG = fr & 255;
            NR24_REG = 0x80 | (fr >> 8);
        }
        chwave[ch] = w;
    } else if (fr != chfreq[ch]) {
        if (ch == 0) { NR13_REG = fr & 255; NR14_REG = fr >> 8; }
        else { NR23_REG = fr & 255; NR24_REG = fr >> 8; }
    }
    chfreq[ch] = fr;
}

static void noise_out(voice_t *p, uint8_t trig) {
    uint8_t nr = noise_tab[(p->op >> 4) & 63];
    gain(p, GAIN_NOISE);
    hw_tick(3);
    if (trig || nr != chfreq[3] || drift(3, gv)) {
        NR42_REG = env_reg(3, gv, gdv);
        NR43_REG = nr;
        NR44_REG = 0x80;
        chfreq[3] = nr;
    }
}

static void wave_out(voice_t *p, uint8_t trig) {
    uint8_t w = p->ow, v = p->ov, level;
    uint16_t fr;
    // wave channel: 4 output levels, changeable without retriggering
    level = v < 24 ? 0 : v < 96 ? 3 : v < 176 ? 2 : 1;
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
    if (level != wave_level) {
        NR32_REG = level << 5;
        wave_level = level;
    }
    if (trig) {
        NR30_REG = 0x80;
        NR33_REG = fr & 255;
        NR34_REG = 0x80 | (fr >> 8);
    } else if (fr != chfreq[2]) {
        NR33_REG = fr & 255;
        NR34_REG = fr >> 8;
    }
    chfreq[2] = fr;
}

static void ch_silence(uint8_t ch) {
    if (chown[ch] == 255) return;
    chown[ch] = 255;
    switch (ch) {
    case 0: NR12_REG = 0; NR14_REG = 0x80; break;
    case 1: NR22_REG = 0; NR24_REG = 0x80; break;
    case 2: NR30_REG = 0; wave_loaded = 255; wave_level = 255; break;
    case 3: NR42_REG = 0; NR44_REG = 0x80; break;
    }
    hwvol[ch] = 0;
    hwstep[ch] = 0;
}

// a voice keeps its channel while its note is held (even through a fade to 0)
#define ACTIVE(v) (V[v].on && (V[v].ov || V[v].n.vol))
#define TONAL(v) (ACTIVE(v) && V[v].ow != 6)
#define NOISY(v) (ACTIVE(v) && V[v].ow == 6)

static void assign(uint8_t ch, uint8_t v) {
    uint8_t trig;
#ifdef MUTE
    if (MUTE & (1 << ch)) v = 255;   // test builds: silence GB channels
#endif
    if (v == 255) { ch_silence(ch); return; }
    trig = V[v].newnote || chown[ch] != v;
    chown[ch] = v;
    if (ch == 2) wave_out(&V[v], trig);
    else if (ch == 3) noise_out(&V[v], trig);
    else pulse_out(ch, &V[v], trig);
}

void snd_update(void) {
    uint8_t v, o;
    for (v = 0; v < NV; v++)
        if (V[v].on) voice_frame(v);
    // pulse1: sfx4 > music2
    o = TONAL(4) ? 4 : TONAL(2) ? 2 : 255;
    assign(0, o);
    // pulse2: sfx5 > music3 > music1
    o = TONAL(5) ? 5 : TONAL(3) ? 3 : TONAL(1) ? 1 : 255;
    assign(1, o);
    // wave: music0
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
