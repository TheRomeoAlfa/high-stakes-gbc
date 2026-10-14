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
    if (spr_split) {   // every VBlank, even when the main loop runs late (lcd_isr turns them off)
        LCDC_REG |= LCDCF_OBJON;
        if (spl_ly) LCDC_REG |= LCDCF_OBJ16;
        else LCDC_REG &= ~LCDCF_OBJ16;
        LYC_REG = (spl_ly ? spl_ly : cut_ly) - 1;
    }
}

// score banner wave: BG palette 0 colours 1-3 are swapped on the band's lines (see game.c)
volatile uint8_t band_on;
uint8_t band_pal[6], band_rest[6];
static uint8_t band_set;
static uint8_t obj_pend, obj_off, obj_ly;   // the sprite cut at win_cut: armed / sprites off
                                            // till 112 / its line

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
    if (spr_split) {   // gfx.c: 8x8 sprites from spl_ly, none from cut_ly
        uint8_t ly = LYC_REG;
        while (STAT_REG & 3);
        if (spl_ly && ly + 1 == spl_ly) {
            LCDC_REG &= ~LCDCF_OBJ16;
            if (cut_ly) LYC_REG = cut_ly - 1;
        } else LCDC_REG &= ~LCDCF_OBJON;
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
    if (obj_pend) {   // the score window's top: no sprites over it (back on at 112). Fired two
        //              lines early (another interrupt can hold this one past a line), it waits
        //              for the HBlank before the window's first line
        uint8_t ly = obj_ly - 1;
        obj_pend = 0;
        while (LY_REG < ly);
        while (STAT_REG & 3);
        LCDC_REG &= ~LCDCF_OBJON;
        if (ly == BAND_LY - 1) {
            band_write(band_pal);
            band_set = 1;
            LYC_REG = 111;
        } else LYC_REG = band_on && ly < BAND_LY - 1 ? BAND_LY - 1 : 111;
        return;
    }
    if (LYC_REG == BAND_LY - 1) {
        while (STAT_REG & 3);
        band_write(band_pal);
        band_set = 1;
        LYC_REG = 111;
        return;
    }
    if (win_split || band_set || obj_off) {
        while (STAT_REG & 3);
        if (win_split) LCDC_REG &= ~LCDCF_WINON;
        if (band_set) {
            band_write(band_rest);
            band_set = 0;
        }
        if (obj_off) LCDC_REG |= LCDCF_OBJON;
        if (win_cut) WY_REG = win_cut;   // next frame's window top, with its sprite cut
    }
    // next frame: the sprite cut first, unless the band comes before it
    obj_off = win_cut && win_cut < 112 && !(band_on && win_cut > BAND_LY);
    if (obj_off) {
        obj_ly = win_cut;
        LYC_REG = obj_ly - 2;
        obj_pend = 1;
    } else if (band_on) LYC_REG = BAND_LY - 1;
}

const uint8_t blank16[16] = {0};

void dmg_message(void) BANKED;   // dmg.c


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
