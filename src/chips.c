// The board's chips: hint tokens, comparison arrows, their physics (out of game.c, whose
// bank is full)
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "common.h"
#include "board.h"

static const uint8_t cell_x[9] = {5, 8, 11, 5, 8, 11, 5, 8, 11};   // (as game.c)
static const uint8_t cell_y[9] = {2, 2, 2, 6, 6, 6, 10, 10, 10};
#define CCX(i) ((int16_t)cell_x[i] * 8 + 12)
#define CCY(i) ((int16_t)cell_y[i] * 8 + 15)

// ------------------------------------------------------------------ chips
static chip_t *newchip(void) {
    uint8_t i;
    for (i = 0; i < MAXCH; i++)
        if (!chips[i].used) {
            memset(&chips[i], 0, sizeof(chip_t));
            chips[i].used = 1;
            if (i >= chip_hi) chip_hi = i + 1;
            return &chips[i];
        }
    return &chips[MAXCH - 1];
}

static void dochip(chip_t *c) {
    if (c->rest) return;
    if (c->delay) { c->delay--; return; }
    if (c->dx - c->x >= 1 || c->x - c->dx >= 1) c->x += c->dx > c->x ? 1 : -1;
    if (c->dy - c->y >= 1 || c->y - c->dy >= 1) c->y += c->dy > c->y ? 1 : -1;
    c->z += c->zd;
    c->zd -= 64;
    if (c->z <= c->zbase) {
        c->z = c->zbase;
        if (c->zd > 384 || c->zd < -384) {
            c->zd = -((c->zd >> 2) + (c->zd >> 5) + (c->zd >> 6));   // bounce: ~0.3x
            sfx(58);
        } else c->zd = 0;
    }
    if ((c->delme && c->y - (c->z >> 8) <= -10) || c->x <= -14) c->used = 0;
    else if (!c->zd && c->z == c->zbase && c->x == c->dx && c->y == c->dy && !c->delme) c->rest = 1;
}

void dochips(void) BANKED {
    chip_t *c = chips;
    uint8_t i;
    for (i = chip_hi; i; i--, c++)
        if (c->used && !c->rest) dochip(c);
}

void swipechips(uint8_t ci) BANKED {
    uint8_t i;
    uint16_t m = 1 << ci;
    for (i = 0; i < chip_hi; i++)
        if (chips[i].used && (chips[i].cardmask & m)) {
            chips[i].zd = 8 * 256;
            chips[i].delme = 1;
            chips[i].rest = 0;
            sfx(58);
        }
}

void dropchip1(uint8_t ci, uint8_t delay, int16_t z) BANKED {
    chip_t *c = newchip();
    uint8_t h = cards[ci].hint;
    if (h < 2) h = 2;
    c->x = c->dx = CCX(ci) - 7;   // the 13 px token centred on the 19 px card
    c->y = c->dy = CCY(ci) - 3;
    c->z = z;
    c->zd = z ? 0 : 512;
    c->tile = SPR_TOK2 + (h - 2) * 4;
    c->pal = SPRPAL_TOK2;
    c->w16 = 1;
    c->delay = delay;
    c->cardmask = 1 << ci;
    cards[ci].had1 = 1;
}

static uint8_t neighbor(uint8_t ci, uint8_t dir) {
    // 0 left, 1 right, 2 up, 3 down ; 255 = none
    uint8_t col = ci % 3, row = ci / 3;
    if (dir == 0) return col ? ci - 1 : 255;
    if (dir == 1) return col < 2 ? ci + 1 : 255;
    if (dir == 2) return row ? ci - 3 : 255;
    return row < 2 ? ci + 3 : 255;
}

uint8_t hasouts(uint8_t ci) BANKED {
    uint8_t d, n;
    for (d = 0; d < 4; d++) {
        n = neighbor(ci, d);
        if (n != 255 && cards[n].hidden) return 1;
    }
    return 0;
}

void dropchip3(uint8_t ci) BANKED {
    static const int8_t ox[4] = {-15, 8, -4, -4}, oy[4] = {-3, -3, -18, 11};
    static const uint8_t lowt[4] = {SPR_ARR_L, SPR_ARR_R, SPR_ARR_U, SPR_ARR_D};
    static const uint8_t hight[4] = {SPR_ARR_R, SPR_ARR_L, SPR_ARR_D, SPR_ARR_U};
    uint8_t d, n;
    cards[ci].had3 = 1;
    for (d = 0; d < 4; d++) {
        n = neighbor(ci, d);
        if (n != 255 && cards[n].hidden) {
            chip_t *c = newchip();
            c->x = CCX(ci) - 4;
            c->y = CCY(ci) - 3;
            c->dx = CCX(ci) + ox[d];
            c->dy = CCY(ci) + oy[d];
            c->zd = 512;
            c->tile = cards[n].val < cards[ci].val ? lowt[d] : hight[d];
            c->pal = SPRPAL_ARR_L;
            c->cardmask = (1 << ci) | (1 << n);
        }
    }
}

void dodroptoken(void) BANKED {
    uint8_t cand[9], nc = 0, i, k;
    for (i = 0; i < 9; i++)
        if (cards[i].hidden) cand[nc++] = i;
    for (i = 0; i < droptoken && nc; i++) {
        k = rnd(nc);
        // falls in from above the screen
        dropchip1(cand[k], i * 10, (CCY(cand[k]) - 3 + 12) * 256);
        cand[k] = cand[--nc];
    }
}

