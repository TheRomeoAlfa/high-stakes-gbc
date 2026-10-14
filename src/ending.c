// The ending (you died / you won): out of scenes.c, whose bank is full.
#pragma bank 255
#include <gb/gb.h>
#include "common.h"

// d / 10 as shifts (0.0996), and the rising text's 24 px offset, (18 * 256 - h) / 192, as
// / 64 then / 3: SDCC's 16-bit divisions are slow (as scenes.c's winnings)
static int16_t tenth(int16_t d) {
    uint16_t a = (uint16_t)(d < 0 ? -d : d);
    a = (a >> 4) + (a >> 5) + (a >> 8) + (a >> 9);
    return d < 0 ? -(int16_t)a : (int16_t)a;
}
static uint8_t px24(uint16_t h) {
    uint8_t v = (uint8_t)((uint16_t)(18 * 256 - h) >> 6);
    return (uint8_t)(((uint16_t)v * 171) >> 9);
}

// ------------------------------------------------------------------ ending
// which ending; its texts live here (a literal in scenes.c's bank can't be read from this one)
static uint8_t e_died;
void goverprint_set(uint8_t died) BANKED { e_died = died; }

void scene_end(void) BANKED {
    static const canvas_t txt = {cvbuf, 100, 0, 2, 3, 16, 3, 0, 0};
    uint16_t wint = 1000, th = 0;
    uint8_t died = e_died;
    const char *eg1 = died ? "you died" : "you won", *eg2 = died ? "exsanguination" : "congratulations";
    reset_screen();
    set_bgpal(0, 0, 9, 8, 7);
    if (died) {
        copy_bgpals(1, 7, glasses_pals);
        load_bkg_banked(BANK(glasses_tiles), glasses_tiles, 1, N_GLASSES_TILES, 0);
        map_put(0, 4, 7, GLASSES_W, GLASSES_H, glasses_map, glasses_attr);
    } else {
        copy_bgpals(1, 7, sunset_pals);
        load_bkg_banked(BANK(sunset_tiles), sunset_tiles, 1, N_SUNSET_TILES, 0);
        map_put(0, 4, 7, SUNSET_W, SUNSET_H, sunset_map, sunset_attr);
    }
    // the text drawn once, risen; its rise is a scroll of its lines (24-47, BG rows 3-5) by
    // the LCD interrupt's box split (gfx.c): lines above it in the map are blank, so lower
    // down it is cut off at line 48 as the original's clip. Redrawn at each pixel of the
    // rise it held the screen to 20-30 fps.
    cv_begin(&txt, 0);
    cv_cprint(eg2, 64, 2, 1);
    cv_bignumc(eg1, 64, 9, 2, 0);
    cv_draw(&txt);
    box_nly0 = 24;
    box_nly1 = 48;
    box_nscy = (uint8_t)(0 - 24);
    box_on = 1;
    screen_on();
    while (wint--) {
        // govertxth: the text rises 18 px into its clip (/10 a frame); 24 px canvas here
        box_nscy = (uint8_t)(0 - px24(th));
        th += tenth(18 * 256 - th);
        frame();
        if (BTNP(J_A | J_START)) break;
    }
    savegame();
    fadeout(0);
    wait_frames(120);
    box_on = 0;
    SCY_REG = 0;
    music(0);
    scene = SC_START;
}

