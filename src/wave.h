#ifndef WAVE_H
#define WAVE_H
#include <gb/gb.h>
#include <stdint.h>

// banner rows of the score window (rendered into cvbuf as CV_BANNER, and its tiles uploaded)
extern uint8_t wave_on;
void wave_begin(void) BANKED;
void wave_frame(int16_t wint) BANKED;   // wint: the score timer, 100 -> 0
void wave_end(void) BANKED;             // swaps the full banner in (by DMA, next VBlank)

#endif
