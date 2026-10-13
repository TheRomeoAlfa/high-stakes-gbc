// Fades as PICO-8's: a screen colour walks its row of the cart's fadetable (white through
// grays to dark purple, red through dark red...) instead of only darkening. Colours that are
// not screen colours are scaled by fade_tab (each component * k^2 / 625).
#pragma bank 255
#include <gb/gb.h>
#include "common.h"
#include "gen/fade_ramp.h"

static const uint16_t *fh_src, *fh_ramp;
static const uint8_t *fh_scale;
static uint16_t *fh_dst;
static uint8_t fh_n, fh_lo, fh_hi;

// 32 colours: a screen colour (found by fade_hash[lo ^ hi], checked against pico[]) becomes
// its fade_ramp entry; anything else has its components scaled by the fh_scale row
static void fh_run(void) __naked {
    __asm
    ld  hl, #_fh_src
    ld  e, (hl)
    inc hl
    ld  d, (hl)
    ld  hl, #_fh_dst
    ld  c, (hl)
    inc hl
    ld  b, (hl)
    ld  a, #32
    ld  (_fh_n), a
1$:
    ld  a, (de)
    inc de
    ld  (_fh_lo), a
    ld  l, a
    ld  a, (de)
    inc de
    ld  (_fh_hi), a
    xor a, l
    push de
    ld  e, a
    ld  d, #0
    ld  hl, #_fade_hash
    add hl, de
    ld  a, (hl)
    or  a, a
    jr  z, 9$
    dec a
    add a, a
    ld  e, a
    ld  hl, #_pico
    add hl, de
    ld  a, (_fh_lo)
    cp  a, (hl)
    jr  nz, 9$
    inc hl
    ld  a, (_fh_hi)
    cp  a, (hl)
    jr  nz, 9$
    ld  hl, #_fh_ramp
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    add hl, de
    ld  a, (hl+)
    ld  (bc), a
    inc bc
    ld  a, (hl)
    ld  (bc), a
    dec bc
8$:
    pop de
    inc bc
    inc bc
    ld  hl, #_fh_n
    dec (hl)
    jr  nz, 1$
    ret
9$:     ; not a screen colour: r, g, b each through the scale row
    push bc
    ld  hl, #_fh_scale
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    ld  d, #0
    ld  a, (_fh_lo)
    and a, #31
    ld  e, a
    add hl, de
    ld  c, (hl)             ; r
    ld  a, (_fh_hi)
    and a, #3
    add a, a
    add a, a
    add a, a
    ld  e, a
    ld  a, (_fh_lo)
    swap a
    rrca
    and a, #7
    or  a, e
    ld  e, a
    pop hl
    push hl
    add hl, de
    ld  b, (hl)             ; g
    ld  a, (_fh_hi)
    rrca
    rrca
    and a, #31
    ld  e, a
    pop hl
    add hl, de
    ld  a, (hl)             ; b
    add a, a
    add a, a
    ld  d, a
    ld  a, b
    rrca
    rrca
    rrca
    and a, #3
    or  a, d
    ld  d, a                ; hi: g >> 3 | b << 2
    ld  a, b
    swap a
    add a, a
    and a, #0xE0
    or  a, c
    ld  e, a                ; lo: r | g << 5
    pop bc
    ld  a, e
    ld  (bc), a
    inc bc
    ld  a, d
    ld  (bc), a
    dec bc
    jr  8$
    __endasm;
}

void fade_hue(const uint16_t *src, uint16_t *dst, uint8_t lvl) BANKED {
    fh_src = src;
    fh_dst = dst;
    fh_ramp = fade_ramp[lvl];
    fh_scale = fade_tab + (uint8_t)(25 - lvl) * 32;
    fh_run();
}
