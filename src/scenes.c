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
#define CVB ((uint8_t *)(((uint16_t)cvbuf + 15) & ~15u))   // cvbuf, 16-byte aligned (2545 bytes)
#define RINGS_TITLE 0
#define RINGS_WIN 1
#define RINGS_MENU 2
static uint8_t ring_base, ring_nt, ring_nx, ring_shown, ring_pend, ring_mb, ring_ab;
static uint8_t ring_force, ring_comp, ring_half;   // send the frame again (new text); merge the menu's text
static uint8_t *ring_cdst;
static void menu_comp(uint8_t f, uint8_t from);
static uint8_t mc_place_at;
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

// a row of the ring map; fold: the menu's palettes 3-5 into 2 (the same colours there)
static void ring_row(uint8_t r, uint8_t fold) {
    uint8_t t[20], at[20], i;
    banked_copy(t, ring_mb, ring_mapp + r * 20, 20);
    banked_copy(at, ring_ab, ring_attrp + r * 20, 20);
    if (fold)
        for (i = 0; i < 20; i++)
            if ((at[i] & 7) > 2) at[i] = (at[i] & ~7) | 2;
    map_put(0, 0, r, 20, 1, t, at);
}

static void rings_setup(uint8_t sc) {
    uint8_t r, bank;
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
    for (r = 0; r < 13; r++) ring_row(r, sc == RINGS_MENU);
    ring_pals(sc == RINGS_TITLE ? ring_cols1 : ring_cols2);
    ring_shown = ring_base;
    ring_pend = 255;
    ring_t = 0;
    ring_force = ring_comp = ring_half = 0;
    ring_vram |= 1;
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
        if (ring_half) {      // the menu's text: the second half of its cells, then send them all
            ring_half = 0;
            menu_comp(ring_pend, 1);
            vram_dma_vbl(CVB, ring_cdst, 0, 56, _current_bank);
            if (mc_place_at == 3) mc_place_at = 2;
            return;
        }
        if (vram_dma_busy()) return;
        ring_shown = ring_pend;
        ring_pend = 255;
        ring_flip = 1;
        if (mc_place_at == 2) mc_place_at = 1;
        return;
    }
    f = ring_base + set * RING_STEPS + s;
    if ((f == ring_shown && !ring_force) || vram_dma_busy() > 7) return;   // (behind any other jobs; 12 fit)
    ring_force = 0;
    src = ring_src(f, &bank);
    dst = (uint8_t *)((LCDC_REG & LCDCF_BG8000) ? 0x9000u : 0x8000u);
    n = ring_nt >> 1;   // two jobs: each fits a VBlank with room to spare
    vram_dma_vbl(src, dst, 1, n, bank);
    vram_dma_vbl(src + ((uint16_t)n << 4), dst + ((uint16_t)n << 4), 1, ring_nt - n, bank);
    if (ring_nx) vram_dma_vbl(RING_EXTRA(f), dst + (RING_XTILE << 4), 0, ring_nx, RING_XBANK);
    if (ring_comp) {   // the menu's text over this frame (bank 0's tiles 1-56), half a frame each
        menu_comp(f, 0);
        ring_half = 1;
        ring_cdst = dst + 16;
    }
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

// the "next" arrow (PICO-8's down glyph): down a pixel two thirds of the time,
// abs(sin(t / 2)) >= 0.5 as the original's flr(y + 22.5 + abs(sin(time() / 2)))
void dlg_arrow(uint8_t down) BANKED {
    int16_t x = cv_dlg.w * 8 - 12, y = 21;
    cv_select(&cv_dlg);
    cv_rect(x, y, x + 6, y + 5, 0);
    cv_putc('\x04', x, y + down, 1);
    dlg_flush_at(x, y);
    dlg_flush_at(x, y + 5);
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
// title's own tiles fill bank 0); BG priority (0x80): the cards pass behind it. Drawn in
// place in cvbuf (menu_bufs) and sent from there.
static canvas_t CV_VIAL = {0, 154, 1, 8, 0, 4, 3, 0x87, 1};
static canvas_t CV_BAR2 = {0, 166, 1, 0, 3, 20, 2, 0x87, 1};
static void menu_bufs(void) {
    CV_VIAL.buf = CVB + 1280;
    CV_BAR2.buf = CVB + 1472;   // (to 2111; the menu's merged cells 0-895, its arrows 2112-2431)
}
// a canvas drawn in place (16-byte aligned, placed already): its tiles by DMA at VBlank
static void cv_send(const canvas_t *c) {
    vram_dma_vbl(c->buf, (uint8_t *)0x8000u + ((uint16_t)c->tile << 4), c->bank, c->w * c->h, _current_bank);
}
static void bar_draw_cv(const canvas_t *vial, const canvas_t *bar2, uint8_t show_streak);
static void menu_text_draw(void);
static void menu_text_mask(void);

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
    menu_bufs();
    scoremode = 0;
    bar_draw_cv(&CV_VIAL, &CV_BAR2, 1);   // (redrawn bare for score mode)
    menu_text_draw();   // the menu's text for "continue", ready for title_to_menu
    menu_text_mask();
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
            cv_print("gb v2", 2, 33, 3);
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
            uint8_t f = (uint8_t)(frames % 20) < 10;   // sin(time() * 3) < 0: 20 frames
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
        {   // (always, as the original's txtbuttx: typing or not)
            uint8_t p = (uint8_t)(frames % 60), a = p >= 10 && p < 50;
            if (a != arrow) { arrow = a; dlg_arrow(a); }
        }
        if (BTNP(J_A | J_START)) {
            if (ti < 5) {
                ti++;
                dlg_say(talk[ti]);
                arrow = 255;
                if (ti == 1) { blood = 0; mult = 187; ticksfx = 54; }   // with "heheheh..."
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
// menu: text in BG rows 8-11 (moves with the cards) over the rings, the bar in the window
// from line 104
static canvas_t CV_MTXT = {0, 160, 0, 0, 8, 20, 4, 0, 0};   // (drawn in MTXT_BUF, merged: menu_comp)

// (score mode: the bar alone, as the original's drawblood draws nothing then)
static void bar_draw_cv(const canvas_t *vial, const canvas_t *bar2, uint8_t show_streak) {
    cv_begin(vial, 0);
    if (!scoremode) draw_vial(vial->buf, vial->w, 7, 3, blood_txt);
    cv_draw(vial);
    cv_begin(bar2, 0);
    if (!scoremode) cv_bignumc(numstr(blood_txt), 80, 3, 1, 1);
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
    if (!scoremode) draw_vial(CV_VIAL.buf, CV_VIAL.w, 7, 3, blood_txt);
    cv_send(&CV_VIAL);
    cv_begin(&CV_BAR2, 0);
    if (!scoremode) cv_bignumc(numstr(blood_txt), 80, 3, 1, 1);
    if (show_streak) {
        cv_print(chaintxt(), 2, 2, 2);
        s_begin(); s_str("game:"); s_num(tries);
        cv_print(s_end(), 2, 9, 2);
    }
    cv_send(&CV_BAR2);
}

static void bar_setup(void) {
    set_bgpal(7, 8, 0, 9, 10);   // palettes 0-5: the rings
    map_fill(1, 0, 5, 20, 13, 0, 0x0F);   // blank: bank 1's tile 0 (rings_setup)
}

// ------------------------------------------------------------------ opponent menu
static uint8_t mflash;

// ---- the menu's text and arrows over the rings, as the original prints them on the circles:
// 56 cells (the text's rows 8-11, columns 4-15, and the arrows' 2x2 cells) have tiles of their
// own, built each ring step from the frame's ring tile and the text's pixels (comp_tile) and
// sent with the frame into the hidden copy (VRAM bank 0's tiles 1-56, at 0x8000 / 0x9000), so
// text and rings change together. Ring palettes 3-5 (all colour 10 on the menu) fold into 2
// (rings_setup, menu_fold), leaving 3-5 to the text: red, pink and flashing white.
#define MTXT_BUF ((uint8_t *)(((uint16_t)dlgbuf + 15) & ~15u))   // the text, 20x4 tiles
#define MARR_BUF (CVB + 2112)                                     // the arrows, 10x2 tiles
#define MC_STAGE CVB                                              // the 56 merged tiles
#define MC_N 56
static uint8_t mc_pal[4];      // the text rows' palettes
// mc_place_at: place the cells: 3 the text is new, 2 a frame of it is sent, 1 it shows
static uint8_t menu_pend;      // 1: the bar's canvases to place once their tiles are in

// the cells: 0-47 the text's (rows 8-11, columns 4-15), 48-55 the arrows' (rows 3-4, columns
// 6, 7, 12, 13), each with its own tile (bank 0's 1-56) and an entry in MC_LIST (4 bytes:
// the ring tile, its zone base or 3 to skip, the text's ink mask: the cell's tile in the
// text's buffer, made an 8-byte mask in place). Cells without ink show the ring tile; a cell
// with ink in the text just replaced is still merged (mc_modes(1)) until the new text is
// placed, so nothing shows half changed.
#define MC_LIST (CVB + 896)    // 56 x 4 bytes
#define MC_MT (CVB + 1120)     // ring_mt (gen_assets: each cell's ring tile and zone base, flip)
static uint8_t mc_ink[7], mc_was[7];   // cells with ink: now, in the text before
static const uint8_t mc_ac[4] = {1, 2, 7, 8};   // the arrows' cells' columns in their canvas

static uint8_t *mc_tile(uint8_t i) {   // (the text's: i < 48)
    uint8_t r = 0;
    if (i < 48) {
        while (i >= 12) { i -= 12; r++; }
        return MTXT_BUF + ((uint16_t)(r * 20 + 4 + i) << 4);
    }
    i -= 48;
    return MARR_BUF + ((uint16_t)((i >> 2) * 10 + mc_ac[i & 3]) << 4);
}

// a tile's ink as an 8-byte row mask, in place; mirrored for the left half's flipped tiles
static uint8_t mc_mask(uint8_t *p, uint8_t flip) {
    uint8_t r = 8, v, any = 0, *q = p, *d = p;
    do {
        v = *q++;
        v |= *q++;
        *d++ = v;
        any |= v;
    } while (--r);
    if (flip && any) bytes_rev(p, 8);
    return any;
}

static uint8_t mc_split;   // the cell the second half of the merging starts at (even work)

static void mc_modes(uint8_t was) {
    uint8_t i, n = 0, k = 0, *e = MC_LIST + 1;
    for (i = 0; i < 56; i++, e += 4) {
        uint8_t b = 1 << (i & 7), ink = i >= 48 || (mc_ink[i >> 3] & b) || (was && (mc_was[i >> 3] & b));
        *e = ink ? MC_MT[(i << 1) + 1] & 3 : 3;
        n += ink;
    }
    for (i = 0, e = MC_LIST + 1; i < 56 && k < (n >> 1); i++, e += 4)
        k += *e != 3;
    mc_split = i;
}

static void mc_list(void) {
    uint8_t i, *e = MC_LIST, *m;
    banked_copy(MC_MT, BANK(ring_mt), ring_mt, 112);
    for (i = 0; i < 56; i++, e += 4) {
        m = mc_tile(i);
        e[0] = MC_MT[i << 1];
        e[2] = (uint8_t)(uint16_t)m;
        e[3] = (uint8_t)((uint16_t)m >> 8);
    }
    mc_modes(0);
}

static void menu_comp(uint8_t f, uint8_t half) {   // the cells before mc_split, or from it
    uint8_t bank, from = half ? mc_split : 0, n = half ? 56 - mc_split : mc_split;
    const uint8_t *fr = ring_src(f, &bank);
    if (n) comp_cells(MC_STAGE + ((uint16_t)from << 4), fr, MC_LIST + (from << 2), n, bank);
}

// the cells into the map: the merged tiles where there is ink, the rings elsewhere
static void mc_place_row(uint8_t r) {   // a text row, 0-3
    uint8_t t[20], at[20], c, i;
    {
        banked_copy(t, ring_mb, ring_mapp + (8 + r) * 20, 20);
        banked_copy(at, ring_ab, ring_attrp + (8 + r) * 20, 20);
        for (c = 0; c < 20; c++)
            if ((at[c] & 7) > 2) at[c] = (at[c] & ~7) | 2;
        for (c = 0; c < 12; c++) {
            i = r * 12 + c;
            if (mc_ink[i >> 3] & (1 << (i & 7))) {
                t[4 + c] = 1 + i;
                at[4 + c] = mc_pal[r] | (c < 6 ? 0x20 : 0);
            }
        }
        map_put(0, 0, 8 + r, 20, 1, t, at);
    }
}

static uint8_t mc_arrows;   // the arrows' cells are placed

static void mc_place_arrows(void) {   // (always merged, so placed once)
    static const uint8_t ax[4] = {6, 7, 12, 13};
    uint8_t t[1], at[1], r, c;
    mc_arrows = 1;
    for (r = 0; r < 2; r++)
        for (c = 0; c < 4; c++) {
            t[0] = 49 + r * 4 + c;
            at[0] = 4 | (c < 2 ? 0x20 : 0);   // the arrows' colour 2: pink
            map_put(0, ax[c], 3 + r, 1, 1, t, at);
        }
}

static void mc_place(void) {   // all of it
    uint8_t r;
    for (r = 0; r < 4; r++) mc_place_row(r);
    mc_place_arrows();
    mc_modes(0);
}

static void menu_text_draw(void) {   // into MTXT_BUF
    static const char *t[4];
    static uint8_t col[4];
    static uint8_t ty[4] = {2, 10, 19, 25};   // (line 3 at 16 for an unlocked card: the original's 38)
    static char buf2[24];
    uint8_t i;
    col[0] = 1; col[1] = 2; col[2] = 3; col[3] = 3;
    ty[2] = scoremode || unlocked[mnucur] ? 16 : 19;
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
    CV_MTXT.buf = MTXT_BUF;
    cv_begin(&CV_MTXT, 0);
    for (i = 0; i < 4; i++) {
        cv_cprint(t[i], 80, ty[i], col[i]);
        mc_pal[i] = 2 + col[i];   // colour 1, 2, 3: palette 3, 4, 5
    }
}

// the text's ink masks (after menu_text_draw, a frame later on a card change)
static void menu_text_mask(void) {
    uint8_t i;
    for (i = 0; i < 7; i++) {
        mc_was[i] = mc_ink[i];
        mc_ink[i] = 0;
    }
    {
        uint8_t r, c, *row = MTXT_BUF + (4 << 4);   // row 0, column 4
        for (i = 0, r = 0; r < 4; r++, row += 20 << 4) {
            uint8_t *p = row;
            for (c = 0; c < 12; c++, i++, p += 16)
                if (mc_mask(p, c < 6)) mc_ink[i >> 3] |= 1 << (i & 7);
        }
    }
}

// the text's palettes over the rings (colour 1 ring 3, 2 ring 10): red, pink, flashing white
static void menu_text_pals(void) {
    set_bgpal(3, 0, 3, 10, 8);
    set_bgpal(4, 0, 3, 10, 9);
    set_bgpal(5, 0, 3, 10, 7);
}

static uint8_t mt_stage;   // a card change: 1 the text drawn, its masks next frame

static void menu_text(void) {   // merged at the next ring step, placed after it shows
    menu_text_draw();
    mt_stage = 1;
}

static void menu_text_next(void) {
    if (mt_stage != 1) return;
    mt_stage = 0;
    menu_text_mask();
    mc_modes(1);
    ring_force = 1;
    mc_place_at = 3;
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

// the arrows beside the selected card, bobbing: BG cells over the rings (merged with them
// at the next ring step: menu_comp), as the sprite palettes are the cards'. PICO-8's
// glyphs, 7x5, straight into the cells' ink masks: canvas x 12 - b and 62 + b, y 6 (the
// cells: rows 3-4, columns 6-7 and 12-13)
static void menu_arrows(int8_t b) {
    static const uint8_t gl[2][5] = {{0x7C, 0xE6, 0xC6, 0xE6, 0x7C}, {0x7C, 0xCE, 0xC6, 0xCE, 0x7C}};
    uint8_t a, r, *m0, *m1;
    for (a = 0; a < 2; a++) {
        uint8_t sh = a ? 6 + b : 4 - b, on = 1;   // x - the left cell's x (both always, as the original)
        m0 = mc_tile(48 + a * 2);       // row 3, left then right cell
        m1 = mc_tile(48 + a * 2 + 1);
        memset(m0, 0, 8);
        memset(m1, 0, 8);
        memset(m0 + 160, 0, 8);         // row 4 (MARR_BUF rows are 10 tiles apart)
        memset(m1 + 160, 0, 8);
        if (!on) continue;
        for (r = 0; r < 5; r++) {
            uint16_t w = ((uint16_t)gl[a][r] << 8) >> sh;
            uint8_t y = 6 + r, o = y & 7, *p0 = y < 8 ? m0 : m0 + 160, *p1 = y < 8 ? m1 : m1 + 160;
            p0[o] = (uint8_t)(w >> 8);
            p1[o] = (uint8_t)w;
        }
        if (!a) {   // the left arrow's cells show flipped ring tiles
            bytes_rev(m0, 8); bytes_rev(m1, 8);
            bytes_rev(m0 + 160, 8); bytes_rev(m1 + 160, 8);
        }
    }
}

// the merged cells go in the map once a frame of them shows (mc_place_at), the bar's
// canvases once their tiles are in
static uint8_t mc_row;

static void menu_place(void) {
    if (mc_place_at == 1 && !ring_flip) {   // a row a frame; the old text's cells merged till then
        mc_place_row(mc_row);
        if (++mc_row == 4) {
            mc_row = 0;
            if (!mc_arrows) mc_place_arrows();
            mc_modes(0);
            mc_place_at = 0;
        }
    }
    if (menu_pend && !vram_dma_busy()) {
        cv_place(&CV_VIAL);
        cv_place(&CV_BAR2);
        menu_pend = 0;
    }
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
    static const uint8_t zlo[6] = {0, 15, 26, 41, 60, 80};
    static const uint8_t blank[10] = {0};
    int16_t my = 69 << 8, dy = (int16_t)sy0 << 8, cy, mxp = (int16_t)mnucur * 48 - 4;
    uint8_t z, rb, set, nt = 0, cols[6], shown[6] = {255}, flash = 255, f;   // nt: frames, 40 = circtme 0
    uint8_t mshown[10] = {0};
    cv_place_rings(cvs, blank);   // no streak line on the menu
    set_bgpal(7, 8, 0, 9, 10);    // the bar's colours (the streak's before)
    if (scoremode) {   // the title drew the bar and the text for "continue"; score mode's bar is bare
        menu_text_draw();
        menu_text_mask();
        cv_begin(&CV_VIAL, 0);
        cv_send(&CV_VIAL);
        cv_begin(&CV_BAR2, 0);
        cv_send(&CV_BAR2);
    }
    copy_objpals(0, 4, menu_obj_pals);
    copy_objpals(7, 1, menu_obj_pals + 16);   // the menu's cards (gen_assets)
    cut_ly = 104;   // the cards stay behind the bars
    mound_step(0, mshown);   // (the title's bar as it is, into the block it then changes)
    vq_first = 1;
    spr_split = 1;
    for (;;) {
        if (nt < 40) nt++;
        my -= my / 20;
        if (my < 256) my = 0;   // the last pixel: the menu's cards are still
        // the rings' centre moves to the card, 0.5 px a frame (the title's sets, then 7-9)
        if (dy > -14 * 256) dy -= 141;
        // (the title's set until -3, not each pixel of it: fewer uploads while everything moves)
        set = dy >= -3 * 256 ? (uint8_t)(sy0 + 3) : dy >= -5 * 256 ? 0 : dy >= -9 * 256 ? 7 : dy >= -3200 ? 8 : 9;
        rings_update(set);
        // switched from the middle out, the zone at the edge bright
        rb = (uint8_t)(((uint16_t)nt * 11) >> 2);   // 0..110 px
        for (z = 0; z < 6; z++)   // bright for one ring's width (the original's one ring), then the menu's
            cols[z] = rb >= zlo[z] + 10 ? ring_cols2[z] : rb > zlo[z] || (!z && rb) ? 15 : ring_cols1[z];
        for (z = 0; z < 6 && cols[z] == shown[z]; z++);
        if (z < 6) {
            memcpy(shown, cols, 6);
            ring_pals(cols);
        }
        mound_step(nt, mshown);   // the menu's bar rises over the title's from the middle out
        // the logo rises with the cards, and 20 px more
        cy = 50 + sy0 + (my >> 8) - 69 - (nt >> 1);
        if (cy > -22) {   // (its split line kept off line 0: LYC 0 also matches during line 153)
            draw_logo(80, cy);
            spl_ly = (uint8_t)(cy + 24);
        } else spl_ly = 0;
        menu_cards(mxp, my >> 8);
        f = (uint8_t)(frames % 20) < 10;   // the title's options keep flashing under the bar
        if (f != flash) {
            flash = f;
            bgpal[6 * 4 + 2] = pico[f ? 15 : 7];
            pal_dirty = 1;
        }
        frame();
        if (nt == 40 && !my && !spl_ly && set == 9) break;
    }
    WY_REG = 104;   // the menu's bar whole (the mound leaves the corners), in the window
    win_show = 1;
    vq_first = 0;
    menu_warm = 1;
}

void scene_menu(void) BANKED {
    uint8_t redraw = 1;
    int8_t ab = 99;
    int16_t mx, mtarget, my = menu_from << 8, d;
    uint8_t fold = 255;   // from the title: the ring rows still to fold (menu_fold), then the text
    menu_from = 69;
    mc_arrows = 0;
    mc_row = 0;
    if (menu_warm) {   // from the title: rings, cards, bar and text (drawn) are up
        menu_warm = 0;
        my = 0;
        bar_placed = 1;    // the title's bar is the menu's
        menu_pend = 0;
        redraw = 0;
        ring_base = RING_MENU;   // the same frames as the title's last set
        fold = 0;
    } else {
        reset_screen();
        rings_setup(RINGS_MENU);
        menu_bufs();
        copy_objpals(0, 4, menu_obj_pals);
        copy_objpals(7, 1, menu_obj_pals + 16);   // the menu's cards (gen_assets)
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
    if (redraw) {   // the screen is black: the text merged into both copies, placed now
        bar_draw(!scoremode);
        menu_text_draw();
        menu_text_mask();
        menu_arrows(0);
        ab = 0;
        menu_text_pals();
        mc_list();
        menu_comp(ring_shown, 0);
        menu_comp(ring_shown, 1);
        vram_copy((uint8_t *)0x9010u, CVB, 56 * 16);
        vram_copy((uint8_t *)0x8010u, CVB, 56 * 16);
        mc_place();
        mc_place_at = 0;
        ring_comp = 1;
        ring_vram |= 2;   // (over the sprite tiles: reloaded by reset_screen)
        redraw = 0;
    }
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
        if (fold < 13) {   // menu_fold: a ring row a frame (their palettes 3-5 had these colours)
            ring_row(fold, 1);
            if (++fold == 13) {
                menu_text_pals();
                mc_list();
                ring_comp = 1;
                ring_vram |= 2;
                ring_force = 1;
                mc_place_at = 3;
                fold = 255;
            }
        }
        menu_text_next();
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
            uint8_t f = (uint8_t)(frames % 20) < 10;   // sin(time() * 3) < 0: 20 frames
            if (f != mflash) {
                mflash = f;
                bgpal[5 * 4 + 3] = pico[f ? 15 : 7];   // the text's colour 3
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

// the winnings texts' rise without SDCC's 16-bit divisions (four a frame, with the text
// redrawn as it moves, dropped every other frame of the fade-in): d / 10 as shifts (0.0996),
// and (18 * 256 - h) * 4 / 768 as / 64 then / 3
static int16_t tenth(int16_t d) {
    uint16_t a = (uint16_t)(d < 0 ? -d : d);
    a = (a >> 4) + (a >> 5) + (a >> 8) + (a >> 9);
    return d < 0 ? -(int16_t)a : (int16_t)a;
}
static uint8_t px24(uint16_t h) {
    uint8_t v = (uint8_t)((uint16_t)(18 * 256 - h) >> 6);   // h <= 18 * 256: v <= 72
    return (uint8_t)(((uint16_t)v * 171) >> 9);           // v / 3, exact to 383
}

static void drip_start(void) {
    memset(dp_hv, 0, sizeof(dp_hv));
    memset(dp_tl, 0, sizeof(dp_tl));
    memset(dp_tv, 0, sizeof(dp_tv));
    dp_hd[0] = -(int16_t)((uint16_t)rnd(90) << 8);
    dp_hd[1] = 0;
    dp_h = 0;
    // drip_draw's tiles (cvbuf 768): it only draws, and clears, the high plane
    memset((uint8_t *)(((uint16_t)cvbuf + 15) & ~15u) + 768, 0, 96);
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
    if (phase == 1) {   // (256 - dp_h) / 50, as 1/64 + 1/256 + 1/2048 (0.0200)
        uint16_t a = (uint16_t)(256 - dp_h);
        dp_h += (int16_t)((a >> 6) + (a >> 8) + (a >> 11));
    } else dp_h -= tenth(dp_h);
    dp_ph += 273;   // a turn a second
}

static void drip_draw(uint8_t up) {
    uint8_t *b = (uint8_t *)(((uint16_t)cvbuf + 15) & ~15u) + 768;   // past the canvases (scene_wining)
    // x * 21 / 20 for x 0-40 (SDCC's 16-bit divisions, with the drop's below, and clearing
    // the whole buffer by memset, took a quarter of each frame of the drip)
    static const uint8_t x21[41] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21,
        22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 42};
    uint8_t i, r, n, bit, q, rem;
    int16_t a, z;
    for (i = 1; i < 96; i += 2) b[i] = 0;   // (only the high plane is drawn: the low stays 0)
    for (i = 0, bit = 0x10; i < 2; i++, bit >>= 1) {
        if (dp_hd[i] <= 0) continue;
        a = dp_tl[i] > 0 ? dp_tl[i] >> 8 : 0;
        z = dp_hd[i] >> 8;
        if (z > 39) z = 39;
        if (a > z) continue;
        {   // original rows a..z cover these 42-row ones (8-bit, stepping a pointer: as
            //  16-bit rows with a ternary each, this loop alone took 0.4 of a frame)
            uint8_t a8 = x21[a], cnt = x21[z + 1] - a8;
            uint8_t *p = b + 1 + ((up ? 41 - a8 : a8) << 1);
            if (up) for (; cnt; cnt--, p -= 2) *p |= bit;
            else for (; cnt; cnt--, p += 2) *p |= bit;
        }
    }
    // drop: 6x7 stretched to 7 * h * (1..2) px
    {   // (1 + pulse) * h, 8.8, in 16 bits (no 32-bit maths: its library would land in bank 0)
        uint16_t m = (uint16_t)((256 + drip_s64[(dp_ph >> 8) & 63]) >> 1) * (dp_h >> 1) >> 6;
        m *= 7;
        n = (uint8_t)((m + m / 20) >> 8);
    }
    if (n > 14) n = 14;
    {
        uint8_t *p = b + 1 + (up ? 82 : 0);
        for (r = 0, q = 0, rem = 0; r < n; r++) {   // q = r * 7 / n
            *p |= drip_drop[q];
            if (up) p -= 2; else p += 2;
            for (rem += 7; rem >= n; rem -= n) q++;
        }
    }
    vram_dma_vbl(b, (uint8_t *)0x8000u + (DRIP_TILE << 4), 0, 6, _current_bank);
    for (i = 0; i < 3; i++) spr_put(76, 64 + i * 16, DRIP_TILE + i * 2, SPRPAL_DRIP);
}

void scene_wining(void) BANKED {
    static const canvas_t txt = {cvbuf, 202, 0, 2, 4, 16, 3, 6, 0};
    int16_t wint = 200, vlev = -1;
    uint8_t winphase = 0, dropup, redraw = 1, rbar = 1, turn = 0, dyw = 255, dyg = 255, n, placed = 0;
    // wintxth / govertxth: each text rises 18 px into its clip (/10 a frame) and sinks out
    // again; 24 px canvas here. 8.8, 18 * 256 = fully up.
    uint16_t wth = 0, gth = 0, wtd = 18 * 256, gtd = 0;
    uint8_t died = 0;
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
        uint8_t wh = 0, rip = 0, busy = 0, tfull = 0, part;   // tfull: risen text drawn, losses_desc + 1
        strip = (uint8_t *)(((uint16_t)dlgbuf + 15) & ~15u);
        vlev = vial_level(blood_txt);
        cv_begin(&vial, 0);
        cv_rect(0, 0, 31, 1, 1);
        if (!scoremode) draw_vial(vial.buf, vial.w, 7, 5, blood_txt);   // (score mode: the bar alone)
        memcpy(strip + 192, cvbuf, 64);
        cv_draw(&vial);
        cv_begin(&bar2, 0);
        if (!scoremode) cv_bignumc(numstr(blood_txt), 80, 5, 1, 1);
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
        // and now: its queued copy can find the queue full after this setup and be dropped,
        // leaving the last screen's tiles ("ROUND LOST") along the bar's top until it ripples
        bkg_tiles_upload(STRIP_TILE, 12, strip);
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
            wth += tenth((int16_t)(wtd - wth));
            gth += tenth((int16_t)(gtd - gth));
            n = px24(wth);   // 24 px at 0, 0 at 18 px
            if (n != dyw) { dyw = n; redraw = 1; }
            n = px24(gth);
            if (n != dyg) { dyg = n; redraw = 1; }
            if (redraw && rbar) turn ^= 1;
            busy = 0;
            if (redraw && (!rbar || turn)) {
                // drawn where its DMA reads it (cvbuf 864: canvases 0-767, drip 768), so no
                // copy; with the separator written as bytes, it is a third of a frame, not 0.9
                canvas_t tc = txt;
                uint8_t x, *p;
                tc.buf = (uint8_t *)(((uint16_t)cvbuf + 15) & ~15u) + 864;
                busy = 1;
                part = dyw == 0 && tfull == losses_desc + 1;
                if (part) {
                    // risen, only the number changed (the drip): its two tile rows alone, the
                    // label kept (canvas rows 1-2 as a canvas of their own)
                    canvas_t t2 = tc;
                    t2.buf += 256;
                    t2.y++;
                    t2.h = 2;
                    cv_begin(&t2, 0);
                    cv_bignumc(numstr(wining_txt), 64, 1, 2, 1);
                } else {
                    cv_begin(&tc, 0);
                    if (dyw < 24) {
                        cv_cprint(losses_desc ? "losses" : "winnings", 64, 2 + dyw, 1);
                        cv_bignumc(numstr(wining_txt), 64, 9 + dyw, 2, 1);
                    }
                    tfull = dyw == 0 ? losses_desc + 1 : 0;
                }
                // sepline, pixel row 23: purple line, dotted ends ("aa2a2a", plum shown
                // black): x 2, 4, 6-121, 123, 125 in colour 3, the rest of the row clear
                for (x = 0, p = tc.buf + (32 << 4) + 14; x < 16; x++, p += 16)
                    p[0] = p[1] = x == 0 ? 0x2B : x == 15 ? 0xD4 : 0xFF;
                if (placed & 1)   // (the number alone: its rows, 32 tiles)
                    vram_dma_vbl(tc.buf + (part ? 256 : 0),
                                 (uint8_t *)0x8000u + ((uint16_t)(tc.tile + (part ? 16 : 0)) << 4), tc.bank,
                                 part ? 32 : 48, _current_bank);
                else { cv_draw(&tc); placed |= 1; }
                redraw = 0;
            }
            else if (rbar) {
                rbar = 0;
                busy = 1;
                // (drawn where their DMA reads them, cvbuf 1632 / 1824: copying them there
                // as well cost a third of a frame)
                canvas_t tc;
                if (vial_level(blood_txt) != vlev) {
                    vlev = vial_level(blood_txt);
                    tc = vial;
                    tc.buf = (uint8_t *)(((uint16_t)cvbuf + 15) & ~15u) + 1632;
                    cv_begin(&tc, 0);
                    cv_rect(0, 0, 31, 1, 1);
                    if (!scoremode) draw_vial(tc.buf, tc.w, 7, 5, blood_txt);   // (score mode: the bar alone)
                    memcpy(strip + 192, tc.buf, 64);   // its top row shows in the strip
                    rip |= 2;
                    vram_dma_vbl(tc.buf, (uint8_t *)0x8000u + ((uint16_t)tc.tile << 4), tc.bank, 12, _current_bank);
                }
                tc = bar2;
                tc.buf = (uint8_t *)(((uint16_t)cvbuf + 15) & ~15u) + 1824;
                cv_begin(&tc, 0);
                if (!scoremode) cv_bignumc(numstr(blood_txt), 80, 5, 1, 1);
                vram_dma_vbl(tc.buf, (uint8_t *)0x8000u + ((uint16_t)tc.tile << 4), tc.bank, 40, _current_bank);
            }
            if (winphase == 0) {
                if (wint <= 0) {
                    if (!scoremode) {
                        blood += wining;
                        if (blood < 0) blood = 0;
                        if (blood > 5000) blood = 5000;
                    }
                    savegame();
                    if (scoremode) winphase = 4;   // (wint spent: the text sinks at once)
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
                    died = 1;
                    chain = 0;
                    music(9);
                } else if (blood >= 5000) {
                    winphase = 5; wint = 400;
                    died = 0;
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
                wtd = 0;   // the winnings text sinks, then the ending (its own text rises there)
                if (wth < 230) {
                    savegame();
                    fadeout(1);
                    scene = SC_END;
                    ticksfx = 0;
                    goverprint_set(died);
                    return;
                }
            }
            if (winphase == 1 || winphase == 2) drip_draw(dropup);
            if (winphase >= 1 && !dropup) {   // drpwh: up while the drip reaches the bar, down as it stops
                if (dp_tl[0] > 0 || dp_tl[1] > 0) wh = wh > 2 ? wh - 2 : 0;
                else if ((dp_hd[0] >= (89 << 8) || dp_hd[1] >= (89 << 8)) && wh < 100) wh++;
            }
            // the ripple holds for a frame while a counter is redrawn (both: over a frame)
            strip_tick(wh);
            if ((wh || rip) && !busy) {   // the last frame of a ripple draws the edge flat again
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
            }
            frame();
        }
    }
}

void gotowin(void) BANKED { scene = SC_WINING; }
