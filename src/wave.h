#ifndef WAVE_H
#define WAVE_H
#include <gb/gb.h>
#include <stdint.h>

// banner rows of the score window (rendered into cvbuf as CV_BANNER, and its tiles uploaded)
extern uint8_t wave_on;
#define WVB ((uint8_t *)(((uint16_t)dlgbuf + 15) & ~15u))   // the wave's board and title masks
void wave_prep_title(const uint8_t *src, uint8_t c0, uint8_t n) BANKED;   // banner columns (20)
void wave_prep_lut(uint8_t i0, uint8_t n) BANKED;     // BG palette entries (32)
void wave_prep_board(uint8_t i0, uint8_t n) BANKED;   // board tiles (40)
void wave_begin(void) BANKED;
void wave_frame(int16_t wint) BANKED;   // wint: the score timer, 100 -> 0
void wave_end(void) BANKED;             // swaps the full banner in (by DMA, next VBlank)

#endif
