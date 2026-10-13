#ifndef RIPPLE_H
#define RIPPLE_H
#include <gb/gb.h>

#define STRIP_TILE 128   // BG bank 0: the strip along the winnings bar's top edge
#define EDGE_TILE 140    // the bar's edge outside the strip
extern uint8_t *strip;   // 16-byte aligned: the strip's 12 tiles, then the vial's top row (4)
void strip_draw(uint8_t wh) BANKED;   // wh: drpwh in hundredths (0 = flat)

// title_to_menu: frame nt (0-40) of the menu's bar rising over the title's; shown: its 10
// half columns' rows so far
void mound_step(uint8_t nt, uint8_t *shown) BANKED;

void bytes_rev(uint8_t *p, uint8_t n) BANKED;   // mirror tile rows (bit order) in place

#endif
