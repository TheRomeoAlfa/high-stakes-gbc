#ifndef BOARD_H
#define BOARD_H
#include <gb/gb.h>
#include <stdint.h>

// shared between game.c (the board), score.c (the score panel) and deal.c (dealing)
typedef struct {
    uint8_t val, hidden, hint, had1, had3, badstab;
    uint8_t ani;     // 0 none, 1 flip, 2 deal
    uint8_t t, fr, delay;
    uint8_t lift;    // flip drawn as a hop: lift slot + 1 (0 = drawn with plain frames)
} card_t;
typedef struct {
    uint8_t used, tile, pal, w16, delay, delme;
    uint8_t rest;          // settled: skip physics until something moves it
    int16_t x, y, dx, dy;
    int16_t z, zd, zbase;  // 8.8
    uint16_t cardmask;
} chip_t;
#define MAXCH 24
extern card_t cards[9];
extern chip_t chips[MAXCH];
extern uint8_t chip_hi, vcard, autoflip, pflockard, bonus_dirty;
extern int16_t fx[9], fy[9], fz[9], fzd[9];   // dealing: card flight (x, y 12.4; z 8.8)
extern int16_t stabx, stabdx, staby, stabdy, pflockx, pflocky, pflockdest;
extern int8_t stabonus, dangrect;
extern uint8_t cur, showcur, curchip, curstab, gs[6];
extern uint8_t result;   // 0 none, 1 won, 2 lost, 3 pass
extern uint8_t helpi, help_drawn;
extern uint8_t pend_n, pend_i, pend_kind[5];
extern int16_t pend_val[5];
extern const char *pend_lbl[5];
void game_frame(void) BANKED;
void stake_ease(void) BANKED;   // deal.c
void pass_spr_render(void) BANKED;   // deal.c
void vram_read(uint8_t *dst, const uint8_t *src, uint16_t n) BANKED;   // deal.c, n % 4 == 0
void dochips(void) BANKED;
uint8_t score_phase(void) BANKED;
void score_prep(uint8_t t) BANKED;     // in the wait before score_phase   // 1 = next round, 0 = match over

#endif
