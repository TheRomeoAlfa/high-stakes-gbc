// The message shown on a DMG/MGB/SGB (out of bank 0: it runs once, at boot).
#pragma bank 255
#include <gb/gb.h>
#include "gfx.h"

extern const uint8_t blank16[16];

// On a DMG/MGB/SGB, explain why the game will not run instead of showing a blank screen.
void dmg_message(void) BANKED {
    static const canvas_t cv = {cvbuf, 1, 0, 0, 7, 20, 3, 0, 0};
    static uint8_t row[20];
    uint8_t i, r, t = 1;
    set_bkg_data(0, 1, blank16);
    fill_bkg_rect(0, 0, 32, 32, 0);
    cv_begin(&cv, 0);
    cv_cprint("high stakes requires", 80, 2, 3);
    cv_cprint("a game boy color", 80, 9, 3);
    cv_cprint("(set your emulator to gbc)", 80, 16, 3);
    cv_flush(&cv);
    for (r = 0; r < 3; r++) {
        for (i = 0; i < 20; i++) row[i] = t++;
        set_bkg_tiles(0, 7 + r, 20, 1, row);
    }
    BGP_REG = 0xE4;
    SCX_REG = SCY_REG = 0;
    SHOW_BKG;
    DISPLAY_ON;
    while (1) vsync();
}
