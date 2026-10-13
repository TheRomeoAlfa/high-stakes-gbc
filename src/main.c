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
volatile uint8_t win_split;  // cut window from LY 112 (keeps the HUD visible)

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

// score banner wave: BG palette 0 colours 1-3 are swapped on the band's lines (see game.c)
volatile uint8_t band_on;
uint8_t band_pal[6], band_rest[6];
static uint8_t band_set;

static void band_write(const uint8_t *p) {   // in HBlank: palette RAM is locked while drawing
    BCPS_REG = 0x82;        // palette 0, colour 1, auto-increment
    BCPD_REG = p[0];
    BCPD_REG = p[1];
    BCPD_REG = p[2];
    BCPD_REG = p[3];
    BCPD_REG = p[4];
    BCPD_REG = p[5];
}

// Fires a line early and waits for that line's HBlank: by the time the handler runs, the
// line it triggered on is already being drawn, and a window switched off mid-line leaves
// its left part showing.
static void lcd_isr(void) {
    if (scx_split_ly) {   // opponent menu: the text below the cards doesn't scroll sideways
        while (STAT_REG & 3);
        SCX_REG = scx_split_val;
        return;
    }
    if (box_on) {   // dialog box lines: their own scroll (gfx.c box_on)
        uint8_t ly = LY_REG;
        while (STAT_REG & 3);
        if (ly >= 143 || ly < box_ly0) {   // the box starts (next frame's top if at 143)
            SCY_REG = box_scy;
            LYC_REG = box_ly1 - 1;
        } else {                            // the box ends
            SCY_REG = scy - cam_y;
            LYC_REG = box_ly0 ? box_ly0 - 1 : 143;   // so a late frame() keeps it up
        }
        return;
    }
    if (LYC_REG == BAND_LY - 1) {
        while (STAT_REG & 3);
        band_write(band_pal);
        band_set = 1;
        LYC_REG = 111;
        return;
    }
    if (win_split || band_set) {
        while (STAT_REG & 3);
        if (win_split) LCDC_REG &= ~LCDCF_WINON;
        if (band_set) {
            band_write(band_rest);
            band_set = 0;
        }
    }
    if (band_on) LYC_REG = BAND_LY - 1;
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
        LYC_REG = 111;
        add_LCD(lcd_isr);
        add_LCD(nowait_int_handler);
    }
    set_interrupts(VBL_IFLAG | LCD_IFLAG);
    DISPLAY_ON;

    game_main();
}
