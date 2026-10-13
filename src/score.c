// The score panel after a round (draw_score / update_score): the banner with its wave,
// the score lines one by one, "continue".
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "common.h"
#include "wave.h"
#include "board.h"

static const canvas_t CV_BANNER = {cvbuf, 115, 0, 0, 0, 20, 2, 0, 1};
static canvas_t cv_line = {cvbuf, 155, 0, 0, 2, 20, 1, 0, 1};

static void banner_draw(uint8_t *buf) {
    canvas_t c = CV_BANNER;
    c.buf = buf;
    cv_begin(&c, 2);
    cv_bignumc(result == 3 ? "pass" : result == 1 ? "round won" : "round lost", 80, 3, 0, 0);
}

// The score window's setup, a little a frame in the wait after a round (game.c wait_board,
// t its frame, 0-69), as it all at once stopped the game for 13 frames: the banner drawn in
// WVB (the help box has left dlgbuf), its wave's title mask and colours, the window filled
// and the banner's tiles sent once the help box has slid away, and the board under the band
// last, a tile a frame, once its cards have settled. score_phase does what is left.
static uint8_t score_ready;

void score_prep(uint8_t t) BANKED {
    if (t == 6) banner_draw(WVB);
    else if (t >= 7 && t < 12) wave_prep_title(WVB, (t - 7) * 4, 4);
    else if (t >= 12 && t < 16) wave_prep_lut((t - 12) * 8, 8);
    else if (t >= 20 && t < 26) map_fill(1, 0, (t - 20) * 3, 20, 3, TILE_SOLID(3), 0);
    else if (t == 26) {   // tiles 115-154: 115-127 at 0x9730, 128-154 at 0x8800
        vram_dma_vbl(WVB, (uint8_t *)0x9000u + (115 << 4), 0, 13, _current_bank);
        vram_dma_vbl(WVB + 13 * 16, (uint8_t *)0x8800u, 0, 27, _current_bank);
        score_ready = 1;
    } else if (t >= 28 && t < 68) wave_prep_board(t - 28, 1);
}

uint8_t score_phase(void) BANKED {
    // returns 1 = next round, 0 = match over
    int16_t wint = 100, wintstep = 60;
    int16_t wy = BAND_LY * 16, wyd;
    uint8_t nl = 0, i, blink = 0, pulse = 0;
    // window: banner + orange panel; the banner rows fill with the wave first
    if (!score_ready) {   // (score_prep's)
        map_fill(1, 0, 0, 20, 18, TILE_SOLID(3), 0);
        banner_draw(cvbuf);
        cv_flush(&CV_BANNER);   // shown once the wave is done
    }
    score_ready = 0;
    wave_begin();           // the loop's wave_frame lands with the window coming on
    win_split = 1;
    win_show = 1;
    help_drawn = 0;
    helpi = 0;
    for (;;) {
        ticksfx = 55;
        update_gtxt();
        wint--;
        wyd = 112 - 16 - (nl ? 8 * nl + 4 : 0);
        wy += ((wyd * 16) - wy) / 8;
        WY_REG = (uint8_t)(wy >> 4);
        win_cut = (uint8_t)(wy >> 4);
        dochips();
        if (wint <= 0 && pend_i < pend_n && wave_on) wave_end();   // first line next frame (its tiles)
        else if (wint <= 0 && pend_i < pend_n && !vram_dma_busy()) {   // nor any wave upload left
            i = pend_i++;
            cv_line.y = 2 + nl;
            // 20 tiles a line; the third not at 195 (bank 0's 195-209 are a raised card's, lift.c,
            // and the card under the cursor stays raised under the panel): bank 1's 232-251
            cv_line.tile = nl == 2 ? 232 : 155 + nl * 20;
            cv_line.bank = nl == 2;
            // "continue" uses the card back palette: same orange, plus the plum it pulses from
            cv_line.pal = pend_kind[i] == 2 ? 2 : 0;
            cv_begin(&cv_line, 3);
            if (pend_kind[i] == 2) {
                cv_print(pend_lbl[i], 58, 1, 1);
                tempsave();
            } else {
                cv_print(pend_lbl[i], 12, 1, 1);
                if (pend_kind[i] == 0) cv_rprint(mlstr(pend_val[i], 0), 148, 1, 1);
                if (nl > 0) stakes += pend_val[i];
            }
            cv_draw(&cv_line);
            nl++;
            wint = wintstep;
        }
        // "continue" pulses plum -> purple a third of each second (sin(time()) < 0.5)
        if (pend_i == pend_n && nl) {
            uint8_t b;
            if (++pulse >= 60) pulse = 0;
            b = pulse >= 35 && pulse < 55;
            if (b != blink) {
                blink = b;
                cv_begin(&cv_line, 3);
                cv_print(pend_lbl[pend_n - 1], 58, 1, b ? 2 : 1);
                cv_draw(&cv_line);
            }
        }
        if (BTNP(J_A | J_B | J_START)) {
            wintstep = 5;
            wint = 0;
            if (pend_i == pend_n) {
                // winstakes
                if (result == 2) stakes = -stakes;
                wining += stakes;
                stakes = 0;
                if (round_ >= 5) {
                    if (bonusmode && result == 1) wbin = 1;
                    // the panel slides down behind the HUD while the counters finish
                    while (stakes_txt != stakes || wining_txt != wining || wy < (111 << 4)) {
                        update_gtxt();
                        wy += ((112 << 4) - wy) / 5 + 1;
                        WY_REG = (uint8_t)(wy >> 4);
                        win_cut = (uint8_t)(wy >> 4);
                        game_frame();
                    }
                    win_split = 0;
                    win_cut = 0;
                    return 0;
                }
                return 1;   // the panel closes with the board swap (board_reset_map)
            }
        }
        if (wave_on) wave_frame(wint);
        game_frame();
    }
}

