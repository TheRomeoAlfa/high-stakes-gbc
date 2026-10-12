#ifndef LIFT_H
#define LIFT_H
#include <gb/gb.h>
#include <stdint.h>

#define LIFT_FR_LIFTED 253   // card frame marker while a card is drawn by lift.c

extern uint8_t lift_cfr[9];   // keep in step with the board: card frames at rest
extern uint8_t lift_top[3];   // and the top slot kinds (sidx of gs[0..2])

void lift_luts(void) BANKED;            // after the board palettes are set
void lift_reset(void) BANKED;
uint8_t lift_begin(uint8_t ci) BANKED;  // slot + 1, 0 = none free
void lift_end(uint8_t slot1) BANKED;
void lift_set(uint8_t ci, uint8_t t, uint8_t face) BANKED;   // flip frame t (0..20) to show
void lift_update(void) BANKED;          // render changed frames (before draw_sprites)
void lift_sprites(void) BANKED;         // shadow sprites

#endif
