// Card hop / flip, like the original's: the card hops up (zd=1.4, gravity 0.15) while it turns
// over, drawn column by column at an angle, above a drop shadow.
//
// Every frame is pre-rendered (gen_assets.py lift_frames) into a 3x5 tile region: the card's
// 3x4 cell plus the tile row above it, where the lifted card is blended over whatever is
// there (the card above, or a top slot). Over a top slot each tile keeps the palette of
// whichever side has more pixels and recolours the other side by nearest colour; over a card
// the lifted card's palette is used, since the card palettes share their edge colours.
//
// The cell tiles and the shadow sprites (obj palette 7, colour 3) go from ROM to VRAM by
// CGB DMA in VBlank. The overlap row is pre-blended as well for the common cases (a top
// slot, a card back); only over a face-up or hopping card does the CPU blend it.
// Two cards can hop at once.
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "common.h"
#include "lift.h"

// card i's cell in tiles (5 + i % 3 * 3, 2 + i / 3 * 4): tables, as SDCC makes i % 3 and
// i / 3 16-bit library divisions (the deal worked out every flying card's twice a frame)
static const uint8_t cell_x[9] = {5, 8, 11, 5, 8, 11, 5, 8, 11};
static const uint8_t cell_y[9] = {2, 2, 2, 6, 6, 6, 10, 10, 10};
#define CELLX(i) (cell_x[i])
#define CELLY(i) (cell_y[i])
#define LIFT_HDR 96   // record header + overlap tiles (see LIFT_REC in gen_assets.py)
#define TILE_ADDR(t) ((uint8_t *)0x8800u + ((uint16_t)((t) - 128) << 4))   // BG tiles >= 128

uint8_t lift_cfr[9];    // card frames at rest, as drawn by game.c
uint8_t lift_top[3];    // top slot kinds, as drawn by game.c
static uint8_t lslot[9], llt[9], lface[9];   // per card: slot + 1, flip frame, face value

typedef struct {
    uint8_t card;          // 255 = free
    uint8_t drawn;         // frame shown (255 = none)
    uint8_t shused;        // shadow sprites to draw (bit r*3+c)
    uint8_t mx, my, bankbit, sx, sy;   // region map position, attr bank bit, shadow position
    uint8_t *ov;           // blended overlap tiles (DMA source, 16-aligned)
    uint8_t ids[15], attrs[15];
    // the overlap row without the card, and recoloured into the card's palette:
    // both kept while it doesn't change (bkey 255 = reload every frame)
    uint8_t bkey, bpal[3], bt[48];
    uint8_t rkey[3], rt[48];
} lift_t;
static lift_t lifts[2];
static const uint8_t lift_tile0[2] = {195, 217};   // BG tiles: bank 0 195-209, bank 1 217-231
static uint8_t lift_lut[8][8];   // nearest colour, palette a -> b: 2 bits per source index
static uint8_t lift_hdr[18];   // record bytes 0-17: tile palettes, overlap pixel counts
static uint8_t ov_raw[2 * 48 + 15];                     // blended overlap tiles (DMA source)

void lift_luts(void) BANKED {   // (built by gen_assets.py: the palettes are constants)
    banked_copy(lift_lut, BANK(lift_lut_data), lift_lut_data, sizeof(lift_lut));
}

void lift_reset(void) BANKED {
    lifts[0].card = lifts[1].card = 255;
    memset(lslot, 0, sizeof(lslot));
}

// returns the slot + 1, or 0 if both are busy (the card then flips with plain frames)
uint8_t lift_begin(uint8_t ci) BANKED {
    uint8_t j, s = lifts[0].card == 255 ? 0 : lifts[1].card == 255 ? 1 : 255;
    lift_t *L;
    if (s == 255) return 0;
    L = &lifts[s];
    L->card = ci;
    L->drawn = 255;
    L->shused = 0;
    L->bkey = 255;
    for (j = 0; j < 15; j++) L->ids[j] = lift_tile0[s] + j;
    L->mx = CELLX(ci);
    L->my = CELLY(ci) - 1;
    L->sx = L->mx * 8;
    L->sy = (L->my + 1) * 8;
    L->bankbit = s ? 8 : 0;
    L->ov = (uint8_t *)(((uint16_t)ov_raw + 15) & ~15u) + s * 48;
    lslot[ci] = s + 1;
    llt[ci] = 0;
    return s + 1;
}

void lift_end(uint8_t slot1) BANKED {
    lift_t *L = &lifts[slot1 - 1];
    lslot[L->card] = 0;
    L->card = 255;
}

// what a lifted card overlaps has changed (a top slot redrawn): render it again
void lift_redraw(uint8_t ci) BANKED {
    uint8_t s = lslot[ci];
    if (s) {
        lifts[s - 1].drawn = 255;
        lifts[s - 1].bkey = 255;
    }
}

void lift_set(uint8_t ci, uint8_t t, uint8_t face) BANKED {
    llt[ci] = t;
    lface[ci] = face;
}

// ROM record for frame t of card ci: 0..20 its flip, 21-23 face up raised 1-3 px (a bounce),
// 24 face down raised 1 px (under the cursor); also its group and index in the group
static uint8_t rec_g, rec_i;
static const uint8_t *lift_rec(uint8_t ci, uint8_t t, uint8_t *bank) {
    uint8_t g = 0;
    if (t > 20) {
        uint8_t i = t == LIFT_FR_HOVER ? 0 : (uint8_t)((lface[ci] - 2) * 3 + t - 20);
        rec_g = 255;
        *bank = BANK(lift_z);
        return lift_z + ((uint16_t)i << 8) + ((uint16_t)i << 5);
    }
    if (t > 10) {   // 0-9 back, 10 blank, 11-20 face
        g = lface[ci] - 1;
        t -= 11;
    }
    rec_g = g;
    rec_i = t;
    *bank = lift_bank(g);
    return lift_ptr[g] + ((uint16_t)t << 8) + ((uint16_t)t << 5);   // t * 288
}

// ---- tile blending (asm): args in globals
static uint8_t *bl_t;
static uint8_t bl_f[8], bl_n;

// recolour tile bl_t: each pixel's index i becomes ((code >> 2i) & 3)
static void bl_remap(void) __naked {
    __asm
    ld  a, #8
    ld  (_bl_n), a
1$:
    ld  hl, #_bl_t
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  d, (hl)          ; p0
    inc hl
    ld  e, (hl)          ; p1
    ld  hl, #_bl_f
    ; index 0: ~p0 & ~p1
    ld  a, d
    or  a, e
    cpl
    and a, (hl)
    ld  b, a
    inc hl
    ld  a, d
    or  a, e
    cpl
    and a, (hl)
    ld  c, a
    inc hl
    ; index 1: p0 & ~p1
    ld  a, e
    cpl
    and a, d
    and a, (hl)
    or  a, b
    ld  b, a
    inc hl
    ld  a, e
    cpl
    and a, d
    and a, (hl)
    or  a, c
    ld  c, a
    inc hl
    ; index 2: ~p0 & p1
    ld  a, d
    cpl
    and a, e
    and a, (hl)
    or  a, b
    ld  b, a
    inc hl
    ld  a, d
    cpl
    and a, e
    and a, (hl)
    or  a, c
    ld  c, a
    inc hl
    ; index 3: p0 & p1
    ld  a, d
    and a, e
    and a, (hl)
    or  a, b
    ld  b, a
    inc hl
    ld  a, d
    and a, e
    and a, (hl)
    or  a, c
    ld  c, a
    ; store, advance
    ld  hl, #_bl_t
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  (hl), b
    inc hl
    ld  (hl), c
    inc hl
    ld  a, l
    ld  (_bl_t), a
    ld  a, h
    ld  (_bl_t + 1), a
    ld  hl, #_bl_n
    dec (hl)
    jr  nz, 1$
    ret
    __endasm;
}

static void tile_remap(uint8_t *t, uint8_t code) {
    uint8_t i;
    if (code == 0xE4) return;   // identity
    for (i = 0; i < 8; i += 2, code >>= 2) {
        bl_f[i] = (code & 1) ? 0xFF : 0;
        bl_f[i + 1] = (code & 2) ? 0xFF : 0;
    }
    bl_t = t;
    bl_remap();
}


// lift_base_data entry of each rest frame (back, faces 2..13), 255 = none
static const uint8_t base_of_fr[40] = {
    0, 255, 255, 255, 1, 255, 255, 2, 255, 255, 3, 255, 255, 4, 255, 255, 5, 255, 255, 6,
    255, 255, 7, 255, 255, 8, 255, 255, 9, 255, 255, 10, 255, 255, 11, 255, 255, 12, 255, 255};

// fills L->b* with the overlap row as it looks without its card
static void lift_get_base(lift_t *L) {
    uint8_t ci = L->card, idx, key, a, j, bank, f, *p;
    const uint8_t *rec;
    if (ci < 3) {
        idx = lift_top[ci];
        key = 0x80 | idx;
        idx += 13;
    } else {
        a = ci - 3;
        if (lslot[a]) {   // the card above is hopping too: its cell's bottom row
            rec = lift_rec(a, llt[a], &bank);
            banked_copy(L->bpal, bank, rec + 12, 3);
            banked_copy(L->bt, bank, rec + LIFT_HDR + 9 * 16, 48);
            L->rkey[0] = L->rkey[1] = L->rkey[2] = 255;
            L->bkey = 255;
            return;
        }
        key = f = lift_cfr[a];
        if (key == L->bkey) return;
        idx = f < 40 ? base_of_fr[f] : 255;
    }
    if (key == L->bkey) return;
    L->bkey = key;
    L->rkey[0] = L->rkey[1] = L->rkey[2] = 255;
    if (idx == 255) {
        memset(L->bt, 0, 48);
        L->bpal[0] = L->bpal[1] = L->bpal[2] = 1;
        return;
    }
    bank = lift_bank(255);
    rec = lift_base_data + (uint16_t)idx * 54;
    for (j = 0, p = L->bt; j < 3; j++, p += 16, rec += 18) {
        banked_copy(&L->bpal[j], bank, rec, 1);
        banked_copy(p, bank, rec + 2, 16);
    }
}

// does the card's overlap tile j show any colour 3 pixel?
static uint8_t lift_c3(const uint8_t *rec, uint8_t bank, uint8_t j) {
    uint8_t buf[24], r, *t = buf + 8;
    banked_copy(buf, bank, rec + 18 + j * 8, 8);
    banked_copy(t, bank, rec + 48 + j * 16, 16);
    for (r = 0; r < 8; r++, t += 2)
        if (buf[r] & t[0] & t[1]) return 1;
    return 0;
}

// blends the overlap row at run time (over a face-up or hopping card) into L->ov
static void lift_blend_rt(lift_t *L, const uint8_t *rec, uint8_t bank) {
    uint8_t j, bp, cp, cnt, *bt = L->bt, *rt = L->rt;
    lift_get_base(L);
    for (j = 0; j < 3; j++, bt += 16, rt += 16) {
        bp = L->bpal[j];
        lb_src[j] = bt;
        cnt = lift_hdr[15 + j];
        cp = lift_hdr[j];
        // the face and back palettes share colours 0-2: if the card shows none of its
        // colour 3 here, the base keeps its own palette (the back's pattern is colour 3)
        if (cnt && bp != cp && (bp | cp) == 3 && !lift_c3(rec, bank, j)) cnt = 0;
        if (cnt) {   // the base recoloured to the card's palette (cached): card palettes
            //             share their dark colours, so a card's edge loses nothing
            if (L->rkey[j] != cp) {
                L->rkey[j] = cp;
                memcpy(rt, bt, 16);
                tile_remap(rt, lift_lut[bp][cp]);
            }
            lb_src[j] = rt;
            bp = cp;
        }
        L->attrs[j] = bp | L->bankbit;
    }
    lb_bank = bank;
    lb_rec = rec;
    lb_out = L->ov;
    lift_blend();
}

static void lift_render(uint8_t s) {
    lift_t *L = &lifts[s];
    uint8_t ci = L->card, t = llt[ci], j, bank, v, bankbit = L->bankbit;
    uint8_t *dst = TILE_ADDR(lift_tile0[s]);
    const uint8_t *rec = lift_rec(ci, t, &bank);
    banked_copy(lift_hdr, bank, rec, 18);
    // overlap row: over a top slot or a card back it was blended at build time
    // (variants 0-6 slots, 7 back), otherwise it is blended here
    if (ci < 3) v = lift_top[ci];
    else v = (!lslot[ci - 3] && lift_cfr[ci - 3] == CF_BACK) ? 7 : 255;
    if (t > 20) v = 255;   // raised frames: no pre-blended overlaps
    if (v != 255) {
        uint8_t pb = liftp_bank(rec_g), pals[3];
        const uint8_t *pre = liftp_ptr[rec_g] + ((uint16_t)rec_i << 9) + ((uint16_t)v << 6);
        banked_copy(pals, pb, pre + 48, 3);
        for (j = 0; j < 3; j++) L->attrs[j] = pals[j] | bankbit;
        vram_dma_vbl(pre, dst, s, 3, pb);
    } else {
        lift_blend_rt(L, rec, bank);
        vram_dma_vbl(L->ov, dst, s, 3, bank);
    }
    for (j = 3; j < 15; j++) L->attrs[j] = lift_hdr[j] | bankbit;
    vram_dma_vbl(rec + LIFT_HDR, dst + 48, s, 12, bank);
    map_put_vbl(L->mx, L->my, 3, 5, L->ids, L->attrs);
    // shadow sprites; none under the cursor's 1 px lift (a sliver, and it would hold three
    // sprites on those lines for as long as the cursor stays, crowding the 10-a-line limit)
    if (t == LIFT_FR_HOVER) L->shused = 0;
    else {
        rec = lift_shadow + ((uint16_t)t << 8) - ((uint16_t)t << 5) - ((uint16_t)t << 4);   // t * 208
        bank = lift_bank(255);
        banked_copy(&L->shused, bank, rec, 1);
        vram_dma_vbl(rec + 16, (uint8_t *)0x8000u + (uint16_t)s * 12 * 16, 1, 12, bank);
    }
    L->drawn = t;
}

// top to bottom, so a card hopping into a lifted card above is drawn after it
void lift_update(void) BANKED {
    uint8_t i, s;
    lift_t *L;
    for (i = 0; i < 9; i++) {
        s = lslot[i];
        if (!s) continue;
        L = &lifts[s - 1];
        // if last frame's map block is still queued (no room in that VBlank), hold the
        // frame: new tiles under old palettes would show for a frame
        if (L->drawn != llt[i] && !map_vbl_pending(L->mx, L->my)) lift_render(s - 1);
    }
}

void lift_sprites(void) BANKED {
    uint8_t s, r, c, bit;
    lift_t *L = lifts;
    for (s = 0; s < 2; s++, L++) {
        if (L->card == 255 || !L->shused) continue;
        bit = 1;
        for (r = 0; r < 2; r++)
            for (c = 0; c < 3; c++, bit <<= 1)
                if (L->shused & bit)
                    spr_put(L->sx + c * 8, L->sy + r * 16,
                            s * 12 + (r * 3 + c) * 2, 7 | 0x08);
    }
}
