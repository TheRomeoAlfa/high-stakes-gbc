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

void main(void) {
    DISPLAY_OFF;
    if (_cpu != CGB_TYPE) {
        // CGB only: show a plain message on DMG
        DISPLAY_ON;
        while (1) vsync();
    }
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
