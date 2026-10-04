#ifndef GAME_H
#define GAME_H
#include <gb/gb.h>

extern volatile uint8_t win_show;
extern volatile uint8_t win_split;

void game_main(void) BANKED;

#endif
