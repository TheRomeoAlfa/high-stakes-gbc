// Dealing a round (dealround): shuffle, hints, slot tokens, and the cards' flight in.
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include <rand.h>
#include "common.h"
#include "board.h"

void dealround(void) BANKED {
    uint8_t vals[9], nv = 9, i, k, h, nt;
    uint8_t targs[9];
    bonusmode = 0;
    stabdx = 0;
    stabdy = 0;
    stabonus = 7;
    pflockx = 0;
    pflocky = pflockdest = -28;
    pflockard = 255;
    curchip = 0;
    curstab = 0;
    showcur = 0;
    dangrect = -1;
    helpi = 0;
    if (staby != 0) { stabx = -20 * 16; staby = 0; }
    musiclvl(0);
    for (i = 0; i < 9; i++) vals[i] = i + 2;
    for (i = 0; i < 9; i++) {
        k = rnd(nv);
        memset(&cards[i], 0, sizeof(card_t));
        cards[i].val = vals[k];
        vals[k] = vals[--nv];
        cards[i].hidden = 1;
        cards[i].delay = 10 * (i + 1);
        cards[i].fr = 254;
        cards[i].ani = 2;
        fx[i] = 54 << 4;    // the original's (54, -70) card centre (12.4)
        fy[i] = -93 << 4;
        fz[i] = 10 << 8;
        fzd[i] = 0;
        if (cards[i].val == 10) vcard = i;
    }
    do autoflip = rnd(9); while (cards[autoflip].val == 10);
    cards[autoflip].hidden = 0;
    cur = autoflip + 1;
    for (h = 9; h >= 2; h--) {
        if (vampval == 13 && h == cards[autoflip].val) continue;  // asshole mode
        nt = 0;
        for (i = 0; i < 9; i++)
            if (cards[i].hidden && cards[i].val >= h && cards[i].hint == 0 && !(vampval == 13 && i == vcard)) targs[nt++] = i;
        if (nt) cards[targs[rnd(nt)]].hint = h;
    }
    if (vampval == 13) cards[vcard].hint = cards[autoflip].val;
    cards[autoflip].hidden = 1;
    for (i = 0; i < 6; i++) gs[i] = rnd(2) ? 4 : 2;
    if (dropboxes) {
        if (vampval == 13) {
            uint8_t va = vcard % 3, vb = 3 + vcard / 3;
            if (rnd(5) < 3) gs[rnd(2) ? va : vb] = 10;
            else {
                do k = rnd(6); while (k == va || k == vb);
                gs[k] = 10;
            }
        } else gs[rnd(6)] = 10;
    }
    memset(chips, 0, sizeof(chips));
    chip_hi = 0;
    result = 0;
    stakes = 0;
    round_++;
    bonus_dirty = 1;
}

void bonusround(void) BANKED {
    dealround();
    bonusmode = 1;
    stakes = wining;
    stabdx = -30 * 16;
}


// stake motion, eased with shifts instead of 16-bit divisions (~1/10 and 1/4 per frame)
void stake_ease(void) BANKED {
    if (stabx != stabdx) {
        int16_t d = stabdx - stabx;
        int16_t s = (d >> 3) - (d >> 5);
        stabx += s ? s : (d > 0 ? 1 : -1);
    }
    if (staby != stabdy) {
        int16_t d = stabdy - staby;
        int16_t s = d >> 2;
        staby += s ? s : (d > 0 ? 1 : -1);
    }
}

void startmatch(void) BANKED {
    musiclvl(0);
    music(1);
    round_ = 0;
    wining = wining_txt = 0;
    losses_desc = 0;
    strcpy(wins, ".....");
    stakes_txt = 0;
    stabx = -20 * 16;
    staby = 0;
    dealround();
}

// Reads n (a multiple of 4) bytes of VRAM into RAM while the screen is on: four at a time
// once STAT shows mode 0 or 1. The mode after those is 2, when VRAM is still free, so the
// four reads land in time with interrupts held off only for them (one byte per wait, in C,
// took over 3 frames for the board swap's map rows).
static uint8_t *vr_dst;
static const uint8_t *vr_src;
static uint16_t vr_n;
static void vr_copy(void) __naked {
    __asm
    ld  hl, #_vr_n
    ld  a, (hl+)
    ld  b, (hl)
    srl b
    rra
    srl b
    rra
    ld  c, a                ; bc = n / 4
    ld  hl, #_vr_dst
    ld  a, (hl+)
    ld  e, a
    ld  d, (hl)
    ld  hl, #_vr_src
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
1$:
    di
2$:
    ldh a, (_STAT_REG + 0)
    and a, #2
    jr  z, 3$
    ei
    nop
    di
    jr  2$
3$:
    ld  a, (hl+)
    ld  (de), a
    inc de
    ld  a, (hl+)
    ld  (de), a
    inc de
    ld  a, (hl+)
    ld  (de), a
    inc de
    ld  a, (hl+)
    ld  (de), a
    inc de
    ei
    dec bc
    ld  a, b
    or  a, c
    jr  nz, 1$
    ret
    __endasm;
}

void vram_read(uint8_t *dst, const uint8_t *src, uint16_t n) BANKED {
    vr_dst = dst;
    vr_src = src;
    vr_n = n;
    vr_copy();
}

// the highlighted "pass": OBJ tiles PASS_TILE.. (game.c)
#define PASS_TILE 124
void pass_spr_render(void) BANKED {   // (game.c: its bank is full)
    static const canvas_t cv = {cvbuf, 0, 0, 0, 0, 2, 2, 0, 0};
    uint8_t c, *b = (uint8_t *)(((uint16_t)cvbuf + 15) & ~15u) + 2048 + 128;   // past the stake box's
    cv_begin(&cv, 0);
    cv_print("pass", 0, 0, 1);
    for (c = 0; c < 2; c++) {   // canvas tiles -> 8x16 pairs
        memcpy(b + c * 32, cvbuf + c * 16, 16);
        memcpy(b + c * 32 + 16, cvbuf + (2 + c) * 16, 16);
    }
    vram_dma_vbl(b, (uint8_t *)0x8000u + (PASS_TILE << 4), 0, 4, _current_bank);
}
