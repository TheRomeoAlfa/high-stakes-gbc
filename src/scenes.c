// Non-gameplay scenes: credits, title, intro, opponent menu, bonus ask, winnings, ending.
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include <rand.h>
#include "common.h"

// kept in this file: string literals live in the same ROM bank as their users
const char *const vname[4] = {"bafur", "houkin", "zver", "orlok"};
const char *const vquote[4] = {"'i don't bite'", "'rent is due'", "'zamorit chervichka'", "'blood is life!'"};
const uint8_t vmult[4] = {1, 5, 10, 100};
const int16_t vcost[4] = {0, 50, 200, 1000};
const uint8_t vtoken[4] = {2, 1, 0, 0};


static const int8_t sintab[32] = {0, 1, 1, 2, 2, 3, 3, 3, 3, 3, 3, 3, 2, 2, 1, 1,
                                  0, -1, -1, -2, -2, -3, -3, -3, -3, -3, -3, -3, -2, -2, -1, -1};

// ------------------------------------------------------------------ rings (title / winnings)
// docircs: rings 10 px apart grow from the centre 0.29 px a frame (the original's 0.26 on
// its 9 px spacing). Each 1 px step is a pre-drawn quarter of the ring area (gen_assets),
// streamed by DMA into tiles 1..70; the map mirrors it with tile flips. Palette z holds the
// colours of ring zones z..z+2 (by radius), as the original's circcols.
static uint8_t ring_step = 255;
static uint16_t ring_t;

static void ring_upload(void) {
    uint8_t h = RING_STEPS / 2, s = ring_step;
    const uint8_t *src = (s < h ? rings_a : rings_b) + (uint16_t)(s < h ? s : s - h) * (RING_QTILES * 16);
    vram_dma_vbl(src, (uint8_t *)0x9010u, 0, RING_QTILES, s < h ? RING_BANK_A : RING_BANK_B);
}

static void rings_setup(uint8_t variant) {
    static const uint8_t cols[2][4] = {{3, 10, 2, 9}, {3, 10, 10, 10}};   // circcols1 / circcols2
    uint8_t r, z;
    const uint8_t *c = cols[variant];
    for (r = 0; r < 13; r++) map_put(0, 0, r, 20, 1, ring_map + r * 20, ring_attr + r * 20);
    for (z = 0; z < 4; z++)
        set_bgpal(z, 0, c[z], c[z < 3 ? z + 1 : 3], c[z < 2 ? z + 2 : 3]);
    ring_step = 0;
    ring_upload();
}

static void rings_update(void) {
    uint8_t s;
    ring_t += 74;   // 0.29 px a frame, in 1/256
    s = (uint8_t)(ring_t >> 8);
    if (s >= RING_STEPS) {
        ring_t -= (uint16_t)RING_STEPS << 8;
        s -= RING_STEPS;
    }
    if (s == ring_step || vram_dma_busy()) return;
    ring_step = s;
    ring_upload();
}

// ------------------------------------------------------------------ dialog box
static canvas_t cv_dlg = {dlgbuf, 180, 0, 5, 1, 15, 4, 0, 0};
static const char *dlg_text;
static uint8_t dlg_pos, dlg_len;
static int16_t dlg_x, dlg_y;

// The box (and its card) slides down from above the screen as the original's helpy
// (/4 a frame): it is drawn in BG rows 20-23, shown at any line by a scroll split.
#define BOX_ROW 20
static int16_t box_y, box_ty;   // 8.8 screen line of the box's top, and its target

static void box_place(void) {
    int16_t y = box_y >> 8;
    if (y < -31) y = -31;
    box_nly0 = y > 0 ? (uint8_t)y : 0;
    box_nly1 = (uint8_t)(y + 32);
    box_nscy = (uint8_t)(BOX_ROW * 8 - y);
}

static void dlg_slide_in(int16_t ty) {
    map_fill(0, 0, BOX_ROW - 2, 20, 6, 0, 0);   // rows above the box show while it is high
    box_ty = ty << 8;
    box_y = -31 << 8;
    box_place();
    box_on = 1;
}

static void dlg_slide(void) {
    if (box_y != box_ty) {
        int16_t d = (box_ty - box_y) / 4;
        box_y += d ? d : box_ty - box_y;
        box_place();
    }
}

void dlg_setup(uint8_t ty, uint8_t with_card) BANKED {
    cv_dlg.y = ty;
    cv_dlg.x = with_card ? 5 : 1;
    cv_dlg.w = with_card ? 15 : 18;
    if (with_card) draw_card(1, ty, CF_FACE(vampval));
    dlg_text = 0;
    dlg_len = dlg_pos = 0;
    cv_begin(&cv_dlg, 0);
    cv_draw(&cv_dlg);
}

void dlg_say(const char *s) BANKED {
    uint8_t w = cv_dlg.w * 8;
    dlg_text = s;
    dlg_len = strlen(s);
    dlg_pos = 0;
    dlg_x = 6;
    dlg_y = 6;
    cv_begin(&cv_dlg, 0);
    cv_frame(0, 1, w, 28, 1);
    cv_draw(&cv_dlg);
}

static void dlg_flush_at(int16_t x, int16_t y) {
    uint16_t a = cv_tile_at(x, y), b = cv_tile_at(x + 7, y), c = cv_tile_at(x, y + 5), d = cv_tile_at(x + 7, y + 5);
    cv_flush_tile(&cv_dlg, a);
    if (b != a) cv_flush_tile(&cv_dlg, b);
    if (c != a) cv_flush_tile(&cv_dlg, c);
    if (d != b && d != c) cv_flush_tile(&cv_dlg, d);
}

uint8_t dlg_tick(void) BANKED {
    char ch;
    if (!dlg_text || dlg_pos >= dlg_len) return 1;
    if (frames & 1) return 0;
    ch = dlg_text[dlg_pos++];
    cv_select(&cv_dlg);
    if (ch == '\n') {
        dlg_x = 6;
        dlg_y += 6;
    } else {
        int16_t x0 = dlg_x;
        dlg_x = cv_putc(ch, dlg_x, dlg_y, 2);
        dlg_flush_at(x0, dlg_y);
        sfx(52);
    }
    return dlg_pos >= dlg_len;
}

void dlg_arrow(uint8_t on) BANKED {
    int16_t x = cv_dlg.w * 8 - 12, y = 21;
    cv_select(&cv_dlg);
    cv_rect(x, y, x + 6, y + 4, 0);
    if (on) cv_putc('\x04', x, y, 1);
    dlg_flush_at(x, y);
}

// ------------------------------------------------------------------ credits
void scene_credits(void) BANKED {
    static const canvas_t cv = {cvbuf, 100, 0, 2, 4, 16, 9, 0, 0};
    static const char *const lines[11] = {"made by", "krystian majewski", "@lazydevsacademy", "", "music by",
                                          "grubermusic", "@grubermusic", "", "based on a design by",
                                          "tyler anderson", "@tandyq"};
    uint8_t i, t = 240;
    reset_screen();
    set_bgpal(0, 9, 10, 2, 0);
    cv_begin(&cv, 0);
    for (i = 0; i < 11; i++) cv_cprint(lines[i], 64, 3 + i * 6, 1);
    cv_draw(&cv);
    screen_on();
    while (t--) {
        frame();
        if (BTNP(J_A | J_B | J_START)) break;
    }
    initrand(((uint16_t)DIV_REG << 8) | frames);
    fadeout(1);
    scene = SC_START;
}

// ------------------------------------------------------------------ title
static const char *stmnu[3];
static uint8_t nst;

static void draw_logo(int16_t cx, int16_t cy) {
    uint8_t i;
    const uint8_t *p = logo_parts;
    for (i = 0; i < N_LOGO_PARTS; i++, p += 4)
        spr_put(cx - 20 + p[0], cy - 24 + p[1], p[2], p[3] | 0x08);
}

static const char *chaintxt(void) {
    if (chain == 0) return "";
    s_begin();
    s_str("streak:");
    s_num(chain);
    if (chain == bchain) s_str("!");
    return s_end();
}

void scene_start(void) BANKED {
    static const canvas_t bar = {cvbuf, 100, 0, 0, 13, 20, 5, 4, 0};
    static const canvas_t cvs = {cvbuf, 200, 0, 5, 12, 10, 1, 5, 0};
    // the options sit 50 px apart on a strip that slides (stmnux, /5 a frame) in a clip
    static const canvas_t opt = {cvbuf, 210, 0, 7, 15, 6, 2, 4, 0};   // 48 px, as the original's
    uint8_t stcur = 0, redraw = 1, flash = 0, abob = 0, i;
    int16_t sx = 0, sxd = 255;
    uint16_t bx = 0, by = 0;
    nst = 0;
    if (blood > 0 && blood < 5000) stmnu[nst++] = "continue";
    stmnu[nst++] = "new game";
    if (bchain > 0) stmnu[nst++] = "score mode";

    reset_screen();
    rings_setup(0);
    load_spr_banked(BANK(logo_tiles), logo_tiles, 0, N_LOGO_TILES, 1);
    copy_objpals(0, 3, logo_pals);
    set_bgpal(4, 8, 0, 7, 9);
    set_bgpal(5, 0, 14, 14, 14);
    set_objpal(3, 7, 7, 7);
    cv_begin(&cvs, 0);
    cv_cprint(chaintxt(), 40, 2, 1);
    cv_draw(&cvs);
    rings_update();
    screen_on();
    for (;;) {
        if (redraw) {
            cv_begin(&bar, 0);
            cv_bignumc("high stakes", 80, 4, 1, 0);
#ifdef DEBUG_FPS
            s_begin(); s_num((int16_t)(dbg_loops & 0x7FFF)); s_str("/"); s_num((int16_t)(sys_time & 0x7FFF));
            cv_print(s_end(), 2, 33, 1);
#else
            cv_print("gb v1", 2, 33, 3);
#endif
            cv_draw(&bar);
            redraw = 0;
            sxd = 255;
        }
        sx += (stcur * 50 * 16 - sx) / 5;
        if ((sx >> 4) != sxd) {
            sxd = sx >> 4;
            cv_begin(&opt, 0);
            for (i = 0; i < nst; i++) cv_cprint(stmnu[i], 24 + i * 50 - sxd, 6, 2);
            cv_draw(&opt);
        }
        rings_update();
#ifdef DEBUG_FPS
        if ((frames & 63) == 0) redraw = 1;
#endif
        {
            uint8_t f = ((frames >> 3) & 1);
            if (f != flash) {
                flash = f;
                bgpal[4 * 4 + 2] = pico[f ? 15 : 7];
                set_objpal(3, f ? 15 : 7, f ? 15 : 7, f ? 15 : 7);   // the arrows flash with it
                pal_dirty = 1;
            }
        }
        {   // ◀ ▶ breathe in and out a pixel, twice a second (30.5 - sin(t*2), 90.5 + sin(t*2))
            static const int8_t bob[30] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 0};
            int8_t o = bob[abob];
            if (++abob >= 30) abob = 0;
            if (stcur > 0) spr_put(42 + o, 126, SPR_MARR_L, 3);
            if (stcur < nst - 1) spr_put(112 - o, 126, SPR_MARR_R, 3);
        }
        // cos(t/3), cos(t/4): periods 180 and 240 frames, 32 steps each (8.8)
        bx += 46;
        by += 34;
        draw_logo(80 + sintab[(bx >> 8) & 31], 50 + sintab[(by >> 8) & 31]);
        if (BTNP(J_LEFT) && stcur > 0) { stcur--; sfx(63); }
        else if (BTNP(J_RIGHT) && stcur < nst - 1) { stcur++; sfx(63); }
        if (BTNP(J_A | J_START)) {
            sfx(53);
            blood_txt = blood;
            if (stmnu[stcur][0] == 'n') {
                if (blood < 5000) chain = 0;
                newgame();
                scene = SC_INTRO;
            } else {
                scoremode = stmnu[stcur][0] == 's';
                scene = SC_MENU;
            }
            fadeout(1);
            return;
        }
        frame();
    }
}

// ------------------------------------------------------------------ intro
void scene_intro(void) BANKED {
    static const char *const talk[6] = {
        "an average human body\ncontains about 5000ML\nof blood",
        "heheheh...",
        "oh no! where did all\nthe blood go?!\nheheheh...",
        "don't take it too hard\nyou were living on\nborrowed time anyway",
        "here, the last shot is\non me. it will take\nthe edge off",
        "or maybe you can win\nit all back? how about\nit? one last game..."};
    static const canvas_t big = {cvbuf, 140, 0, 5, 14, 10, 2, 0, 0};   // just wide enough for 5000ML
    uint8_t ti = 0, done = 0, arrow = 0, picfade = 0, k;
    reset_screen();
    vampval = 10;
    set_bgpal(0, 0, 10, 2, 8);
    card_pals_load();
    copy_bgpals(5, 3, whisky_pals);
    for (k = 20; k < 32; k++) bgpal[k] = 0;   // the picture fades in when it is put down
    load_bkg_banked(BANK(whisky_tiles), whisky_tiles, IMG_BASE, N_WHISKY_TILES, 1);
    blood = blood_txt = 5000;
    mult = 1;
    ticksfx = 0;
    dlg_slide_in(8);
    dlg_setup(BOX_ROW, 1);
    dlg_say(talk[0]);
    gtxt_changed = 4;
    screen_on();
    for (;;) {
        update_gtxt();
        dlg_slide();
        if (picfade && picfade <= 33) {   // 33 frames from black, by the fade table
            uint8_t lv = (uint8_t)(picfade * 25 / 33);
            const uint8_t *ft = fade_tab + lv * 32;
            for (k = 0; k < 12; k++) {
                uint16_t v = whisky_pals[k];
                bgpal[20 + k] = ft[v & 31] | ((uint16_t)ft[(v >> 5) & 31] << 5) | ((uint16_t)ft[(v >> 10) & 31] << 10);
            }
            pal_dirty = 1;
            picfade++;
        }
        if (gtxt_changed & 4) {
            cv_begin(&big, 0);
            cv_bignumc(numstr(blood_txt), 40, 2, 3, 1);
            cv_draw(&big);
            gtxt_changed = 0;
        }
        done = dlg_tick();
        if (done) {
            uint8_t a = (frames >> 4) & 1;
            if (a != arrow) { arrow = a; dlg_arrow(a); }
        }
        if (BTNP(J_A | J_START)) {
            if (ti < 5) {
                ti++;
                dlg_say(talk[ti]);
                arrow = 0;
                if (ti == 2) { blood = 0; mult = 187; ticksfx = 54; }
                else if (ti == 4) {
                    blood = 20; blood_txt = 0; mult = 1;
                    map_put(0, 8, 7, WHISKY_W, WHISKY_H, whisky_map, whisky_attr);
                    picfade = 1;
                }
            } else {
                sfx(51);
                fadeout(1);
                scene = SC_MENU;
                scoremode = 0;
                ticksfx = 0;
                blood_txt = blood;
                return;
            }
        }
        frame();
    }
}

// ------------------------------------------------------------------ blood vial + bar (menu / winnings)
// port of drawblood()'s fill level: rows of the vial still red ("empty")
static int16_t vial_level(int16_t bl) {
    int16_t px = bl / 3 - bl / 345;   // 1652 * bl / 5000 in 16 bits (keeps 32-bit maths out of bank 0)
    static const int16_t lookup[6] = {0, 8, 30, 80, 162, 212};
    uint8_t i = 0;
    if (px >= 212) return 10 - (px - 212) / 144;
    do i++; while (!(px <= lookup[i - 1]));
    return 15 - (i - 1);
}

static void draw_vial(const canvas_t *cv, int16_t ox, int16_t oy, int16_t bl) {
    // red rows on top mean "empty", black is blood
    int16_t pxperc = vial_level(bl);
    uint8_t x, y, c;
    cv_select(cv);
    for (y = 0; y < 19; y++)
        for (x = 0; x < 9; x++) {
            c = vial_px[y * 9 + x];
            if (c == 8) continue;  // outside: bar colour
            if (c == 2) c = (y < pxperc) ? 0 : 1;  // red(empty) / black(blood)
            else c = 1;
            cv_pset(ox + x, oy + y, c);
            cv_pset(ox + 17 - x, oy + y, c);
        }
}

// menu: text in BG rows 8-11 (moves with the cards), the bar in the window from line 104
static const canvas_t CV_MTXT = {cvbuf, 1, 0, 0, 8, 20, 4, 0, 0};
static const canvas_t CV_VIAL = {cvbuf, 81, 0, 8, 0, 4, 3, 5, 1};
static const canvas_t CV_BAR2 = {cvbuf, 93, 0, 0, 3, 20, 2, 5, 1};

static void bar_draw(uint8_t show_streak) {
    cv_begin(&CV_VIAL, 0);
    draw_vial(&CV_VIAL, 7, 3, blood_txt);
    cv_draw(&CV_VIAL);
    cv_begin(&CV_BAR2, 0);
    cv_bignumc(numstr(blood_txt), 80, 3, 1, 1);
    if (show_streak) {
        cv_print(chaintxt(), 2, 2, 2);
        s_begin(); s_str("game:"); s_num(tries);
        cv_print(s_end(), 2, 9, 2);
    }
    cv_draw(&CV_BAR2);
}

static void bar_setup(void) {
    set_bgpal(5, 8, 0, 9, 10);
    map_fill(1, 0, 5, 20, 13, 0, 5);
}

// ------------------------------------------------------------------ opponent menu
static uint8_t mflash;

static void menu_text(void) {
    static const char *t[4];
    static uint8_t col[4];
    static const uint8_t ty[4] = {2, 10, 19, 25};
    static char buf2[24];
    uint8_t i;
    col[0] = 1; col[1] = 2; col[2] = 3; col[3] = 3;
    if (scoremode) {
        t[0] = vname[mnucur];
        s_begin(); s_str("high score:"); s_num(high[mnucur]);
        strcpy(buf2, s_end());
        t[1] = buf2;
        t[2] = "";
        t[3] = "\x01 deal me in";
        col[2] = 2;
    } else if (unlocked[mnucur]) {
        t[0] = vname[mnucur];
        t[1] = vquote[mnucur];
        s_begin(); s_str("payout "); s_num(vmult[mnucur]); s_str("x");
        strcpy(buf2, s_end());
        t[2] = buf2;
        t[3] = "\x01 deal me in";
        col[1] = 2; col[2] = 2;
    } else {
        t[0] = "???";
        s_begin(); s_str("payout "); s_num(vmult[mnucur]); s_str("x");
        strcpy(buf2, s_end());
        t[1] = buf2;
        s_begin(); s_str("buy-in: "); s_num(vcost[mnucur]); s_str("ML");
        t[2] = s_end();
        t[3] = "\x01 unlock";
    }
    cv_begin(&CV_MTXT, 0);
    for (i = 0; i < 4; i++) cv_cprint(t[i], 80, ty[i], col[i]);
    cv_draw(&CV_MTXT);
}

// ---- the opponent cards (update_menu): each drifts on its own, randx / randy =
// cos((t + i * 0.79) / 2 or 3) * 1.7, and rides an arc, y + cos((x - 64) / 160) * 5.
// Drawn as 8x8 sprites (menu_spr_tiles in OBJ bank 1, card palettes), behind the bar.
static const int8_t mcos[64] = {
    127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
    0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126,
    -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12,
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126
};

static void menu_cards(int16_t mxp, int16_t d, uint16_t t) {
    uint8_t n, i, p, k;
    for (n = 0; n < 4; n++) {   // the selected card first: it wins the per-line sprite limit
        int16_t left, top, dx;
        int8_t rx, ry, dip;
        i = n == 0 ? mnucur : (n <= mnucur ? n - 1 : n);
        rx = (int8_t)(((int16_t)mcos[(uint8_t)((t * 546u + i * 25887u) >> 10) & 63] * 109) >> 13);
        ry = (int8_t)(((int16_t)mcos[(uint8_t)((t * 364u + i * 17257u) >> 10) & 63] * 109) >> 13);
        left = 64 + i * 48 - mxp + rx;
        dx = left + 10 - 80;
        if (dx < 0) dx = -dx;
        k = dx > 150 ? 63 : (uint8_t)(dx * 5 / 12) & 63;   // 160 original px a turn
        dip = (int8_t)(mcos[k] * 5 / 127) - 5;
        top = 16 + d + dip + ry;
        for (p = 0; p < 12; p++) {   // (past 40 sprites, a far card's edge drops for a moment)
            int16_t x = left + (p % 3) * 8, y = top + (p / 3) * 8;
            k = i * 12 + p;
            if (x > -8 && x < 168 && y > -8 && y < 144)
                spr_put(x, y, k, menu_spr_pal[k] | 0x88);   // OBJ bank 1, behind BG colours 1-3
        }
    }
}

// the arrows beside the selected card: BG (the sprite palettes are the cards'), bobbing
static const canvas_t CV_MARR = {cvbuf, 140, 0, 5, 3, 10, 2, 0, 0};

static void menu_arrows(int8_t b) {
    cv_begin(&CV_MARR, 0);
    if (mnucur > 0) cv_putc('\x02', 52 - b - 40, 6, 2);
    if (mnucur < 3) cv_putc('\x03', 102 + b - 40, 6, 2);
    cv_draw(&CV_MARR);
}

// mnuy: the menu slides up into place from below (from the title or the intro), or down
// from above after the winnings screen, easing 1/20 a frame; the bar stays
int16_t menu_from = 69;

void scene_menu(void) BANKED {
    uint8_t redraw = 1;
    int8_t ab = 99;
    uint16_t mt = 0;
    int16_t mx, mtarget, my = menu_from << 8, d;
    menu_from = 69;
    reset_screen();
    set_bgpal(0, 0, 8, 9, 7);
    copy_objpals(0, 4, card_pals);
    load_spr_banked(BANK(menu_spr_tiles), menu_spr_tiles, 0, 48, 1);
    SPRITES_8x8;   // reset_screen() sets 8x16 again
    bar_setup();
    map_fill(1, 0, 0, 20, 5, 0, 5);
    map_fill(0, 0, 12, 32, 20, 0, 0);   // the slide scrolls these rows into view
    mtarget = (int16_t)mnucur * 48 - 4;
    mx = mtarget * 16;
    WY_REG = 104;
    win_show = 1;
    blood_txt = blood;
    bar_draw(!scoremode);
    menu_text();
    mflash = 0;
    screen_on();
    for (;;) {
        update_gtxt();
        if (gtxt_changed & 4) {
            bar_draw(!scoremode);
            gtxt_changed = 0;
        }
        if (BTNP(J_LEFT) && mnucur > 0) { mnucur--; sfx(63); redraw = 1; }
        else if (BTNP(J_RIGHT) && mnucur < 3) { mnucur++; sfx(63); redraw = 1; }
        if (redraw) { menu_text(); redraw = 0; ab = 99; }
        mtarget = (int16_t)mnucur * 48 - 4;
        mx += (mtarget * 16 - mx) / 10;
        my -= my / 20;
        if (my > -128 && my < 128) my = 0;
        d = my >> 8;
        scy = (uint8_t)-d;
        menu_cards(mx >> 4, d, mt++);
        {
            uint8_t f = (frames >> 3) & 1;
            if (f != mflash) {
                mflash = f;
                bgpal[3] = pico[f ? 15 : 7];
                pal_dirty = 1;
            }
        }
        {
            int8_t b = sintab[(frames >> 2) & 31];
            if (b != ab) { ab = b; menu_arrows(b); }
        }
        if (BTNP(J_A | J_START)) {
            if (unlocked[mnucur] || scoremode) {
                sfx(53);
                fadeout(1);
                vampval = 10 + mnucur;
                mult = vmult[mnucur];
                dropboxes = mnucur > 0;
                droptoken = vtoken[mnucur];
                blood_txt = blood;
                ticksfx = 0;
                startmatch();
                scene = SC_GAME;
                return;
            } else if (blood > vcost[mnucur]) {
                sfx(53);
                mult = vmult[mnucur];
                ticksfx = 54;
                blood -= vcost[mnucur];
                unlocked[mnucur] = 1;
                savegame();
                redraw = 1;
            } else sfx(61);
        }
        frame();
    }
}

// ------------------------------------------------------------------ bonus round question
void scene_ask(void) BANKED {
    static const canvas_t top = {cvbuf, 1, 0, 0, 1, 20, 3, 5, 0};
    static const canvas_t btn = {cvbuf, 70, 0, 4, 11, 12, 2, 5, 0};
    uint8_t cur = 0, redraw = 1;
    int16_t cx = 56 * 256;
    fadeout(1);
    reset_screen();
    set_bgpal(0, 0, 10, 2, 8);
    card_pals_load();
    set_bgpal(5, 0, 9, 2, 8);
    copy_objpals(0, 8, obj_pals);
    cv_begin(&top, 0);
    cv_cprint("winnings", 80, 2, 1);
    cv_bignumc(numstr(wining), 80, 9, 3, 1);
    cv_draw(&top);
    dlg_slide_in(40);
    dlg_setup(BOX_ROW, 1);
    dlg_say("how about a final\nbonus round? double\nwinnings or nothing?");
    screen_on();
    for (;;) {
        dlg_slide();
        dlg_tick();   // no "more" arrow here, as in the original (txtbuttx false)
        if (BTNP(J_LEFT)) { if (cur) sfx(63); cur = 0; redraw = 1; }
        else if (BTNP(J_RIGHT)) { if (!cur) sfx(63); cur = 1; redraw = 1; }
        if (redraw) {
            uint8_t i;
            cv_begin(&btn, 0);
            for (i = 0; i < 2; i++) {
                uint8_t c = cur == i ? 1 : 2, x = 12 + i * 32;
                cv_rrect(x, 2, 21, 11, c);
                cv_rect(x + 1, 3, x + 19, 11, 0);
                cv_cprint(i ? "no" : "yes", x + 11, 5, c);
            }
            cv_draw(&btn);
            redraw = 0;
        }
        {
            int16_t d = (cur ? 83 : 51) * 256 - cx;   // fingertip centred on the button
            cx += d - d / 3;
        }
        spr_put16(cx >> 8, 98, SPR_HAND, SPRPAL_HAND);
        if (BTNP(J_A | J_START)) {
            sfx(51);
            if (cur == 1) {
                scene = SC_WINING;
            } else {
                fadeout(1);
                bonusround();
                scene = SC_GAME;
            }
            return;
        }
        frame();
    }
}

// ------------------------------------------------------------------ winnings screen
// ------------------------------------------------------------------ winnings drip
// update_wining / draw_wining: two 1 px lines of blood (x 63/64) run between the number
// and the vial, each starting after its own delay and speeding up (0.05 px/frame^2), their
// tails following the same way; the drop at the source swells in, pulses between 1x and 2x
// its height and shrinks away. Drawn each frame into three 8x16 sprites (spare tiles
// 116-121), 40 px of the original's clip mapped onto the 42 px between number and vial.
#define DRIP_TILE 116
static const uint8_t drip_s64[64] = {   // (sin + 1) / 2, 1/256
    128, 141, 153, 165, 177, 188, 199, 209, 219, 227, 234, 241, 246, 250, 254, 255,
    255, 255, 254, 250, 246, 241, 234, 227, 219, 209, 199, 188, 177, 165, 153, 141,
    128, 115, 103, 91, 79, 68, 57, 47, 37, 29, 22, 15, 10, 6, 2, 1,
    0, 1, 2, 6, 10, 15, 22, 29, 37, 47, 57, 68, 79, 91, 103, 115
};
static const uint8_t drip_drop[7] = {0x7E, 0x3C, 0x3C, 0x18, 0x18, 0x18, 0x18};   // x 61-66
static int16_t dp_hd[2], dp_hv[2], dp_tl[2], dp_tv[2];   // heads / tails and speeds, 8.8 px
static uint16_t dp_h, dp_ph;                               // drop height (8.8), pulse phase

static void drip_start(void) {
    memset(dp_hv, 0, sizeof(dp_hv));
    memset(dp_tl, 0, sizeof(dp_tl));
    memset(dp_tv, 0, sizeof(dp_tv));
    dp_hd[0] = -(int16_t)((uint16_t)rnd(90) << 8);
    dp_hd[1] = 0;
    dp_h = 0;
}

static void drip_step(uint8_t phase) {
    uint8_t i;
    for (i = 0; i < 2; i++) {
        dp_hv[i] += 13;
        if (phase == 2) dp_tv[i] += 13;
        // past 100 px nothing changes; stop there before 8.8 overflows
        if (dp_hd[i] < (100 << 8)) dp_hd[i] += dp_hv[i];
        if (dp_tl[i] < (100 << 8)) dp_tl[i] += dp_tv[i];
    }
    if (phase == 1) dp_h += (256 - dp_h) / 50;
    else dp_h -= dp_h / 10;
    dp_ph += 273;   // a turn a second
}

static void drip_draw(uint8_t up) {
    uint8_t *b = (uint8_t *)(((uint16_t)cvbuf + 15) & ~15u) + 1024;   // past the canvases
    uint8_t i, r, n, bit;
    int16_t a, z;
    memset(b, 0, 96);
    for (i = 0, bit = 0x10; i < 2; i++, bit >>= 1) {
        if (dp_hd[i] <= 0) continue;
        a = dp_tl[i] > 0 ? dp_tl[i] >> 8 : 0;
        z = dp_hd[i] >> 8;
        if (z > 39) z = 39;
        if (a > z) continue;
        a = a * 21 / 20;              // original rows a..z cover these 42-row ones
        z = (z + 1) * 21 / 20 - 1;
        for (; a <= z; a++) b[((up ? 41 - a : a) << 1) + 1] |= bit;
    }
    // drop: 6x7 stretched to 7 * h * (1..2) px
    {   // (1 + pulse) * h, 8.8, in 16 bits (no 32-bit maths: its library would land in bank 0)
        uint16_t m = (uint16_t)((256 + drip_s64[(dp_ph >> 8) & 63]) >> 1) * (dp_h >> 1) >> 6;
        m *= 7;
        n = (uint8_t)((m + m / 20) >> 8);
    }
    if (n > 14) n = 14;
    for (r = 0; r < n; r++)
        b[((up ? 41 - r : r) << 1) + 1] |= drip_drop[r * 7 / n];
    vram_dma_vbl(b, (uint8_t *)0x8000u + (DRIP_TILE << 4), 0, 6, _current_bank);
    for (i = 0; i < 3; i++) spr_put(76, 64 + i * 16, DRIP_TILE + i * 2, SPRPAL_DRIP);
}

void scene_wining(void) BANKED {
    static const canvas_t txt = {cvbuf, 100, 0, 2, 4, 16, 3, 4, 0};
    int16_t wint = 200, vlev = -1;
    uint8_t winphase = 0, dropup, redraw = 1, rbar = 1, over = 0, turn = 0, dyw = 255, dyg = 255, n;
    // wintxth / govertxth: each text rises 18 px into its clip (/10 a frame) and sinks out
    // again; 24 px canvas here. 8.8, 18 * 256 = fully up.
    uint16_t wth = 0, gth = 0, wtd = 18 * 256, gtd = 0;
    const char *g1 = 0, *g2 = 0;
    fadeout(1);
    music(-1);
    if (wining > high[mnucur]) high[mnucur] = wining;
    dropup = wining < 0;
    reset_screen();
    rings_setup(1);
    set_bgpal(4, 0, 9, 8, 2);   // 3: the separator's purple
    bar_setup();
    copy_objpals(0, 8, obj_pals);
    WY_REG = 104;
    win_show = 1;
    // bar sits at window rows 0..4 here
    blood_txt = blood;
    map_fill(1, 0, 0, 20, 5, 0, 5);
    {
        static const canvas_t vial = {cvbuf, 150, 0, 8, 0, 4, 3, 5, 1};
        static const canvas_t bar2 = {cvbuf, 162, 0, 0, 3, 20, 2, 5, 1};
        rings_update();
        screen_on();
        for (;;) {
            ticksfx = 54;
            update_gtxt();
            rings_update();
            wint--;
            // While the drip runs both numbers tick every frame: redraw one canvas a frame,
            // in turn, and the vial only when its level moves, or the frame rate halves.
            if (gtxt_changed & 2) redraw = 1;
            if (gtxt_changed & 4) rbar = 1;
            gtxt_changed = 0;
            wth += ((int16_t)(wtd - wth)) / 10;
            gth += ((int16_t)(gtd - gth)) / 10;
            n = (uint8_t)((uint16_t)(18 * 256 - wth) * 4 / 768);   // 24 px at 0, 0 at 18 px
            if (n != dyw) { dyw = n; redraw = 1; }
            n = (uint8_t)((uint16_t)(18 * 256 - gth) * 4 / 768);
            if (n != dyg) { dyg = n; redraw = 1; }
            if (redraw && rbar) turn ^= 1;
            if (redraw && (!rbar || turn)) {
                cv_begin(&txt, 0);
                if (dyw < 24) {
                    cv_cprint(losses_desc ? "losses" : "winnings", 64, 2 + dyw, 1);
                    cv_bignumc(numstr(wining_txt), 64, 9 + dyw, 2, 1);
                }
                if (over && dyg < 24) {
                    cv_cprint(g2, 64, 2 + dyg, 1);
                    cv_bignumc(g1, 64, 9 + dyg, 2, 0);
                }
                {   // sepline: purple line, dotted ends ("aa2a2a", plum shown black)
                    static const uint8_t cap[6] = {0, 0, 3, 0, 3, 0};
                    uint8_t x;
                    cv_hline(6, 121, 23, 3);
                    for (x = 0; x < 6; x++) {
                        cv_pset(x, 23, cap[x]);
                        cv_pset(127 - x, 23, cap[x]);
                    }
                }
                cv_draw(&txt);
                redraw = 0;
            }
            else if (rbar) {
                rbar = 0;
                if (vial_level(blood_txt) != vlev) {
                    vlev = vial_level(blood_txt);
                    cv_begin(&vial, 0);
                    draw_vial(&vial, 7, 3, blood_txt);
                    cv_draw(&vial);
                }
                cv_begin(&bar2, 0);
                cv_bignumc(numstr(blood_txt), 80, 3, 1, 1);
                cv_draw(&bar2);
            }
            if (winphase == 0) {
                if (wint <= 0) {
                    if (!scoremode) {
                        blood += wining;
                        if (blood < 0) blood = 0;
                        if (blood > 5000) blood = 5000;
                    }
                    savegame();
                    if (scoremode) { winphase = 4; wint = 200; }
                    else if (wining == 0) winphase = 3;
                    else { wining = 0; winphase = 1; drip_start(); }
                }
            } else if (winphase == 1) {
                drip_step(1);
                if (wining_txt == 0) {
                    winphase = 2;
                    dp_tl[1] = -(int16_t)((uint16_t)rnd(90) << 8);
                }
            } else if (winphase == 2) {
                drip_step(2);
                if (dp_tl[0] > (90 << 8) && dp_tl[1] > (90 << 8)) winphase = 3;
            } else if (winphase == 3) {
                if (blood <= 0) {
                    winphase = 5; wint = 400;
                    g2 = "exsanguination"; g1 = "you died";
                    chain = 0;
                    music(9);
                } else if (blood >= 5000) {
                    winphase = 5; wint = 400;
                    g2 = "congratulations"; g1 = "you won";
                    chain++;
                    if (chain > bchain) bchain = chain;
                    music(10);
                } else {
                    winphase = 4; wint = 200;
                }
            } else if (winphase == 4) {
                if (wint <= 0) wtd = 0;   // the text sinks, then the menu comes down
                if (!wtd && wth < 128) {
                    fadeout(1);
                    music(0);
                    menu_from = -91;
                    scene = SC_MENU;
                    ticksfx = 0;
                    return;
                }
            } else if (winphase == 5) {
                if (!over) { over = 1; redraw = 1; wint = 120; wtd = 0; gtd = 18 * 256; }
                if (wint <= 0) {
                    savegame();
                    fadeout(1);
                    scene = SC_END;
                    ticksfx = 0;
                    goverprint_set(g1, g2);
                    return;
                }
            }
            if (winphase == 1 || winphase == 2) drip_draw(dropup);
            if (BTNP(J_A | J_B | J_START)) {
                wint = 0;
                blood_txt = blood;
                wining_txt = wining;
                gtxt_changed = 6;
                rbar = 1;
                wth = wtd;
                gth = gtd;
                if (winphase == 1 || winphase == 2) { winphase = 3; }
            }
            frame();
        }
    }
}

// ------------------------------------------------------------------ ending
static const char *eg1, *eg2;
void goverprint_set(const char *g1, const char *g2) BANKED { eg1 = g1; eg2 = g2; }

void scene_end(void) BANKED {
    static const canvas_t txt = {cvbuf, 100, 0, 2, 3, 16, 3, 0, 0};
    uint16_t wint = 1000, th = 0;
    uint8_t died = eg2[0] == 'e', tdy = 255;
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
    screen_on();
    while (wint--) {
        // govertxth: the text rises 18 px into its clip (/10 a frame); 24 px canvas here
        uint8_t dy = (uint8_t)((uint16_t)(18 * 256 - th) * 24 / (18 * 256));
        if (dy != tdy) {
            tdy = dy;
            cv_begin(&txt, 0);
            cv_cprint(eg2, 64, 2 + dy, 1);
            cv_bignumc(eg1, 64, 9 + dy, 2, 0);
            cv_draw(&txt);
        }
        th += (18 * 256 - th) / 10;
        frame();
        if (BTNP(J_A | J_START)) break;
    }
    savegame();
    fadeout(0);
    wait_frames(120);
    music(0);
    scene = SC_START;
}

void gotowin(void) BANKED { scene = SC_WINING; }
