#ifndef BOARD_H
#define BOARD_H
#include <gb/gb.h>
#include <stdint.h>

// shared between game.c (the board) and score.c (the score panel)
extern uint8_t result;   // 0 none, 1 won, 2 lost, 3 pass
extern uint8_t helpi, help_drawn;
extern uint8_t pend_n, pend_i, pend_kind[5];
extern int16_t pend_val[5];
extern const char *pend_lbl[5];
void game_frame(void) BANKED;
void dochips(void) BANKED;
uint8_t score_phase(void) BANKED;   // 1 = next round, 0 = match over

#endif
