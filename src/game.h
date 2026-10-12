#ifndef GAME_H
#define GAME_H
#include <gb/gb.h>

extern volatile uint8_t win_show;
extern volatile uint8_t win_split;
#define BAND_LY 96   // first line of the score banner
extern volatile uint8_t band_on;
extern uint8_t band_pal[6], band_rest[6];

void game_main(void) BANKED;

#endif
