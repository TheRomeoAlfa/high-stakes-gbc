#ifndef RIPPLE_H
#define RIPPLE_H
#include <gb/gb.h>

#define STRIP_TILE 128   // BG bank 0: the strip along the winnings bar's top edge
#define EDGE_TILE 140    // the bar's edge outside the strip
extern uint8_t *strip;   // 16-byte aligned: the strip's 12 tiles, then the vial's top row (4)
void strip_draw(uint8_t wh) BANKED;   // wh: drpwh in hundredths (0 = flat)

#endif
