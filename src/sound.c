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
#include <stddef.h>
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

voice_t V[NV];
uint8_t snd_gv, snd_ff;   // (assembly: the channel-gained level, freq_of's f)
int8_t snd_gdv;
static int8_t pat = -1;
static uint8_t lvlmask = 0x0F;
uint8_t leader;
static uint8_t sfxnext;

// per-GB-channel state
uint8_t chown[4] = {255, 255, 255, 255};
uint8_t chwave[4];
uint16_t chfreq[4];
int16_t chop[4] = {-1, -1, -1, -1};
uint8_t hwvol[4];        // simulated hardware level * 16
int8_t hwstep[4];        // simulated envelope change per frame
uint8_t wave_loaded = 255, wave_level = 255, wave_hold, wave_quiet;

// wave-channel (bass) waveforms, scaled to 70% amplitude around the midpoint
// so the bass doesn't dominate or distort small speakers
const uint8_t waves[7][16] = {
    {0x23,0x44,0x56,0x67,0x89,0x9A,0xBB,0xCD,0xDC,0xBB,0xA9,0x98,0x76,0x65,0x44,0x32}, // tri
    {0x23,0x44,0x56,0x67,0x89,0x9A,0xBB,0xCD,0xDB,0xA9,0x76,0x43,0x22,0x22,0x22,0x22}, // tilted saw
    {0x22,0x33,0x44,0x44,0x55,0x66,0x66,0x77,0x88,0x99,0x99,0xAA,0xBB,0xBB,0xCC,0xDD}, // saw
    {0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22}, // square
    {0xDD,0xDD,0xDD,0xDD,0xDD,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22}, // pulse
    {0x8A,0xBC,0xCC,0xBA,0x86,0x54,0x45,0x68,0x99,0x99,0x97,0x65,0x44,0x56,0x89,0xAB}, // organ
    {0x34,0x45,0x66,0x78,0x99,0xAB,0xBC,0xDD,0xDC,0xBB,0xA9,0x98,0x76,0x65,0x44,0x33}, // phaser (soft)
};
// pulse duty per PICO-8 waveform: 0=12.5% 1=25% 2=50% (50% has the fewest harmonics)
const uint8_t duty_of[8] = {2, 2, 1, 2, 1, 2, 2, 1};
// mix balance per GB channel (x/16): PICO-8's mix is bass heavy, GB pulses/noise are bright
#ifndef GAIN_PULSE
#define GAIN_PULSE 10
#endif
#ifndef GAIN_NOISE
#define GAIN_NOISE 7
#endif
// relative loudness of PICO-8 waveforms vs a GB pulse/wave at the same level (x/16)
const uint8_t wloud[8] = {13, 13, 12, 10, 11, 13, 9, 12};
// hardware envelope: change per frame (vol*16 units) for periods 1..7 (1 step = 1/64 s)
const uint8_t envstep[8] = {0, 17, 9, 6, 4, 3, 3, 2};
const int8_t vib[8] = {-4, -2, 0, 2, 4, 2, 0, -2};

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





// a frame on a sequence's clock: acc += 256; returns 1 if a note is due (acc >= dur), when
// seq_step takes over. Most frames start no note, and SDCC's code for this through the
// struct pointer (a 17-byte stack frame) cost about 1000 cycles a call, 12 calls a frame.
// q in DE (seq_t: s 0-1, idx 2, played 3, nd 4, acc 5-6, dur 7-8)
static uint8_t seq_tick(seq_t *q) __naked {
    (void)q;
    __asm
    ld  hl, #5
    add hl, de
    ld  c, (hl)             ; acc low
    inc hl
    inc (hl)                ; acc high += 1
    ld  b, (hl)
    inc hl
    ld  a, c                ; acc - dur: carry if acc < dur
    sub a, (hl)
    inc hl
    ld  a, b
    sbc a, (hl)
    ld  a, #0
    ret c
    inc a
    ret
    __endasm;
}


static void voice_start(uint8_t v, uint8_t n) {
    voice_t *p = &V[v];
    seq_start(&p->sq, p8_sfx + (uint16_t)n * 68);
    p->on = 1;
    p->cust = 0;
    p->last = p8_sfx_last[n];   // its last note with a volume (gen_assets.py)
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

void next_pattern(void) {
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


// ---- per-frame voice update, in assembly (SDCC's code for these struct-pointer loops took
// a fifth of every frame). The rare parts, a note starting, stay in C (voice_seq, voice_iseq).
// The offsets below are checked against the structs.
#define VO_CUST 2
#define VO_NEWNOTE 3
#define VO_SQ 4
#define VO_N 14
#define VO_ISQ 39
#define VO_IN 49
#define VO_OP 74
#define VO_OV 76
#define VO_ODV 77
#define VO_OW 78
#define NO_WAVE 1
#define NO_VOL 2
#define NO_MODE 4
#define NO_ARPSH 5
#define NO_T 6
#define NO_PBASE 7
#define NO_PACC 9
#define NO_PSTEP 11
#define NO_VACC 13
#define NO_VSTEP 15
#define NO_ARP 17
#define SQ_DONE 9
#define OFFCHK(t, f, v) typedef char chk_##t##_##f[(offsetof(t, f) == (v)) ? 1 : -1]
OFFCHK(voice_t, cust, VO_CUST); OFFCHK(voice_t, newnote, VO_NEWNOTE); OFFCHK(voice_t, sq, VO_SQ);
OFFCHK(voice_t, n, VO_N); OFFCHK(voice_t, isq, VO_ISQ); OFFCHK(voice_t, in, VO_IN);
OFFCHK(voice_t, op, VO_OP); OFFCHK(voice_t, ov, VO_OV); OFFCHK(voice_t, odv, VO_ODV);
OFFCHK(voice_t, ow, VO_OW); OFFCHK(note_t, wave, NO_WAVE); OFFCHK(note_t, vol, NO_VOL);
OFFCHK(note_t, mode, NO_MODE); OFFCHK(note_t, arpsh, NO_ARPSH); OFFCHK(note_t, t, NO_T);
OFFCHK(note_t, pbase, NO_PBASE); OFFCHK(note_t, pacc, NO_PACC); OFFCHK(note_t, pstep, NO_PSTEP);
OFFCHK(note_t, vacc, NO_VACC); OFFCHK(note_t, vstep, NO_VSTEP); OFFCHK(note_t, arp, NO_ARP);
OFFCHK(seq_t, acc, 5); OFFCHK(seq_t, dur, 7); OFFCHK(seq_t, done, SQ_DONE);
typedef char chk_voice_size[(sizeof(voice_t) == 79) ? 1 : -1];

uint8_t vs_vi;   // the voice voice_seq works on

// ---- a note starting, in assembly. q (seq_t) in HL for the sequence routines.

// the notes due on a sequence after snd_seq_tick: HL = q -> A = 1 when a note started, then
// D = its lo byte, E = its hi byte; q->done set when it ran off the end
void snd_notes(void) __naked {
    __asm
_snd_seqstep:
    add sp, #-6             ; 0-1 q, 2 started, 3 lo, 4 hi
    ld  c, l
    ld  a, h
    ldhl sp, #1
    ld  (hl-), a
    ld  (hl), c
    ldhl sp, #2
    ld  (hl), #0
1$:
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  bc, #5
    add hl, bc              ; &acc
    ld  a, (hl+)
    ld  c, a
    ld  b, (hl)
    inc hl                  ; &dur
    ld  a, c
    sub a, (hl)
    ld  e, a
    inc hl
    ld  a, b
    sbc a, (hl)
    jr  c, 9$               ; acc < dur: no more
    ld  d, a                ; acc -= dur
    dec hl
    dec hl
    ld  (hl), d
    dec hl
    ld  (hl), e
    dec hl
    dec hl                  ; &played
    inc (hl)
    dec hl                  ; &idx
    inc (hl)
    ld  b, (hl)             ; B = idx
    dec hl
    ld  a, (hl-)
    ld  l, (hl)
    ld  h, a                ; HL = s
    push hl
    ld  de, #66
    add hl, de
    ld  a, (hl+)
    ld  c, a                ; C = loop start
    ld  a, (hl)             ; loop end
    cp  a, c
    jr  c, 2$
    jr  z, 2$               ; (no loop)
    ld  d, a
    ld  a, b
    cp  a, d
    jr  c, 2$
    ld  b, c                ; past the loop end: back to its start
2$:
    ld  a, b
    cp  a, #32
    jr  c, 3$
    ld  b, #31              ; ran off the end
    ldhl sp, #2
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    inc hl
    inc hl
    ld  (hl), b
    ld  de, #(SQ_DONE - 2)
    add hl, de
    ld  (hl), #1
    pop hl
    jr  9$
3$:
    pop hl
    ld  a, b
    add a, a
    ld  e, a
    ld  d, #0
    add hl, de
    ld  a, (hl+)
    ld  d, a                ; lo
    ld  e, (hl)             ; hi
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    inc hl
    inc hl
    ld  (hl), b             ; idx
    ldhl sp, #2
    ld  (hl), #1
    inc hl
    ld  (hl), d
    inc hl
    ld  (hl), e
    jr  1$
9$:
    ldhl sp, #2
    ld  a, (hl+)
    ld  d, (hl)
    inc hl
    ld  e, (hl)
    add sp, #6
    ret

; HL = q, DE = s: the sequence starts (seq_start)
_snd_seqstart:
    ld  (hl), e
    inc hl
    ld  (hl), d
    inc hl
    ld  (hl), #255          ; idx
    inc hl
    ld  (hl), #0            ; played
    push hl
    ld  hl, #65
    add hl, de
    ld  a, (hl)             ; speed
    or  a, a
    jr  nz, 1$
    inc a
1$:
    ld  e, a                ; dur = speed * TICK (127): (speed << 7) - speed
    ld  d, #0
    ld  h, d
    ld  l, a
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    ld  a, l
    sub a, e
    ld  c, a
    ld  a, h
    sbc a, d
    ld  b, a                ; BC = dur
    pop hl
    inc hl                  ; &nd
    or  a, a
    jr  nz, 2$
    inc a
2$:
    ld  (hl+), a            ; nd = dur >> 8, at least 1
    ld  (hl), c             ; acc = dur: the first note starts at once
    inc hl
    ld  (hl), b
    inc hl
    ld  (hl), c             ; dur
    inc hl
    ld  (hl), b
    inc hl
    ld  (hl), #0            ; done
    ret

; HL = x (int16), C = d (1..255): HL = x / d, truncated toward 0 as C
_snd_sdiv:
    bit 7, h
    jr  z, _snd_udiv
    xor a, a
    sub a, l
    ld  l, a
    ld  a, #0
    sbc a, h
    ld  h, a
    call _snd_udiv
    xor a, a
    sub a, l
    ld  l, a
    ld  a, #0
    sbc a, h
    ld  h, a
    ret
; HL = HL / C (unsigned). Clobbers A, B.
_snd_udiv:
    xor a, a
    ld  b, #16
1$:
    add hl, hl
    rla
    jr  c, 2$
    cp  a, c
    jr  c, 3$
2$:
    sub a, c
    inc l
3$:
    dec b
    jr  nz, 1$
    ret

; A = v (int8, -7..7), C = nd: BC = (v << 8) / nd as (|v| * (recip_tab[nd - 1] >> 4)) >> 4
_snd_volstep:
    push hl
    push de
    ld  e, a                ; E = v
    ld  b, #0
    dec c
    ld  hl, #_recip_tab
    add hl, bc
    add hl, bc
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a                ; HL = recip_tab[nd - 1]
    srl h
    rr  l
    srl h
    rr  l
    srl h
    rr  l
    srl h
    rr  l                   ; r
    ld  a, e
    bit 7, a
    jr  z, 1$
    cpl
    inc a
1$:
    ld  d, h
    ld  c, l                ; DC = r
    ld  hl, #0
    or  a, a
    jr  z, 3$
    ld  b, a
2$:
    ld  a, l
    add a, c
    ld  l, a
    ld  a, h
    adc a, d
    ld  h, a
    dec b
    jr  nz, 2$
3$:
    srl h                   ; m >> 4
    rr  l
    srl h
    rr  l
    srl h
    rr  l
    srl h
    rr  l
    bit 7, e
    jr  z, 4$
    xor a, a
    sub a, l
    ld  l, a
    ld  a, #0
    sbc a, h
    ld  h, a
4$:
    ld  b, h
    ld  c, l
    pop de
    pop hl
    ret

; HL = n (note_t), DE = q (its seq_t), B = lo, C = hi: the note starts (note_start).
; Frame: 0 ppitch, 1 pvol, 2 nd, 3 fx, 4-5 n, 6-7 q, 8 hi, 9 lo.
_snd_notestart:
    push bc
    push de
    push hl
    add sp, #-4
    ldhl sp, #4
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  b, (hl)             ; previous pitch
    inc hl
    inc hl
    ld  c, (hl)             ; previous volume
    ldhl sp, #0
    ld  (hl), b
    inc hl
    ld  (hl), c
    ldhl sp, #6
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  bc, #4
    add hl, bc
    ld  a, (hl)             ; q->nd
    ldhl sp, #2
    ld  (hl), a
    ldhl sp, #9
    ld  b, (hl)             ; lo
    dec hl
    ld  c, (hl)             ; hi
    ldhl sp, #4
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  a, b
    and a, #63
    ld  (hl+), a            ; pitch
    ld  d, a
    ld  a, c
    add a, a
    add a, a
    ld  e, a
    ld  a, b
    rlca
    rlca
    and a, #3
    or  a, e
    and a, #7
    ld  (hl+), a            ; wave = ((lo >> 6) | (hi << 2)) & 7
    ld  a, c
    srl a
    and a, #7
    ld  (hl+), a            ; vol = (hi >> 1) & 7
    ld  e, a
    ld  a, c
    swap a
    and a, #7
    ld  (hl+), a            ; fx = (hi >> 4) & 7
    ld  c, a
    xor a, a
    ld  (hl+), a            ; mode = 0
    inc hl
    ld  (hl+), a            ; t = 0
    ld  a, d
    swap a
    ld  b, a
    and a, #0xF0
    ld  (hl+), a            ; pbase = pitch << 4
    ld  a, b
    and a, #0x0F
    ld  (hl+), a
    inc hl
    inc hl
    inc hl
    inc hl
    xor a, a
    ld  (hl+), a            ; vacc = vol << 8
    ld  a, e
    ld  (hl+), a
    xor a, a
    ld  (hl+), a            ; vstep = 0
    ld  (hl), a
    ld  a, c
    ldhl sp, #3
    ld  (hl), a             ; fx
    dec a
    jr  z, 10$
    dec a
    jp  z, 20$
    dec a
    jp  z, 30$
    dec a
    jp  z, 40$
    dec a
    jp  z, 50$
    dec a
    jp  z, 60$
    dec a
    jp  z, 60$
    jp  99$
10$:                        ; 1: slide from the previous pitch and volume
    ldhl sp, #1
    ld  a, (hl)
    or  a, a
    jp  z, 99$
    ldhl sp, #4
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    ld  bc, #NO_MODE
    add hl, bc
    ld  (hl), #1
    ld  bc, #(NO_PACC - NO_MODE)
    add hl, bc
    ld  (hl), #0            ; pacc = ppitch << 8
    inc hl
    push hl
    ldhl sp, #4             ; ppitch (two pushes)
    ld  a, (hl)
    pop hl
    ld  (hl+), a
    push hl                 ; &pstep
    ; pstep = ((pitch - ppitch) << 8) / nd
    ldhl sp, #2             ; (n, &pstep pushed)
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  a, (hl)             ; pitch
    ldhl sp, #4
    sub a, (hl)             ; - ppitch
    ld  h, a
    ld  l, #0
    push hl
    ldhl sp, #8             ; nd (three pushes)
    ld  c, (hl)
    pop hl
    call _snd_sdiv
    ld  b, h
    ld  c, l
    pop hl
    ld  (hl), c
    inc hl
    ld  (hl), b
    inc hl
    ld  (hl), #0            ; vacc = pvol << 8
    inc hl
    push hl
    ldhl sp, #5             ; pvol (two pushes)
    ld  a, (hl)
    pop hl
    ld  (hl+), a
    push hl                 ; &vstep
    ; vstep = vol_step(vol - pvol, nd)
    ldhl sp, #2
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    inc hl
    inc hl
    ld  a, (hl)             ; vol
    ldhl sp, #5
    sub a, (hl)             ; - pvol
    ldhl sp, #6
    ld  c, (hl)             ; nd
    call _snd_volstep
    pop hl
    ld  (hl), c
    inc hl
    ld  (hl), b
    pop hl
    jp  99$
20$:                        ; 2: vibrato
    ldhl sp, #4
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  bc, #NO_MODE
    add hl, bc
    ld  (hl), #2
    jp  99$
30$:                        ; 3: drop: pacc = pbase << 4, pstep = -((pbase << 4) / nd)
    ldhl sp, #4
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    ld  bc, #NO_MODE
    add hl, bc
    ld  (hl), #1
    pop hl
    push hl
    ld  a, (hl)             ; pitch
    ld  bc, #NO_PACC
    add hl, bc
    ld  (hl), #0
    inc hl
    ld  (hl), a
    ld  h, a
    ld  l, #0
    ldhl sp, #4
    push af
    ld  c, (hl)
    pop af
    pop hl
    push hl
    ld  h, a
    ld  l, #0
    call _snd_udiv
    xor a, a
    sub a, l
    ld  c, a
    ld  a, #0
    sbc a, h
    ld  b, a
    pop hl
    ld  de, #NO_PSTEP
    add hl, de
    ld  (hl), c
    inc hl
    ld  (hl), b
    jp  99$
40$:                        ; 4: fade in: vacc = 0, vstep = vol_step(vol, nd)
    ldhl sp, #4
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    inc hl
    inc hl
    ld  a, (hl)             ; vol
    push af
    ldhl sp, #6             ; nd (two pushes)
    ld  c, (hl)
    pop af
    call _snd_volstep
    pop hl
    ld  de, #NO_VACC
    add hl, de
    xor a, a
    ld  (hl+), a
    ld  (hl+), a
    ld  (hl), c
    inc hl
    ld  (hl), b
    jr  99$
50$:                        ; 5: fade out: vstep = -vol_step(vol, nd)
    ldhl sp, #4
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    inc hl
    inc hl
    ld  a, (hl)
    push af
    ldhl sp, #6
    ld  c, (hl)
    pop af
    call _snd_volstep
    xor a, a
    sub a, c
    ld  c, a
    ld  a, #0
    sbc a, b
    ld  b, a
    pop hl
    ld  de, #NO_VSTEP
    add hl, de
    ld  (hl), c
    inc hl
    ld  (hl), b
    jr  99$
60$:                        ; 6, 7: arpeggio over this group of 4 notes
    ldhl sp, #6
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a                ; q
    ld  a, (hl+)
    ld  e, a
    ld  a, (hl+)
    ld  d, a                ; DE = s
    ld  a, (hl)             ; idx
    and a, #0xFC            ; g
    ld  b, a
    ld  hl, #65
    add hl, de
    ld  a, (hl)             ; speed
    cp  a, #9
    ld  a, #0
    adc a, #0               ; 1 when speed <= 8
    ld  c, a
    ldhl sp, #3
    ld  a, (hl)             ; fx
    sub a, #5               ; 1 for 6, 2 for 7
    sub a, c
    ld  c, a                ; arpsh
    push de
    ldhl sp, #6
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a                ; n
    push hl
    ld  de, #NO_MODE
    add hl, de
    ld  (hl), #3
    inc hl
    ld  (hl), c             ; arpsh
    pop hl
    ld  de, #NO_ARP
    add hl, de              ; &arp
    pop de                  ; s
    ld  a, b
    add a, a
    add a, e
    ld  e, a
    jr  nc, 61$
    inc d                   ; DE = s + g * 2
61$:
    ld  c, #4
62$:
    ld  a, (de)
    inc de
    inc de
    and a, #63
    swap a
    ld  b, a
    and a, #0xF0
    ld  (hl+), a
    ld  a, b
    and a, #0x0F
    ld  (hl+), a            ; arp[k] = (s[(g + k) * 2] & 63) << 4
    dec c
    jr  nz, 62$
99$:
    add sp, #10
    ret

; a note due on a voices own sequence (snd_seq_tick said so): DE = p (voice vs_vi) -> A = 1
; when nothing more is done for it this frame. Frame: 0-1 p, 2 started, 3 lo, 4 hi,
; 5 was_cust, 6 owave.
_snd_voice_seq:
    add sp, #-8
    ldhl sp, #0
    ld  (hl), e
    inc hl
    ld  (hl), d
    ld  h, d
    ld  l, e
    ld  bc, #VO_SQ
    add hl, bc
    call _snd_seqstep
    ldhl sp, #2
    ld  (hl+), a
    ld  (hl), d
    inc hl
    ld  (hl), e
    ; the leading voice ends its pattern after 32 notes
    ld  a, (_vs_vi)
    cp  a, #4
    jr  nc, 1$
    ld  b, a
    ld  a, (_leader)
    cp  a, b
    jr  nz, 1$
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  bc, #(VO_SQ + 3)
    add hl, bc
    ld  a, (hl)             ; played
    cp  a, #33
    jr  c, 1$
    add sp, #8
    call _next_pattern
    ld  a, #1
    ret
1$:
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    ld  bc, #(VO_SQ + SQ_DONE)
    add hl, bc
    ld  c, (hl)             ; done
    pop hl
    ldhl sp, #2
    ld  a, (hl)             ; started
    or  a, a
    jp  z, 20$
    ld  a, c
    or  a, a
    jp  nz, 20$
    ; a new note
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    inc hl
    inc hl
    ld  a, (hl)             ; cust
    ld  bc, #(VO_N + NO_WAVE - VO_CUST)
    add hl, bc
    ld  b, (hl)             ; n.wave
    ldhl sp, #7             ; (the push)
    ld  (hl+), a
    ld  (hl), b
    pop hl
    ; an sfx voice stops after its last audible note, unless it loops
    ld  a, (_vs_vi)
    cp  a, #4
    jr  c, 3$
    push hl
    ld  bc, #(VO_SQ + 2)
    add hl, bc
    ld  b, (hl)             ; idx
    pop hl
    push hl
    inc hl
    ld  a, (hl)             ; last
    cp  a, b
    pop hl
    jr  nc, 3$              ; idx <= last
    push hl
    ld  bc, #VO_SQ
    add hl, bc
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  bc, #66
    add hl, bc
    ld  a, (hl+)
    ld  b, (hl)
    cp  a, b                ; loop start < loop end: it loops
    pop hl
    jr  c, 3$
    ld  (hl), #0            ; on = 0
    add sp, #8
    ld  a, #1
    ret
3$:
    ; note_start(&n, &sq, lo, hi)
    push hl
    ld  bc, #VO_SQ
    add hl, bc
    ld  d, h
    ld  e, l
    pop hl
    push hl
    ld  bc, #VO_N
    add hl, bc
    push hl
    ldhl sp, #7             ; lo (two pushes)
    ld  b, (hl)
    inc hl
    ld  c, (hl)             ; hi
    pop hl
    call _snd_notestart
    pop hl                  ; p
    ldhl sp, #4
    ld  a, (hl)             ; hi
    rlca
    and a, #1
    ld  b, a                ; cust = hi >> 7
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    inc hl
    inc hl
    ld  (hl), b
    ld  a, b
    or  a, a
    jr  z, 6$
    ld  bc, #(VO_N + NO_VOL - VO_CUST)
    add hl, bc
    ld  a, (hl)             ; n.vol
    or  a, a
    jr  z, 6$
    ; restart the instrument, unless this note slides on from the same one
    ldhl sp, #5
    ld  a, (hl)             ; was_cust
    or  a, a
    jr  z, 5$
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  bc, #(VO_N + 3)
    add hl, bc
    ld  a, (hl)             ; fx
    dec a
    jr  nz, 5$
    ld  bc, #(NO_WAVE - 3)
    add hl, bc
    ld  b, (hl)             ; n.wave
    ldhl sp, #6
    ld  a, (hl)             ; owave
    cp  a, b
    jr  z, 6$
5$:
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    ld  bc, #(VO_N + NO_WAVE)
    add hl, bc
    ld  a, (hl)             ; wave: p8_sfx + wave * 68
    ld  l, a
    ld  h, #0
    add hl, hl
    add hl, hl
    ld  d, h
    ld  e, l                ; wave * 4
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl              ; wave * 64
    add hl, de
    ld  de, #_p8_sfx
    add hl, de
    ld  d, h
    ld  e, l
    pop hl
    push hl
    ld  bc, #VO_ISQ
    add hl, bc
    call _snd_seqstart
    pop hl
    ld  bc, #VO_IN
    add hl, bc
    xor a, a
    ld  (hl+), a            ; in.pitch = 0
    inc hl
    ld  (hl), a             ; in.vol = 0
6$:
    ldhl sp, #0             ; newnote = n.vol != 0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    ld  bc, #(VO_N + NO_VOL)
    add hl, bc
    ld  a, (hl)
    pop hl
    or  a, a
    jr  z, 7$
    ld  a, #1
7$:
    inc hl
    inc hl
    inc hl
    ld  (hl), a
    xor a, a
    add sp, #8
    ret
20$:
    ; ran off the end: an sfx voice stops; music holds silence until the leader ends
    ; the pattern
    ld  a, c
    or  a, a
    jr  z, 22$
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  a, (_vs_vi)
    cp  a, #4
    jr  c, 21$
    ld  (hl), #0
    add sp, #8
    ld  a, #1
    ret
21$:
    inc hl
    inc hl
    ld  (hl), #0            ; cust = 0
    ld  bc, #(VO_N + NO_VOL - VO_CUST)
    add hl, bc
    xor a, a
    ld  (hl), a             ; n.vol = 0
    ld  bc, #(NO_VACC - NO_VOL)
    add hl, bc
    ld  (hl+), a            ; n.vacc = 0
    ld  (hl+), a
    ld  (hl+), a            ; n.vstep = 0
    ld  (hl), a
22$:
    xor a, a
    add sp, #8
    ret

; a note due on a voices instrument sequence: DE = p. Frame: 0-1 p, 2 ow, 3 ov.
_snd_voice_iseq:
    add sp, #-4
    ldhl sp, #0
    ld  (hl), e
    inc hl
    ld  (hl), d
    ld  h, d
    ld  l, e
    ld  bc, #VO_ISQ
    add hl, bc
    call _snd_seqstep
    or  a, a
    jr  z, 9$
    push de                 ; lo, hi
    ldhl sp, #2
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    ld  bc, #(VO_IN + NO_WAVE)
    add hl, bc
    ld  a, (hl+)            ; in.wave
    ld  b, (hl)             ; in.vol
    ldhl sp, #6             ; (two pushes)
    ld  (hl+), a
    ld  (hl), b
    pop hl                  ; p
    push hl
    ld  bc, #VO_ISQ
    add hl, bc
    ld  d, h
    ld  e, l
    pop hl
    ld  bc, #VO_IN
    add hl, bc
    pop bc                  ; B = lo, C = hi
    call _snd_notestart
    ; only instrument notes that change waveform or come in from silence retrigger;
    ; pitch changes (arpeggios, runs) just retune as PICO-8
    ldhl sp, #3
    ld  a, (hl)             ; ov
    or  a, a
    jr  z, 1$
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  bc, #(VO_IN + NO_WAVE)
    add hl, bc
    ld  b, (hl)
    ldhl sp, #2
    ld  a, (hl)             ; ow
    cp  a, b
    jr  z, 9$
1$:
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    inc hl
    inc hl
    inc hl
    ld  (hl), #1            ; newnote
9$:
    add sp, #4
    ret
    __endasm;
}

// one voice's frame: its sequences, then its pitch and volume for the outputs. p in DE.
void voice_frame(voice_t *p) __naked {
    (void)p;
    __asm
    push de                 ; [sp] = p
    ld  h, d
    ld  l, e
    inc hl
    inc hl
    inc hl
    ld  (hl), #0            ; newnote = 0
    inc hl                  ; &sq
    ld  d, h
    ld  e, l
    call _seq_tick
    or  a, a
    jr  z, 1$
    pop de
    push de
    call _snd_voice_seq
    or  a, a
    jr  z, 1$
    pop hl
    ret
1$:
    ; the instrument, while the note has one and a volume
    pop hl
    push hl
    inc hl
    inc hl
    ld  a, (hl)             ; cust
    or  a, a
    jr  z, 3$
    ld  de, #(VO_N + NO_VOL - VO_CUST)
    add hl, de
    ld  a, (hl)             ; n.vol
    or  a, a
    jr  z, 3$
    pop hl
    push hl
    ld  de, #VO_ISQ
    add hl, de
    ld  d, h
    ld  e, l
    call _seq_tick
    or  a, a
    jr  z, 2$
    pop de
    push de
    call _snd_voice_iseq
2$:
    pop hl
    push hl
    ld  de, #(VO_ISQ + SQ_DONE)
    add hl, de
    ld  a, (hl)
    or  a, a
    jr  z, 3$
    ld  de, #(VO_IN + NO_VACC - VO_ISQ - SQ_DONE)
    add hl, de
    xor a, a                ; isq done: in.vacc = in.vstep = 0
    ld  (hl+), a
    ld  (hl+), a
    ld  (hl+), a
    ld  (hl), a
3$:
    pop de
    jp  _snd_voice_render
    __endasm;
}

// HL = note_t *; BC = its pitch * 16 this frame (a pitch ramp advances). Clobbers A, DE, HL.
void snd_helpers(void) __naked {
    __asm
_snd_np:
    push hl
    ld  de, #NO_MODE
    add hl, de
    ld  a, (hl)
    pop hl
    dec a
    jr  z, 1$
    dec a
    jr  z, 2$
    dec a
    jr  z, 3$
    ld  de, #NO_PBASE       ; static: pbase
    add hl, de
    ld  c, (hl)
    inc hl
    ld  b, (hl)
    ret
1$:                         ; ramp: pacc += pstep, pitch = pacc >> 4
    ld  de, #NO_PACC
    add hl, de
    ld  c, (hl)
    inc hl
    ld  b, (hl)
    inc hl
    ld  a, (hl+)
    ld  e, a
    ld  d, (hl)
    ld  a, c
    add a, e
    ld  c, a
    ld  a, b
    adc a, d
    ld  b, a
    dec hl
    dec hl
    ld  (hl-), a
    ld  (hl), c
    sra b
    rr  c
    sra b
    rr  c
    sra b
    rr  c
    sra b
    rr  c
    ret
2$:                         ; vibrato: pbase + vib[t & 7]
    ld  de, #NO_T
    add hl, de
    ld  a, (hl+)
    and a, #7
    ld  c, (hl)
    inc hl
    ld  b, (hl)
    ld  e, a
    ld  d, #0
    ld  hl, #_vib
    add hl, de
    ld  a, (hl)
    ld  e, a
    rlca
    sbc a, a
    ld  d, a
    ld  a, c
    add a, e
    ld  c, a
    ld  a, b
    adc a, d
    ld  b, a
    ret
3$:                         ; arpeggio: arp[(t >> arpsh) & 3]
    push hl
    ld  de, #NO_ARPSH
    add hl, de
    ld  a, (hl+)
    ld  b, a
    ld  a, (hl)
    inc b
    jr  5$
4$:
    srl a
5$:
    dec b
    jr  nz, 4$
    and a, #3
    add a, a
    add a, #NO_ARP
    ld  e, a
    ld  d, #0
    pop hl
    add hl, de
    ld  c, (hl)
    inc hl
    ld  b, (hl)
    ret

; HL = note_t *: its volume steps (vacc += vstep, kept to 0..7 << 8) and t++.
; DE = vacc before, BC = after. Clobbers A.
_snd_nvstep:
    push hl
    ld  a, l
    add a, #NO_T
    ld  l, a
    jr  nc, 1$
    inc h
1$:
    inc (hl)
    ld  a, l
    add a, #(NO_VACC - NO_T)
    ld  l, a
    jr  nc, 2$
    inc h
2$:
    ld  e, (hl)
    inc hl
    ld  d, (hl)
    inc hl
    ld  a, (hl+)
    ld  c, a
    ld  b, (hl)
    or  a, b
    jr  z, 6$
    ld  a, e
    add a, c
    ld  c, a
    ld  a, d
    adc a, b
    ld  b, a
    bit 7, b
    jr  z, 3$
    ld  bc, #0
    jr  5$
3$:
    ld  a, b
    cp  a, #7
    jr  c, 5$
    jr  nz, 4$
    ld  a, c
    or  a, a
    jr  z, 5$
4$:
    ld  bc, #0x0700
5$:
    dec hl
    dec hl
    ld  (hl), b
    dec hl
    ld  (hl), c
    pop hl
    ret
6$:
    ld  b, d
    ld  c, e
    pop hl
    ret

; BC = a volume (8.8, 0..7 << 8): A = BC * 34 >> 8 (mix_vol without an instrument)
_snd_mixn:
    ld  h, b
    ld  l, c
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, bc
    add hl, hl
    ld  a, h
    ret

; BC = outer volume, DE = instrument volume (8.8): A = (BC >> 4) * (DE >> 4) * 5 >> 8
_snd_mixc:
    ld  a, b
    swap a
    and a, #0xF0
    ld  h, a
    ld  a, c
    swap a
    and a, #0x0F
    or  a, h
    ld  h, a
    ld  a, d
    swap a
    and a, #0xF0
    ld  l, a
    ld  a, e
    swap a
    and a, #0x0F
    or  a, l
    ld  e, a
    ld  a, h
    call _snd_mul8
    ld  d, h
    ld  e, l
    add hl, hl
    add hl, hl
    add hl, de
    ld  a, h
    ret

; HL = A * E, A at most 15 (a gain or loudness). Clobbers A, B, D.
_snd_mul4:
    ld  hl, #0
    ld  d, h
    swap a
    ld  b, #4
1$:
    add hl, hl
    rla
    jr  nc, 2$
    add hl, de
2$:
    dec b
    jr  nz, 1$
    ret

; HL = A * E (unsigned). Clobbers A, B, D.
_snd_mul8:
    ld  hl, #0
    ld  d, h
    ld  b, #8
1$:
    add hl, hl
    rla
    jr  nc, 2$
    add hl, de
2$:
    dec b
    jr  nz, 1$
    ret
    __endasm;
}

// p in DE: pitch (op), level (ov), its change (odv) and waveform (ow) for this frame.
// Stack frame: 0-1 p, 2-3 pitch, 4 vol_comp[nv], 5 vol_comp[ov], 6 ov, 7 spare.
void snd_voice_render(voice_t *p) __naked {
    (void)p;
    __asm
    add sp, #-8
    ldhl sp, #0
    ld  (hl), e
    inc hl
    ld  (hl), d
    ld  h, d
    ld  l, e
    ld  bc, #VO_N
    add hl, bc
    call _snd_np            ; BC = the note s pitch
    ldhl sp, #2
    ld  (hl), c
    inc hl
    ld  (hl), b
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    inc hl
    inc hl
    ld  a, (hl)             ; cust
    or  a, a
    jp  z, 20$
    ld  bc, #(VO_N + NO_VOL - VO_CUST)
    add hl, bc
    ld  a, (hl)             ; n.vol
    or  a, a
    jp  z, 20$
    ; ---- with an instrument
    ld  bc, #(VO_IN - VO_N - NO_VOL)
    add hl, bc
    call _snd_np            ; BC = the instrument s pitch
    ldhl sp, #2
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    add hl, bc
    ld  bc, #-(24 * 16)     ; (instrument notes are relative to C2)
    add hl, bc
    ld  b, h
    ld  c, l
    ldhl sp, #2
    ld  (hl), c
    inc hl
    ld  (hl), b
    ldhl sp, #0             ; ow = in.wave
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  bc, #(VO_IN + NO_WAVE)
    add hl, bc
    ld  a, (hl)
    ld  bc, #(VO_OW - VO_IN - NO_WAVE)
    add hl, bc
    ld  (hl), a
    ldhl sp, #0             ; ov = mix(n.vacc, in.vacc)
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    ld  bc, #(VO_IN + NO_VACC)
    add hl, bc
    ld  e, (hl)
    inc hl
    ld  d, (hl)
    pop hl
    ld  bc, #(VO_N + NO_VACC)
    add hl, bc
    ld  c, (hl)
    inc hl
    ld  b, (hl)
    call _snd_mixc
    ldhl sp, #6
    ld  (hl), a
    ldhl sp, #0             ; the instrument s volume steps
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  de, #VO_IN
    add hl, de
    call _snd_nvstep        ; BC = in.vacc after
    ldhl sp, #4
    ld  (hl), c
    inc hl
    ld  (hl), b
    ldhl sp, #0             ; the note s
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  de, #VO_N
    add hl, de
    call _snd_nvstep        ; BC = n.vacc after
    ldhl sp, #4
    ld  a, (hl+)
    ld  d, (hl)
    ld  e, a
    call _snd_mixc          ; A = nv = mix(n.vacc after, in.vacc after)
    jr  30$
20$:
    ; ---- without
    ldhl sp, #0             ; ow = n.wave
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    ld  bc, #(VO_N + NO_WAVE)
    add hl, bc
    ld  a, (hl)
    pop hl
    push hl
    ld  bc, #VO_OW
    add hl, bc
    ld  (hl), a
    pop hl
    ld  bc, #VO_N
    add hl, bc
    call _snd_nvstep        ; DE = n.vacc before, BC after
    push bc
    ld  b, d
    ld  c, e
    call _snd_mixn          ; ov from the volume before
    pop bc
    ldhl sp, #6
    ld  (hl), a
    call _snd_mixn          ; A = nv, from the volume after
30$:
    ; A = nv. Perceptual compression, then the waveform s loudness:
    ; ov = vol_comp[ov >> 2] * w >> 4, odv = (int8)((vol_comp[nv >> 2] - that) * w >> 4)
    rra
    rra
    and a, #0x3F
    ld  e, a
    ld  d, #0
    ld  hl, #_vol_comp
    add hl, de
    ld  a, (hl)
    ldhl sp, #4
    ld  (hl+), a            ; [4] b8
    inc hl
    ld  a, (hl)             ; [6] ov
    rra
    rra
    and a, #0x3F
    ld  e, a
    ld  d, #0
    ld  hl, #_vol_comp
    add hl, de
    ld  a, (hl)
    ldhl sp, #5
    ld  (hl), a             ; [5] a8
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  bc, #VO_OW
    add hl, bc
    ld  c, (hl)
    ld  b, #0
    ld  hl, #_wloud
    add hl, bc
    ld  a, (hl)             ; w
    ldhl sp, #7
    ld  (hl-), a            ; [7] w, for odv
    dec hl
    ld  e, (hl)             ; E = a8
    call _snd_mul4          ; HL = w * a8
    ld  a, l
    srl h
    rra
    srl h
    rra
    srl h
    rra
    srl h
    rra
    ldhl sp, #6
    ld  (hl), a             ; [6] ov, final
    ldhl sp, #4
    ld  a, (hl+)            ; b8
    sub a, (hl)             ; - a8 (carry: negative)
    push af
    ldhl sp, #9             ; [7] w (the push)
    ld  b, (hl)
    pop af
    jr  nc, 31$
    cpl
    inc a
    ld  e, a
    ld  a, b
    call _snd_mul4          ; HL = w * |b8 - a8|
    xor a, a
    sub a, l
    ld  l, a
    ld  a, #0
    sbc a, h
    ld  h, a                ; HL = -HL
    jr  32$
31$:
    ld  e, a
    ld  a, b
    call _snd_mul4
32$:
    sra h                   ; >> 4, arithmetic
    rr  l
    sra h
    rr  l
    sra h
    rr  l
    sra h
    rr  l
    ld  a, l
    ldhl sp, #7
    ld  (hl), a             ; [7] odv
    ; op = max(pitch, 0), then ov, odv
    ldhl sp, #2
    ld  a, (hl+)
    ld  c, a
    ld  b, (hl)
    bit 7, b
    jr  z, 33$
    ld  bc, #0
33$:
    ldhl sp, #6
    ld  a, (hl+)
    ld  d, a                ; D = ov
    ld  e, (hl)             ; E = odv
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push de
    ld  de, #VO_OP
    add hl, de
    pop de
    ld  (hl), c
    inc hl
    ld  (hl), b
    inc hl
    ld  (hl), d             ; ov
    inc hl
    ld  (hl), e             ; odv
    add sp, #8
    ret
    __endasm;
}

// ---- the GB channels, in assembly: which voice each one plays, and its registers.
// envp[a]: env_reg's frames per level for a level change of a a frame, (17 + a / 2) / a,
// 0 when over 7 and at least 1 (a table, not a division)
const uint8_t envp[129] = {
    0, 0, 0, 6, 4, 3, 3, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

// The channel outputs after the voices' frames. Each output routine: HL = p, E = channel,
// B = trig.
void snd_outputs(void) __naked {
    __asm
    ; tonal (B) and noisy (C) voices, bit v: on, with a level or a note volume
    ld  hl, #_V
    ld  bc, #0
    ld  e, #1
1$:
    ld  a, (hl)             ; on
    or  a, a
    jr  z, 4$
    push hl
    ld  a, l
    add a, #VO_OV
    ld  l, a
    jr  nc, 2$
    inc h
2$:
    ld  a, (hl+)            ; ov
    inc hl
    ld  d, (hl)             ; ow
    pop hl
    or  a, a
    jr  nz, 3$
    push hl
    ld  a, l
    add a, #(VO_N + NO_VOL)
    ld  l, a
    jr  nc, 5$
    inc h
5$:
    ld  a, (hl)             ; n.vol
    pop hl
    or  a, a
    jr  z, 4$
3$:
    ld  a, d
    cp  a, #6
    jr  z, 6$
    ld  a, b
    or  a, e
    ld  b, a
    jr  4$
6$:
    ld  a, c
    or  a, e
    ld  c, a
4$:
    ld  a, l
    add a, #79
    ld  l, a
    jr  nc, 7$
    inc h
7$:
    sla e
    bit 6, e
    jr  z, 1$
    ; pulse 1: sfx 4 > music 2
    ld  d, #4
    bit 4, b
    jr  nz, 10$
    ld  d, #2
    bit 2, b
    jr  nz, 10$
    ld  d, #255
10$:
    ld  e, #0
    push bc
    call _snd_assign
    pop bc
    ; pulse 2: sfx 5 > music 3 > music 1
    ld  d, #5
    bit 5, b
    jr  nz, 11$
    ld  d, #3
    bit 3, b
    jr  nz, 11$
    ld  d, #1
    bit 1, b
    jr  nz, 11$
    ld  d, #255
11$:
    ld  e, #1
    push bc
    call _snd_assign
    pop bc
    ; wave: music 0
    ld  d, #0
    bit 0, b
    jr  nz, 12$
    ld  d, #255
12$:
    ld  e, #2
    push bc
    call _snd_assign
    pop bc
    ; noise: 4 > 5 > 1 > 3 > 2 > 0
    ld  d, #4
    bit 4, c
    jr  nz, 13$
    ld  d, #5
    bit 5, c
    jr  nz, 13$
    ld  d, #1
    bit 1, c
    jr  nz, 13$
    ld  d, #3
    bit 3, c
    jr  nz, 13$
    ld  d, #2
    bit 2, c
    jr  nz, 13$
    ld  d, #0
    bit 0, c
    jr  nz, 13$
    ld  d, #255
13$:
    ld  e, #3
    jp  _snd_assign

; HL += E, HL += 2E (A clobbered)
_snd_ix1:
    ld  a, l
    add a, e
    ld  l, a
    ret nc
    inc h
    ret
_snd_ix2:
    ld  a, e
    add a, a
    add a, l
    ld  l, a
    ret nc
    inc h
    ret

; E = channel, D = voice (255: none): trig = newnote, or a new owner; then its output
_snd_assign:
    ld  a, d
    inc a
    jp  z, _snd_silence
    ld  hl, #_V
    ld  a, d
    or  a, a
    jr  z, 2$
    ld  bc, #79
1$:
    add hl, bc
    dec a
    jr  nz, 1$
2$:
    push hl
    ld  bc, #VO_NEWNOTE
    add hl, bc
    ld  b, (hl)             ; B = newnote
    ld  hl, #_chown
    call _snd_ix1
    ld  a, (hl)
    ld  (hl), d             ; chown[ch] = v
    cp  a, d
    jr  z, 3$
    ld  b, #1
3$:
    pop hl
    ld  a, e
    cp  a, #2
    jp  z, _snd_wave_out
    cp  a, #3
    jp  z, _snd_noise_out
    jp  _snd_pulse_out

; E = channel: silent, the DAC left on (switching DACs off and on pops)
_snd_silence:
    ld  hl, #_chown
    call _snd_ix1
    ld  a, (hl)
    inc a
    ret z
    ld  (hl), #255
    ld  a, e
    or  a, a
    jr  nz, 2$
    ld  a, #0x08
    ldh (_NR12_REG + 0), a
    ld  a, #0x80
    ldh (_NR14_REG + 0), a
    jr  5$
2$:
    dec a
    jr  nz, 3$
    ld  a, #0x08
    ldh (_NR22_REG + 0), a
    ld  a, #0x80
    ldh (_NR24_REG + 0), a
    jr  5$
3$:
    dec a
    jr  nz, 4$
    xor a, a
    ldh (_NR32_REG + 0), a
    ld  (_wave_level), a
    jr  5$
4$:
    ld  a, #0x08
    ldh (_NR42_REG + 0), a
    ld  a, #0x80
    ldh (_NR44_REG + 0), a
5$:
    ld  hl, #_hwvol
    call _snd_ix1
    ld  (hl), #0
    ld  hl, #_hwstep
    call _snd_ix1
    ld  (hl), #0
    ret

; HL = p, A = gain: snd_gv = ov * g >> 4, snd_gdv = (int8)(odv * g >> 4). Keeps HL, DE.
_snd_gain:
    push de
    push hl
    ld  bc, #VO_OV
    add hl, bc
    ld  e, (hl)             ; E = ov
    inc hl
    ld  c, (hl)             ; C = odv
    ld  b, a                ; B = g
    push bc
    call _snd_mul4          ; HL = g * ov
    ld  a, l
    srl h
    rra
    srl h
    rra
    srl h
    rra
    srl h
    rra
    ld  (_snd_gv), a
    pop bc
    ld  a, c
    bit 7, a
    jr  z, 1$
    cpl
    inc a
    ld  e, a
    ld  a, b
    call _snd_mul4
    xor a, a
    sub a, l
    ld  l, a
    ld  a, #0
    sbc a, h
    ld  h, a
    jr  2$
1$:
    ld  e, a
    ld  a, b
    call _snd_mul4
2$:
    sra h
    rr  l
    sra h
    rr  l
    sra h
    rr  l
    sra h
    rr  l
    ld  a, l
    ld  (_snd_gdv), a
    pop hl
    pop de
    ret

; E = channel: hwvol += hwstep, kept to 0..240. Keeps E.
_snd_hwtick:
    ld  hl, #_hwstep
    call _snd_ix1
    ld  c, (hl)
    ld  hl, #_hwvol
    call _snd_ix1
    ld  a, (hl)
    bit 7, c
    jr  nz, 2$
    add a, c
    jr  c, 1$
    cp  a, #241
    jr  c, 3$
1$:
    ld  a, #240
    jr  3$
2$:
    add a, c
    jr  c, 3$
    xor a, a
3$:
    ld  (hl), a
    ret

; E = channel, A = level v: A nonzero when the simulated hardware level has drifted from v
; (silent against audible, or more than 40 apart). Keeps E.
_snd_drift:
    ld  c, a
    ld  hl, #_hwvol
    call _snd_ix1
    ld  b, (hl)
    ld  a, b
    cp  a, #8
    sbc a, a
    ld  d, a
    ld  a, c
    cp  a, #8
    sbc a, a
    xor a, d
    ret nz
    ld  a, b
    sub a, c
    jr  nc, 1$
    cpl
    inc a
1$:
    cp  a, #41
    ld  a, #0
    ret c
    inc a
    ret

; E = channel, from snd_gv, snd_gdv: A = the envelope register (hwvol, hwstep follow it). Keeps E.
_snd_env:
    ld  a, (_snd_gv)
    add a, #8
    rra
    srl a
    srl a
    srl a                   ; lvl = (v + 8) >> 4
    cp  a, #16
    jr  c, 1$
    ld  a, #15
1$:
    ld  b, a                ; B = lvl
    ld  a, (_snd_gdv)
    bit 7, a
    jr  z, 2$
    cpl
    inc a
2$:
    ld  hl, #_envp
    add a, l
    ld  l, a
    jr  nc, 3$
    inc h
3$:
    ld  c, (hl)             ; C = p
    ld  hl, #_hwvol
    call _snd_ix1
    ld  a, b
    swap a
    ld  (hl), a             ; hwvol = lvl << 4
    xor a, a
    or  a, c
    jr  z, 4$
    push de
    ld  e, c
    ld  hl, #_envstep
    call _snd_ix1
    ld  d, (hl)
    ld  a, (_snd_gdv)
    bit 7, a
    ld  a, d
    pop de
    jr  z, 4$
    cpl
    inc a
4$:
    ld  hl, #_hwstep
    push af
    call _snd_ix1
    pop af
    ld  (hl), a             ; hwstep (0 when p is)
    ld  a, b
    or  a, a
    jr  nz, 5$
    ld  a, #0x08            ; silent, the DAC kept on
    ret
5$:
    swap a
    or  a, c
    ld  b, a                ; (lvl << 4) | p
    ld  a, c
    or  a, a
    jr  z, 6$
    ld  a, (_snd_gdv)
    bit 7, a
    jr  nz, 6$
    or  a, a
    jr  z, 6$
    set 3, b                ; rising
6$:
    ld  a, b
    ret

; HL = p16 (pitch * 16, >= 0), A = 12 for an octave up: BC = the GB frequency. Clobbers DE.
_snd_freq:
    ld  d, a
    ld  a, l
    and a, #15
    ld  (_snd_ff), a        ; f
    ld  e, a
    srl h
    rr  l
    srl h
    rr  l
    srl h
    rr  l
    srl h
    rr  l
    ld  a, l
    add a, #12
    add a, d                ; i, in 8 bits as freq_of
    cp  a, #107
    jr  c, 1$
    ld  a, #107
    ld  e, #0
1$:
    ld  l, a
    ld  h, #0
    add hl, hl
    ld  bc, #_pitch_tab
    add hl, bc
    ld  a, (hl+)
    ld  c, a
    ld  a, (hl+)
    ld  b, a                ; BC = pitch_tab[i]
    ld  a, e
    or  a, a
    ret z
    ld  a, (hl+)
    sub a, c
    ld  e, a
    ld  a, (hl)
    sbc a, b
    ld  d, a                ; DE = pitch_tab[i + 1] - pitch_tab[i]
    push bc
    ld  hl, #0
    ld  a, (_snd_ff)        ; f
    swap a
    ld  b, #4
2$:
    add hl, hl
    add a, a
    jr  nc, 3$
    add hl, de
3$:
    dec b
    jr  nz, 2$
    srl h
    rr  l
    srl h
    rr  l
    srl h
    rr  l
    srl h
    rr  l
    pop bc
    add hl, bc
    ld  b, h
    ld  c, l
    ret
    __endasm;
}

// The pulse channels. Frame: 0-1 p, 2 ch, 3 trig, 4-5 fr, 6 w.
void snd_pulse(void) __naked {
    __asm
_snd_pulse_out:
    add sp, #-8
    ld  c, l
    ld  a, h
    ldhl sp, #1
    ld  (hl-), a
    ld  (hl), c
    ldhl sp, #2
    ld  (hl), e
    inc hl
    ld  (hl), b
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    ld  bc, #VO_OW
    add hl, bc
    ld  a, (hl)
    ldhl sp, #8             ; [6] (the push moved sp)
    ld  (hl), a
    pop hl
    ld  a, #GAIN_PULSE
    call _snd_gain          ; keeps HL = p
    ld  bc, #VO_OP
    add hl, bc
    ld  c, (hl)
    inc hl
    ld  b, (hl)             ; BC = op
    ldhl sp, #2
    ld  e, (hl)             ; E = ch
    inc hl
    ld  a, (hl)             ; trig
    or  a, a
    jr  nz, 2$
    ld  hl, #_chop
    call _snd_ix2
    ld  a, (hl+)
    cp  a, c
    jr  nz, 2$
    ld  a, (hl)
    cp  a, b
    jr  nz, 2$
    ld  hl, #_chfreq        ; the same pitch: the same frequency
    call _snd_ix2
    ld  a, (hl+)
    ld  c, a
    ld  b, (hl)
    jr  3$
2$:
    ld  hl, #_chop
    call _snd_ix2
    ld  (hl), c
    inc hl
    ld  (hl), b
    ld  h, b
    ld  l, c
    xor a, a
    call _snd_freq
3$:
    ldhl sp, #4
    ld  (hl), c
    inc hl
    ld  (hl), b
    ldhl sp, #2
    ld  e, (hl)
    call _snd_hwtick
    ; retrigger on trig, a duty change or a drifted level
    ldhl sp, #3
    ld  a, (hl)
    or  a, a
    jr  nz, 5$
    ld  hl, #_chwave
    call _snd_ix1
    ld  c, (hl)
    ld  b, #0
    ld  hl, #_duty_of
    add hl, bc
    ld  d, (hl)
    ldhl sp, #6
    ld  c, (hl)
    ld  hl, #_duty_of
    add hl, bc
    ld  a, (hl)
    cp  a, d
    jr  nz, 5$
    ld  a, (_snd_gv)
    call _snd_drift
    or  a, a
    jr  nz, 5$
    ; the frequency alone, when it changed
    ldhl sp, #4
    ld  c, (hl)
    inc hl
    ld  b, (hl)
    ld  hl, #_chfreq
    call _snd_ix2
    ld  a, (hl+)
    cp  a, c
    jr  nz, 4$
    ld  a, (hl)
    cp  a, b
    jr  z, 9$
4$:
    ld  a, e
    or  a, a
    jr  nz, 41$
    ld  a, c
    ldh (_NR13_REG + 0), a
    ld  a, b
    ldh (_NR14_REG + 0), a
    jr  9$
41$:
    ld  a, c
    ldh (_NR23_REG + 0), a
    ld  a, b
    ldh (_NR24_REG + 0), a
    jr  9$
5$:
    call _snd_env
    ld  d, a                ; D = envelope
    ldhl sp, #6
    ld  c, (hl)
    ld  b, #0
    ld  hl, #_duty_of
    add hl, bc
    ld  a, (hl)
    rrca
    rrca                    ; duty << 6
    ld  b, a
    ldhl sp, #4
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a                ; HL = fr
    ld  a, e
    or  a, a
    jr  nz, 6$
    xor a, a
    ldh (_NR10_REG + 0), a
    ld  a, b
    ldh (_NR11_REG + 0), a
    ld  a, d
    ldh (_NR12_REG + 0), a
    ld  a, l
    ldh (_NR13_REG + 0), a
    ld  a, h
    or  a, #0x80
    ldh (_NR14_REG + 0), a
    jr  7$
6$:
    ld  a, b
    ldh (_NR21_REG + 0), a
    ld  a, d
    ldh (_NR22_REG + 0), a
    ld  a, l
    ldh (_NR23_REG + 0), a
    ld  a, h
    or  a, #0x80
    ldh (_NR24_REG + 0), a
7$:
    ldhl sp, #6
    ld  c, (hl)
    ld  hl, #_chwave
    call _snd_ix1
    ld  (hl), c             ; chwave[ch] = w
9$:
    ldhl sp, #4
    ld  c, (hl)
    inc hl
    ld  b, (hl)
    ld  hl, #_chfreq
    call _snd_ix2
    ld  (hl), c
    inc hl
    ld  (hl), b
    add sp, #8
    ret

; the noise channel. Frame: 0-1 p, 2 trig, 3 nr.
_snd_noise_out:
    add sp, #-4
    ld  c, l
    ld  a, h
    ldhl sp, #1
    ld  (hl-), a
    ld  (hl), c
    ldhl sp, #2
    ld  (hl), b
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    ld  bc, #VO_OP
    add hl, bc
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    srl h
    rr  l
    srl h
    rr  l
    srl h
    rr  l
    srl h
    rr  l
    ld  a, l
    and a, #63
    ld  c, a
    ld  b, #0
    ld  hl, #_noise_tab
    add hl, bc
    ld  a, (hl)
    ldhl sp, #5             ; [3] (the push moved sp)
    ld  (hl), a
    pop hl
    ld  a, #GAIN_NOISE
    call _snd_gain
    ld  e, #3
    call _snd_hwtick
    ldhl sp, #2
    ld  a, (hl+)            ; trig
    ld  c, (hl)             ; nr
    or  a, a
    jr  nz, 2$
    ld  hl, #(_chfreq + 6)
    ld  a, (hl+)
    cp  a, c
    jr  nz, 2$
    ld  a, (hl)
    or  a, a
    jr  nz, 2$
    ld  a, (_snd_gv)
    call _snd_drift
    or  a, a
    jr  z, 3$
2$:
    call _snd_env
    ldh (_NR42_REG + 0), a
    ldhl sp, #3
    ld  a, (hl)
    ldh (_NR43_REG + 0), a
    ld  (_chfreq + 6), a
    xor a, a
    ld  (_chfreq + 7), a
    ld  a, #0x80
    ldh (_NR44_REG + 0), a
3$:
    add sp, #4
    ret

; the wave channel (the bass): 4 output levels, changed without a retrigger, with hysteresis
; and held 4 frames (each change shifts its DC offset: a click). Frame: 0-1 p, 2 trig,
; 3 level, 4-5 fr.
_snd_wave_out:
    add sp, #-6
    ld  c, l
    ld  a, h
    ldhl sp, #1
    ld  (hl-), a
    ld  (hl), c
    ldhl sp, #2
    ld  (hl), b
    ld  bc, #VO_OV
    ld  a, (hl-)
    ld  a, (hl-)
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    add hl, bc
    ld  c, (hl)             ; C = v
    ld  a, (_wave_level)
    ldhl sp, #3
    ld  (hl), a             ; level = wave_level
    ld  a, (_wave_hold)
    or  a, a
    jr  z, 1$
    dec a
    ld  (_wave_hold), a
    jr  8$
1$:
    ld  a, (_wave_level)
    ld  b, a                ; B = want
    ld  a, c
    cp  a, #16
    jr  c, 2$
    xor a, a
    ld  (_wave_quiet), a
2$:
    ld  a, b
    or  a, a
    jr  z, 3$
    inc a
    jr  nz, 4$
3$:                         ; off or unset: on above 40, loud above 120
    ld  a, c
    cp  a, #41
    jr  c, 7$
    ld  b, #3
    cp  a, #121
    jr  c, 7$
    ld  b, #2
    jr  7$
4$:
    ld  a, c
    cp  a, #16
    jr  nc, 5$
    ld  a, (_wave_quiet)    ; quiet: low level through short gaps, off after 12 frames
    inc a
    ld  (_wave_quiet), a
    ld  b, #3
    cp  a, #13
    jr  c, 7$
    ld  b, #0
    jr  7$
5$:
    ld  a, (_wave_level)
    cp  a, #3
    jr  nz, 6$
    ld  a, c
    cp  a, #141
    jr  c, 7$
    ld  b, #2
    jr  7$
6$:
    cp  a, #2
    jr  nz, 7$
    ld  a, c
    cp  a, #90
    jr  nc, 7$
    ld  b, #3
7$:
    ld  a, (_wave_level)
    cp  a, b
    jr  z, 8$
    ldhl sp, #3
    ld  (hl), b
    ld  a, #4
    ld  (_wave_hold), a
8$:
    ldhl sp, #0
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  bc, #VO_OP
    add hl, bc
    ld  c, (hl)
    inc hl
    ld  b, (hl)             ; BC = op
    ldhl sp, #2
    ld  a, (hl)
    or  a, a
    jr  nz, 9$
    ld  hl, #(_chop + 4)
    ld  a, (hl+)
    cp  a, c
    jr  nz, 9$
    ld  a, (hl)
    cp  a, b
    jr  nz, 9$
    ld  hl, #(_chfreq + 4)
    ld  a, (hl+)
    ld  c, a
    ld  b, (hl)
    jr  10$
9$:
    ld  hl, #(_chop + 4)
    ld  (hl), c
    inc hl
    ld  (hl), b
    ld  h, b
    ld  l, c
    ld  a, #12
    call _snd_freq
10$:
    ldhl sp, #4
    ld  (hl), c
    inc hl
    ld  (hl), b
    ld  a, (_wave_loaded)   ; one fixed waveform, loaded once (a rewrite needs the DAC off: a pop)
    inc a
    jr  nz, 12$
    xor a, a
    ldh (_NR30_REG + 0), a
    ld  hl, #_waves
    ld  de, #_AUD3WAVE
    ld  b, #16
11$:
    ld  a, (hl+)
    ld  (de), a
    inc de
    dec b
    jr  nz, 11$
    ld  a, #0x80
    ldh (_NR30_REG + 0), a
    xor a, a
    ld  (_wave_loaded), a
12$:
    ldhl sp, #3
    ld  b, (hl)
    ld  a, (_wave_level)
    cp  a, b
    jr  z, 13$
    ld  a, b
    swap a
    add a, a                ; level << 5
    ldh (_NR32_REG + 0), a
    ld  a, b
    ld  (_wave_level), a
13$:
    ldhl sp, #4
    ld  c, (hl)
    inc hl
    ld  b, (hl)
    ld  a, (_wave_loaded)   ; only the first start triggers (a trigger clicks)
    or  a, a
    jr  nz, 14$
    ld  a, c
    ldh (_NR33_REG + 0), a
    ld  a, b
    or  a, #0x80
    ldh (_NR34_REG + 0), a
    ld  a, #1
    ld  (_wave_loaded), a
    jr  15$
14$:
    ld  hl, #(_chfreq + 4)
    ld  a, (hl+)
    cp  a, c
    jr  nz, 16$
    ld  a, (hl)
    cp  a, b
    jr  z, 15$
16$:
    ld  a, c
    ldh (_NR33_REG + 0), a
    ld  a, b
    ldh (_NR34_REG + 0), a
15$:
    ld  hl, #(_chfreq + 4)
    ld  (hl), c
    inc hl
    ld  (hl), b
    add sp, #6
    ret
    __endasm;
}

static void ch_silence(uint8_t ch) {
    if (chown[ch] == 255) return;
    chown[ch] = 255;
    switch (ch) {
    // silence with the DAC left on (switching DACs off/on pops)
    case 0: NR12_REG = 0x08; NR14_REG = 0x80; break;
    case 1: NR22_REG = 0x08; NR24_REG = 0x80; break;
    case 2: NR32_REG = 0; wave_level = 0; break;
    case 3: NR42_REG = 0x08; NR44_REG = 0x80; break;
    }
    hwvol[ch] = 0;
    hwstep[ch] = 0;
}

void snd_update(void) {
    uint8_t v;
    voice_t *p = V;
    for (v = 0; v < NV; v++, p++)
        if (p->on) {
            vs_vi = v;
            voice_frame(p);
        }
    snd_outputs();
}

void snd_init(void) BANKED {
    uint8_t i;
    NR52_REG = 0x80;
#ifndef MASTER_VOL
#define MASTER_VOL 5   // 0..7 per side; 7 = maximum, leaves no headroom
#endif
    NR50_REG = (MASTER_VOL << 4) | MASTER_VOL;
    NR51_REG = 0xFF;
    for (i = 0; i < NV; i++) V[i].on = 0;
    for (i = 0; i < 4; i++) chown[i] = 0, ch_silence(i);
}
