// High Stakes - Game Boy Color demake
// Original PICO-8 game by Krystian Majewski (Lazy Devs), music by Gruber,
// cover design by Tyler Q Anderson and Jamie C Lee. CC BY-NC-SA 4.0.
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gb/hardware.h>
#include <rand.h>
#include "gen/assets.h"
#include "gfx.h"
#include "sound.h"
#include "game.h"

volatile uint8_t win_show;   // window enabled at top of frame
volatile uint8_t win_split;  // cut window at LY 112 (keeps the HUD visible)

volatile uint8_t joy_latch;
static uint8_t joy_isr_prev;

static void vbl_isr(void) {
    // latch new presses every frame so short taps are never missed,
    // even while the main loop is busy rendering
    uint8_t j = joypad();
    joy_latch |= j & ~joy_isr_prev;
    joy_isr_prev = j;
    if (win_show) LCDC_REG |= LCDCF_WINON;
    else LCDC_REG &= ~LCDCF_WINON;
}

static void lcd_isr(void) {
    if (win_split) LCDC_REG &= ~LCDCF_WINON;
}

static const uint8_t blank16[16] = {0};

// On a DMG/MGB/SGB, explain why the game will not run instead of showing a blank screen.
static void dmg_message(void) {
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

void main(void) {
    DISPLAY_OFF;
    if (_cpu != CGB_TYPE) dmg_message();
    cpu_fast();
    snd_init();

    // global VRAM: card tiles in bank 1, game sprites in bank 0
    load_bkg_banked(BANK(card_tiles), card_tiles, 0, N_CARD_TILES, 1);
    load_spr_banked(BANK(spr_tiles), spr_tiles, 0, N_SPR_TILES, 0);
    set_bkg_data(0, 1, blank16);
    make_solid_tiles();
    clear_bg();

    SPRITES_8x16;
    SHOW_SPRITES;
    SHOW_BKG;
    WX_REG = 7;
    WY_REG = 144;

    CRITICAL {
        add_VBL(vbl_isr);
        STAT_REG = STATF_LYC;
        LYC_REG = 112;
        add_LCD(lcd_isr);
        add_LCD(nowait_int_handler);
    }
    set_interrupts(VBL_IFLAG | LCD_IFLAG);
    DISPLAY_ON;

    game_main();
}
