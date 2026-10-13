// Non-gameplay scenes: credits, title, intro, opponent menu, bonus ask, winnings, ending.
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include <rand.h>
#include "common.h"
#include "ripple.h"

// kept in this file: string literals live in the same ROM bank as their users
const char *const vname[4] = {"bafur", "houkin", "zver", "orlok"};
const char *const vquote[4] = {"'i don't bite'", "'rent is due'", "'zamorit chervichka'", "'blood is life!'"};
const uint8_t vmult[4] = {1, 5, 10, 100};
const int16_t vcost[4] = {0, 50, 200, 1000};
const uint8_t vtoken[4] = {2, 1, 0, 0};


static const int8_t sintab[32] = {0, 1, 1, 2, 2, 3, 3, 3, 3, 3, 3, 3, 2, 2, 1, 1,
                                  0, -1, -1, -2, -2, -3, -3, -3, -3, -3, -3, -3, -2, -2, -1, -1};

// ------------------------------------------------------------------ rings (title / menu / winnings)
// docircs: rings 10 px apart grow 0.29 px a frame (the original's 0.26 on its 9 px spacing),
// each born where the logo or the chosen card is and drifting to the centre as it grows.
// Each 1 px step is a pre-drawn frame (gen_assets: up to 131 tiles, mirrored left-right by
// the map) for the scene's centre offset. The frames go by DMA into whichever of the two
// copies of the ring tiles (0x8000 / 0x9000: 128 in VRAM bank 1, the rest in bank 0's
// 124-127) is hidden, then frame() switches LCDC.4 to show it: a step never shows half drawn. Palette z holds the colours of ring zones z..z+2 (by
// radius), as the original's circcols. Everything else on these screens uses BG tiles
// 128-255 (the same in both modes), or tile 0 of bank 1 for blank.
#define RINGS_TITLE 0
#define RINGS_WIN 1
#define RINGS_MENU 2
static uint8_t ring_base, ring_nt, ring_nx, ring_shown, ring_pend, ring_mb, ring_ab;
static const uint8_t *ring_mapp, *ring_attrp;
static uint16_t ring_t;
// ring colours by zone (radius), the original's circcols1 / circcols2
static const uint8_t ring_cols1[6] = {3, 10, 2, 9, 9, 9}, ring_cols2[6] = {3, 3, 10, 10, 10, 10};

// frame f: 8 a bank, 128 tiles at 0x4000 + 2048 i; its extra 4 tiles at 64 f in RING_XBANK
static const uint8_t *ring_src(uint8_t f, uint8_t *bank) {
    *bank = RING_BANK0 + (f >> 3);
    return (const uint8_t *)(0x4000u + ((uint16_t)(f & 7) << 11));
}
#define RING_EXTRA(f) ((const uint8_t *)0x4000u + ((uint16_t)(f) << 6))
#define RING_XTILE 124   // bank 0

// palette z: the colours of zones z, z + 1, z + 2
static void ring_pals(const uint8_t *c) {
    uint8_t z;
    for (z = 0; z < 6; z++)
        set_bgpal(z, 0, c[z], c[z < 5 ? z + 1 : 5], c[z < 4 ? z + 2 : 5]);
}

static void rings_setup(uint8_t sc) {
    uint8_t r, t[20], at[20], bank;
    const uint8_t *src;
    if (sc == RINGS_WIN) {
        ring_mapp = ring_map_win; ring_attrp = ring_attr_win;
        ring_mb = BANK(ring_map_win); ring_ab = BANK(ring_attr_win);
        ring_base = RING_WIN; ring_nt = RING_NT_WIN; ring_nx = RING_NX_WIN;
    } else {   // the title and the menu share the map
        ring_mapp = ring_map_title; ring_attrp = ring_attr_title;
        ring_mb = BANK(ring_map_title); ring_ab = BANK(ring_attr_title);
        ring_base = sc == RINGS_MENU ? RING_MENU : RING_TITLE;
        ring_nt = RING_NT_TITLE; ring_nx = RING_NX_TITLE;
    }
    map_fill(0x80, 0, 0, 32, 18, 0, 0x08);   // blank: tile 0 of bank 1, in both copies
    map_fill(0x81, 0, 0, 32, 18, 0, 0x08);
    for (r = 0; r < 13; r++) {
        banked_copy(t, ring_mb, ring_mapp + r * 20, 20);
        banked_copy(at, ring_ab, ring_attrp + r * 20, 20);
        map_put(0, 0, r, 20, 1, t, at);
    }
    ring_pals(sc == RINGS_TITLE ? ring_cols1 : ring_cols2);
    ring_shown = ring_base;
    ring_pend = 255;
    ring_t = 0;
    ring_vram = 1;
    src = ring_src(ring_base, &bank);
    load_bkg_banked(bank, src, 0, ring_nt, 1);   // 0x9000
    load_spr_banked(bank, src, 0, ring_nt, 1);   // 0x8000
    if (ring_nx) {
        load_bkg_banked(RING_XBANK, RING_EXTRA(ring_base), RING_XTILE, ring_nx, 0);
        load_spr_banked(RING_XBANK, RING_EXTRA(ring_base), RING_XTILE, ring_nx, 0);
    }
}

// set: the title's frame set (its logo bob + 3; 7-9 the move to the menu), 0 elsewhere
static void rings_update(uint8_t set) {
    uint8_t s, f, n, bank;
    const uint8_t *src;
    uint8_t *dst;
    ring_t += 74;   // 0.29 px a frame, in 1/256
    s = (uint8_t)(ring_t >> 8);
    if (s >= RING_STEPS) {
        ring_t -= (uint16_t)RING_STEPS << 8;
        s -= RING_STEPS;
    }
    if (ring_flip) return;   // shown at the next VBlank
    if (ring_pend != 255) {   // uploading
        if (vram_dma_busy()) return;
        ring_shown = ring_pend;
        ring_pend = 255;
        ring_flip = 1;
        return;
    }
    f = ring_base + set * RING_STEPS + s;
    if (f == ring_shown || vram_dma_busy() > 7) return;   // (queued behind any other jobs; 12 fit)
    src = ring_src(f, &bank);
    dst = (uint8_t *)((LCDC_REG & LCDCF_BG8000) ? 0x9000u : 0x8000u);
    n = ring_nt >> 1;   // two jobs: each fits a VBlank with room to spare
    vram_dma_vbl(src, dst, 1, n, bank);
    vram_dma_vbl(src + ((uint16_t)n << 4), dst + ((uint16_t)n << 4), 1, ring_nt - n, bank);
    if (ring_nx) vram_dma_vbl(RING_EXTRA(f), dst + (RING_XTILE << 4), 0, ring_nx, RING_XBANK);
    ring_pend = f;
}

// A canvas redrawn while the screen runs (already placed, tiles 128+): its tiles go by DMA
// at VBlank, from its own spot in cvbuf (off), so the next canvas drawn can't change them
// before they go. Copying with the CPU waits on HBlanks for most of a frame.
static void cv_dma(const canvas_t *c, uint16_t off) {
    uint8_t n = c->w * c->h;
    uint8_t *d = (uint8_t *)(((uint16_t)cvbuf + 15) & ~15u) + off;
    memcpy(d, cvbuf, (uint16_t)n << 4);
    vram_dma_vbl(d, (uint8_t *)0x8000u + ((uint16_t)c->tile << 4), c->bank, n, _current_bank);
}

// place a canvas (just drawn, BG map, in the ring area) over the rings: its cells with ink
// (or those set in sel), the rings in the others, so the rings show around the text
static void cv_place_rings(const canvas_t *c, const uint8_t *sel) {
    uint8_t r, i, k, t[20], at[20], a = c->pal | (c->bank ? 8 : 0);
    const uint8_t *p = c->buf;
    uint16_t o;
    for (r = 0; r < c->h; r++) {
        o = (uint16_t)(c->y + r) * 20 + c->x;
        banked_copy(t, ring_mb, ring_mapp + o, c->w);
        banked_copy(at, ring_ab, ring_attrp + o, c->w);
        for (i = 0; i < c->w; i++, p += 16) {
            k = sel ? *sel++ : tile_ink(p);
            if (k) {
                t[i] = c->tile + r * c->w + i;
                at[i] = a;
            }
        }
        map_put(0, c->x, c->y + r, c->w, 1, t, at);
    }
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

#define LOGO_SPR 128   // OBJ bank 1, past the ring tiles at 0x8000
#define MENU_SPR 208   // OBJ bank 1: the menu's cards, past the ring tiles, the logo and the text
static void title_to_menu(const canvas_t *cvs, int8_t sy0);
// the menu's bar, also drawn by the title for title_to_menu: bank 1 past the logo (the
// title's own tiles fill bank 0); BG priority (0x80): the cards pass behind it
static const canvas_t CV_VIAL = {cvbuf, 154, 1, 8, 0, 4, 3, 0x87, 1};
static const canvas_t CV_BAR2 = {cvbuf, 166, 1, 0, 3, 20, 2, 0x87, 1};
static void bar_draw_cv(const canvas_t *vial, const canvas_t *bar2, uint8_t show_streak);
static void menu_text_draw(void);

static void draw_logo(int16_t cx, int16_t cy) {
    uint8_t i;
    const uint8_t *p = logo_parts;
    for (i = 0; i < N_LOGO_PARTS; i++, p += 4)
        spr_put(cx - 20 + p[0], cy - 24 + p[1], LOGO_SPR + p[2], (p[3] + 4) | 0x08);   // OBJ palettes 4-6
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
    static const canvas_t bar = {cvbuf, 128, 0, 0, 13, 20, 5, 6, 0};   // palettes 0-5: the rings
    static const canvas_t cvs = {cvbuf, 228, 0, 5, 12, 10, 1, 7, 0};
    // the options sit 50 px apart on a strip that slides (stmnux, /5 a frame) in a clip
    static const canvas_t opt = {cvbuf, 238, 0, 7, 15, 6, 2, 6, 0};   // 48 px, as the original's
    uint8_t stcur = 0, redraw = 1, flash = 0, abob = 0, i;
    int16_t sx = 0, sxd = 255;
    uint16_t bx = 0, by = 0;
    nst = 0;
    if (blood > 0 && blood < 5000) stmnu[nst++] = "continue";
    stmnu[nst++] = "new game";
    if (bchain > 0) stmnu[nst++] = "score mode";

    reset_screen();
    rings_setup(RINGS_TITLE);
    load_spr_banked(BANK(logo_tiles), logo_tiles, LOGO_SPR, N_LOGO_TILES, 1);
    copy_objpals(4, 3, logo_pals);   // 0-3: the menu's cards, ready for title_to_menu
    load_spr_banked(BANK(menu_spr_tiles), menu_spr_tiles, MENU_SPR, 48, 1);
    map_fill(1, 0, 0, 20, 6, 0, 0x0F);   // the menu's bar, hidden until title_to_menu
    blood_txt = blood;
    bar_draw_cv(&CV_VIAL, &CV_BAR2, 1);   // (redrawn without the streak for score mode)
    scoremode = 0;
    menu_text_draw();   // the menu's text for "continue", ready for title_to_menu
    set_bgpal(6, 8, 0, 7, 9);
    set_bgpal(7, 0, 14, 14, 14);
    set_objpal(3, 7, 7, 7);
    cv_begin(&cvs, 0);
    cv_cprint(chaintxt(), 40, 2, 1);
    cv_flush(&cvs);
    cv_place_rings(&cvs, 0);   // over the rings, which show where it has no ink
    rings_update(3);
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
        rings_update(sintab[(by >> 8) & 31] + 3);   // the logo's bob
#ifdef DEBUG_FPS
        if ((frames & 63) == 0) redraw = 1;
#endif
        {
            uint8_t f = ((frames >> 3) & 1);
            if (f != flash) {
                flash = f;
                bgpal[6 * 4 + 2] = pico[f ? 15 : 7];
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
            } else {   // the menu builds over the title (draw_menu while circtme > 0)
                scoremode = stmnu[stcur][0] == 's';
                title_to_menu(&cvs, sintab[(by >> 8) & 31]);
                scene = SC_MENU;
                return;
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
// BG priority (0x80) on the text and the bar: the cards pass behind them, over the rings
static const canvas_t CV_MTXT = {cvbuf, 160, 0, 0, 8, 20, 4, 0x86, 0};

static void bar_draw_cv(const canvas_t *vial, const canvas_t *bar2, uint8_t show_streak) {
    cv_begin(vial, 0);
    draw_vial(vial, 7, 3, blood_txt);
    cv_draw(vial);
    cv_begin(bar2, 0);
    cv_bignumc(numstr(blood_txt), 80, 3, 1, 1);
    if (show_streak) {
        cv_print(chaintxt(), 2, 2, 2);
        s_begin(); s_str("game:"); s_num(tries);
        cv_print(s_end(), 2, 9, 2);
    }
    cv_draw(bar2);
}

static uint8_t bar_placed;

static void bar_draw(uint8_t show_streak) {
    if (!bar_placed) {   // the screen is black (scene_menu's setup)
        bar_draw_cv(&CV_VIAL, &CV_BAR2, show_streak);
        bar_placed = 1;
        return;
    }
    cv_begin(&CV_VIAL, 0);
    draw_vial(&CV_VIAL, 7, 3, blood_txt);
    cv_dma(&CV_VIAL, 1280);   // cvbuf: canvases drawn in 0-1279
    cv_begin(&CV_BAR2, 0);
    cv_bignumc(numstr(blood_txt), 80, 3, 1, 1);
    if (show_streak) {
        cv_print(chaintxt(), 2, 2, 2);
        s_begin(); s_str("game:"); s_num(tries);
        cv_print(s_end(), 2, 9, 2);
    }
    cv_dma(&CV_BAR2, 1472);
}

static void bar_setup(void) {
    set_bgpal(7, 8, 0, 9, 10);   // palettes 0-5: the rings
    map_fill(1, 0, 5, 20, 13, 0, 0x0F);   // blank: bank 1's tile 0 (rings_setup)
}

// ------------------------------------------------------------------ opponent menu
static uint8_t mflash;

#define MTXT_BUF ((uint8_t *)(((uint16_t)dlgbuf + 15) & ~15u))
static uint8_t menu_pend;   // canvases sent, to place once their tiles are in: 1 bar, 2 text

static void menu_text_draw(void) {   // into MTXT_BUF
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
    memcpy(MTXT_BUF, cvbuf, 80 * 16);
}

// by DMA (a CPU copy of 80 tiles stops the menu for frames), placed once it is in (menu_place)
static void menu_text_send(void) {
    vram_dma_vbl(MTXT_BUF, (uint8_t *)0x8000u + (160 << 4), 0, 80, _current_bank);
    menu_pend |= 2;
}

static void menu_text(void) {
    menu_text_draw();
    menu_text_send();
}

// ---- the opponent cards (update_menu): each drifts on its own, randx / randy =
// cos((t + i * 0.79) / 2 or 3) * 1.7, and rides an arc, y + cos((x - 64) / 160) * 5.
// Drawn as 8x8 sprites (menu_spr_tiles in OBJ bank 1, card palettes), over the rings and
// behind the text and the bar.
// cos tables (64 steps a turn), precomputed: 16-bit multiplies and divides per card cost
// the menu a quarter of its frames. mdrift = cos * 1.7, mdip = cos * 5 - 5, and mturn = dx
// * 5 / 12 (160 original px a turn)
static const int8_t mdrift[64] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    0, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -2, -2, -2, -2, -2,
    -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -1, -1, -1, -1, -1, -1,
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1
};
static const int8_t mdip[64] = {
    0, -1, -1, -1, -1, -1, -1, -2, -2, -2, -3, -3, -4, -4, -5, -5,
    -5, -5, -5, -6, -6, -7, -7, -8, -8, -8, -9, -9, -9, -9, -9, -9,
    -10, -9, -9, -9, -9, -9, -9, -8, -8, -8, -7, -7, -6, -6, -5, -5,
    -5, -5, -5, -4, -4, -3, -3, -2, -2, -2, -1, -1, -1, -1, -1, -1
};
static const uint8_t mturn[151] = {
    0, 0, 0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5, 5, 6, 6, 7, 7,
    7, 8, 8, 9, 9, 10, 10, 10, 11, 11, 12, 12, 12, 13, 13, 14, 14, 15, 15,
    15, 16, 16, 17, 17, 17, 18, 18, 19, 19, 20, 20, 20, 21, 21, 22, 22, 22, 23,
    23, 24, 24, 25, 25, 25, 26, 26, 27, 27, 27, 28, 28, 29, 29, 30, 30, 30, 31,
    31, 32, 32, 32, 33, 33, 34, 34, 35, 35, 35, 36, 36, 37, 37, 37, 38, 38, 39,
    39, 40, 40, 40, 41, 41, 42, 42, 42, 43, 43, 44, 44, 45, 45, 45, 46, 46, 47,
    47, 47, 48, 48, 49, 49, 50, 50, 50, 51, 51, 52, 52, 52, 53, 53, 54, 54, 55,
    55, 55, 56, 56, 57, 57, 57, 58, 58, 59, 59, 60, 60, 60, 61, 61, 62, 62
};

static void menu_cards(int16_t mxp, int16_t d) {
    // drift phases: (t + i * 0.79) / 2 and / 3 in 64ths of a turn, 8.8 (546 / 364 a frame)
    static uint16_t ax[4] = {0, 25887u, 51774u, 12125u}, ay[4] = {0, 17257u, 34514u, 51771u};
    uint8_t i, k, far = 0;
    int16_t fd = -1, c = 64 - mxp + 10 - 80;
    // three cards at most: a line crosses one sprite row of each card, 3 sprites a card,
    // and 10 a line is the hardware's limit; the card farthest from the centre is left out
    for (i = 0; i < 4; i++, c += 48) {
        int16_t e = c < 0 ? -c : c;
        if (e > fd) { fd = e; far = i; }
    }
    for (i = 0; i < 4; i++) {
        int16_t left, top, dx;
        ax[i] += 546;
        ay[i] += 364;
        if (i == far) continue;
        left = 64 + (int16_t)i * 48 - mxp + mdrift[(uint8_t)(ax[i] >> 10) & 63];
        dx = left + 10 - 80;
        if (dx < 0) dx = -dx;
        top = 16 + d + mdip[dx > 150 ? 63 : mturn[dx]] + mdrift[(uint8_t)(ay[i] >> 10) & 63];
        k = i * 12;
        spr_grid(left, top, MENU_SPR + k, menu_spr_pal + k, 0x08, 3, 4);   // OBJ bank 1
    }
}

// the arrows beside the selected card: BG (the sprite palettes are the cards'), bobbing
static const canvas_t CV_MARR = {cvbuf, 140, 0, 5, 3, 10, 2, 0x86, 0};

static uint8_t marr_placed, marr_vis;

static void menu_arrows(int8_t b) {
    cv_begin(&CV_MARR, 0);
    if (mnucur > 0) cv_putc('\x02', 52 - b - 40, 6, 2);
    if (mnucur < 3) cv_putc('\x03', 102 + b - 40, 6, 2);
    cv_dma(&CV_MARR, 2112);   // every few frames; past the menu's other canvases in cvbuf
}

// the menu's canvases go on screen once their tiles are in VRAM
static void menu_place(void) {
    uint8_t v = (mnucur > 0) | ((mnucur < 3) << 1), i;
    static uint8_t sel[20];
    if (vram_dma_busy()) return;
    if (menu_pend & 1) {
        cv_place(&CV_VIAL);
        cv_place(&CV_BAR2);
    }
    if (menu_pend & 2) {
        canvas_t c = CV_MTXT;
        c.buf = MTXT_BUF;
        cv_place_rings(&c, 0);
    }
    menu_pend = 0;
    if (v != marr_vis && marr_placed) {   // each arrow stays in its 2x2 cells as it bobs: those
        marr_vis = v;                     // show the canvas, the rest rings
        for (i = 0; i < 20; i++) {
            uint8_t x = i < 10 ? i : i - 10;
            sel[i] = ((v & 1) && (x == 1 || x == 2)) || ((v & 2) && (x == 7 || x == 8));
        }
        cv_place_rings(&CV_MARR, sel);
    }
    marr_placed = 1;   // (the arrows' first tiles are in by the next call)
}

// mnuy: the menu slides up into place from below (from the title or the intro), or down
// from above after the winnings screen, easing 1/20 a frame; the bar stays
int16_t menu_from = 69;
static uint8_t menu_warm;   // title_to_menu left the menu on screen

// The menu builds over the title (the original's draw_menu while circtme > 0, 40 frames):
// the logo rises away, the cards rise from behind the title's bar, the menu's bar rises over
// it, the rings recolour from the middle out (a bright edge, circcols3) as their centre moves
// to the chosen card. Then scene_menu takes over the screen as it is (menu_warm).
// The logo is 8x16 sprites and the cards 8x8: the LCD interrupt switches between them.
static void title_to_menu(const canvas_t *cvs, int8_t sy0) {
    static const uint8_t zlo[6] = {0, 15, 26, 41, 60, 80}, zhi[6] = {15, 26, 41, 60, 80, 110};
    static const uint8_t blank[10] = {0};
    int16_t my = 69 << 8, dy = (int16_t)sy0 << 8, cy, mxp = (int16_t)mnucur * 48 - 4;
    uint8_t z, rb, set, nt = 0, cols[6], shown[6] = {255}, flash = 255, f;   // nt: frames, 40 = circtme 0
    cv_place_rings(cvs, blank);   // no streak line on the menu
    set_bgpal(7, 8, 0, 9, 10);    // the bar's colours (the streak's before)
    if (scoremode) {   // the title drew the bar with the streak, and the text for "continue"
        menu_text_draw();
        cv_begin(&CV_BAR2, 0);
        cv_bignumc(numstr(blood_txt), 80, 3, 1, 1);
        cv_dma(&CV_BAR2, 1792);   // past the title's canvases in cvbuf
    }
    copy_objpals(0, 4, card_pals);
    WY_REG = 144;
    win_show = 1;
    cut_ly = 104;   // the cards stay behind the bars
    spr_split = 1;
    for (;;) {
        if (nt < 40) nt++;
        my -= my / 20;
        if (my < 256) my = 0;   // the last pixel: the menu's cards are still
        // the rings' centre moves to the card, 0.5 px a frame (the title's sets, then 7-9)
        if (dy > -14 * 256) dy -= 141;
        set = dy >= -896 ? (uint8_t)(((dy + 128) >> 8) + 3) : dy >= -9 * 256 ? 7 : dy >= -3200 ? 8 : 9;
        rings_update(set);
        // switched from the middle out, the zone at the edge bright
        rb = (uint8_t)(((uint16_t)nt * 11) >> 2);   // 0..110 px
        for (z = 0; z < 6; z++)
            cols[z] = rb >= zhi[z] ? ring_cols2[z] : rb > zlo[z] || (!z && rb) ? 15 : ring_cols1[z];
        for (z = 0; z < 6 && cols[z] == shown[z]; z++);
        if (z < 6) {
            memcpy(shown, cols, 6);
            ring_pals(cols);
        }
        // the menu's bar rises 40 px * (1 - (circtme / 100)^2)
        z = 40 - nt;   // circtme 0..40
        WY_REG = 104 + (uint8_t)(((((uint16_t)z * z) >> 3) * 205) >> 10);   // z * z / 40
        // the logo rises with the cards, and 20 px more
        cy = 50 + sy0 + (my >> 8) - 69 - (nt >> 1);
        if (cy > -22) {   // (its split line kept off line 0: LYC 0 also matches during line 153)
            draw_logo(80, cy);
            spl_ly = (uint8_t)(cy + 24);
        } else spl_ly = 0;
        menu_cards(mxp, my >> 8);
        f = (frames >> 3) & 1;   // the title's options keep flashing under the bar
        if (f != flash) {
            flash = f;
            bgpal[6 * 4 + 2] = pico[f ? 15 : 7];
            pal_dirty = 1;
        }
        if (nt == 39) menu_text_send();   // the menu's text, while the cards settle (placed by scene_menu)
        frame();
        if (nt == 40 && !my && !spl_ly && set == 9) break;
    }
    menu_warm = 1;
}

void scene_menu(void) BANKED {
    uint8_t redraw = 1;
    int8_t ab = 99;
    int16_t mx, mtarget, my = menu_from << 8, d;
    menu_from = 69;
    if (menu_warm) {   // from the title: rings, cards and bar are up; the rest goes on top
        menu_warm = 0;
        my = 0;
        bar_placed = 1;    // the title's bar is the menu's; its text is in VRAM (menu_pend 2)
        redraw = 0;
        ring_base = RING_MENU;   // the same frames as the title's last set
        set_bgpal(6, 0, 8, 9, 7);
    } else {
        reset_screen();
        rings_setup(RINGS_MENU);
        set_bgpal(6, 0, 8, 9, 7);
        copy_objpals(0, 4, card_pals);
        load_spr_banked(BANK(menu_spr_tiles), menu_spr_tiles, MENU_SPR, 48, 1);
        SPRITES_8x8;   // reset_screen() sets 8x16 again
        bar_setup();
        map_fill(1, 0, 0, 20, 5, 0, 0x8F);
        map_fill(0, 0, 13, 32, 19, 0, 0x08);   // the slide scrolls these rows into view
        cut_ly = 104;   // the cards slide in behind the bar
        spr_split = 1;
        bar_placed = 0;
        menu_pend = 0;
    }
    mtarget = (int16_t)mnucur * 48 - 4;
    mx = mtarget * 16;
    WY_REG = 104;
    win_show = 1;
    blood_txt = blood;
    if (redraw) {   // (from the title: both up already)
        bar_draw(!scoremode);
        menu_text();
    }
    mflash = 0;
    marr_placed = 0;
    marr_vis = 255;
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
        menu_place();
        mtarget = (int16_t)mnucur * 48 - 4;
        mx += (mtarget * 16 - mx) / 10;
        my -= my / 20;
        if (my > -128 && my < 128) my = 0;
        d = my >> 8;
        scy = (uint8_t)-d;
        rings_update(0);
        menu_cards(mx >> 4, d);
        {
            uint8_t f = (frames >> 3) & 1;
            if (f != mflash) {
                mflash = f;
                bgpal[6 * 4 + 3] = pico[f ? 15 : 7];
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
    uint8_t *b = (uint8_t *)(((uint16_t)cvbuf + 15) & ~15u) + 768;   // past the canvases (scene_wining)
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
    static const canvas_t txt = {cvbuf, 202, 0, 2, 4, 16, 3, 6, 0};
    int16_t wint = 200, vlev = -1;
    uint8_t winphase = 0, dropup, redraw = 1, rbar = 1, over = 0, turn = 0, dyw = 255, dyg = 255, n, placed = 0;
    // wintxth / govertxth: each text rises 18 px into its clip (/10 a frame) and sinks out
    // again; 24 px canvas here. 8.8, 18 * 256 = fully up.
    uint16_t wth = 0, gth = 0, wtd = 18 * 256, gtd = 0;
    const char *g1 = 0, *g2 = 0;
    fadeout(1);
    music(-1);
    if (wining > high[mnucur]) high[mnucur] = wining;
    dropup = wining < 0;
    reset_screen();
    rings_setup(RINGS_WIN);
    set_bgpal(6, 0, 9, 8, 2);   // 3: the separator's purple (palettes 0-5: the rings)
    bar_setup();
    copy_objpals(0, 8, obj_pals);
    WY_REG = 102;   // 2 px up for the ripple, everything in it drawn 2 px lower
    win_show = 1;
    // bar sits at window rows 0..4 here
    blood_txt = blood;
    map_fill(1, 0, 0, 20, 6, 0, 0x0F);   // blank: bank 1's tile 0 (rings_setup)
    {
        static const canvas_t vial = {cvbuf, 150, 0, 8, 0, 4, 3, 7, 1};
        static const canvas_t bar2 = {cvbuf, 162, 0, 0, 3, 20, 2, 7, 1};
        uint8_t wh = 0, rip = 0;
        strip = (uint8_t *)(((uint16_t)dlgbuf + 15) & ~15u);
        vlev = vial_level(blood_txt);
        cv_begin(&vial, 0);
        cv_rect(0, 0, 31, 1, 1);
        draw_vial(&vial, 7, 5, blood_txt);
        memcpy(strip + 192, cvbuf, 64);
        cv_draw(&vial);
        cv_begin(&bar2, 0);
        cv_bignumc(numstr(blood_txt), 80, 5, 1, 1);
        cv_draw(&bar2);
        placed = 6;
        {   // window row 0: the edge tile, the strip over x 32-127
            static const uint8_t edge[16] = {0xFF, 0, 0xFF, 0};
            uint8_t row[20], at[20], i;
            bkg_tiles_upload(EDGE_TILE, 1, edge);
            for (i = 0; i < 20; i++) {
                row[i] = i < 4 || i >= 16 ? EDGE_TILE : STRIP_TILE + i - 4;
                at[i] = 7;
            }
            map_put(1, 0, 0, 20, 1, row, at);
        }
        strip_draw(0);
        rings_update(0);
        screen_on();
        for (;;) {
            ticksfx = 54;
            update_gtxt();
            rings_update(0);
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
                if (placed & 1) cv_dma(&txt, 864);   // cvbuf: canvases drawn in 0-767, drip 768
                else { cv_draw(&txt); placed |= 1; }
                redraw = 0;
            }
            else if (rbar) {
                rbar = 0;
                if (vial_level(blood_txt) != vlev) {
                    vlev = vial_level(blood_txt);
                    cv_begin(&vial, 0);
                    cv_rect(0, 0, 31, 1, 1);
                    draw_vial(&vial, 7, 5, blood_txt);
                    memcpy(strip + 192, cvbuf, 64);   // its top row shows in the strip
                    rip |= 2;
                    cv_dma(&vial, 1632);
                }
                cv_begin(&bar2, 0);
                cv_bignumc(numstr(blood_txt), 80, 5, 1, 1);
                cv_dma(&bar2, 1824);
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
            if (winphase >= 1 && !dropup) {   // drpwh: up while the drip reaches the bar, down as it stops
                if (dp_tl[0] > 0 || dp_tl[1] > 0) wh = wh > 2 ? wh - 2 : 0;
                else if ((dp_hd[0] >= (89 << 8) || dp_hd[1] >= (89 << 8)) && wh < 100) wh++;
            }
            if (wh || rip) {   // the last frame of a ripple draws the edge flat again
                strip_draw(wh);
                rip = wh ? 1 : 0;
            }
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
