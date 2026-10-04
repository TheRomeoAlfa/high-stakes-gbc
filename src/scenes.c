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
static uint8_t ring_cols[4];
static uint8_t ring_phase = 255;

static void rings_setup(uint8_t variant) {
    load_bkg_banked(BANK(ring_tiles), ring_tiles, 1, N_RING_TILES, 0);
    {
        uint8_t r;
        for (r = 0; r < 13; r++) map_put(0, 0, r, 20, 1, ring_map + r * 20, ring_attr + r * 20);
    }
    if (variant == 0) { ring_cols[0] = 3; ring_cols[1] = 10; ring_cols[2] = 2; ring_cols[3] = 9; }
    else { ring_cols[0] = 3; ring_cols[1] = 10; ring_cols[2] = 10; ring_cols[3] = 10; }
    ring_phase = 255;
}

static uint16_t ring_t;

static void rings_update(void) {
    uint8_t ph, z, k, lit;
    ring_t += 27;  // 0.26 px/frame on a 10px ring spacing, 4 phases
    ph = (uint8_t)(ring_t >> 8) & 3;
    if (ph == ring_phase) return;
    ring_phase = ph;
    lit = (4 - ph) & 3;
    for (z = 0; z < 4; z++) {
        uint16_t *p = bgpal + z * 4;
        for (k = 0; k < 4; k++) {
            uint8_t on = k == lit;
            p[k] = on ? pico[ring_cols[z]] : 0;
        }
    }
    pal_dirty = 1;
}

// ------------------------------------------------------------------ dialog box
static canvas_t cv_dlg = {dlgbuf, 180, 0, 5, 1, 15, 4, 0, 0};
static const char *dlg_text;
static uint8_t dlg_pos, dlg_len;
static int16_t dlg_x, dlg_y;

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
    uint8_t stcur = 0, redraw = 1, flash = 0;
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
    cv_begin(&cvs, 0);
    cv_cprint(chaintxt(), 40, 2, 1);
    cv_draw(&cvs);
    rings_update();
    screen_on();
    for (;;) {
        if (redraw) {
            cv_begin(&bar, 0);
            cv_bignumc("high stakes", 80, 4, 1, 0);
            cv_cprint(stmnu[stcur], 80, 22, 2);
            if (stcur > 0) cv_putc('\x02', 42, 22, 2);
            if (stcur < nst - 1) cv_putc('\x03', 112, 22, 2);
            cv_print("gb v1", 2, 33, 3);
            cv_draw(&bar);
            redraw = 0;
        }
        rings_update();
        {
            uint8_t f = ((frames >> 3) & 1);
            if (f != flash) {
                flash = f;
                bgpal[4 * 4 + 2] = pico[f ? 15 : 7];
                pal_dirty = 1;
            }
        }
        draw_logo(80 + sintab[(frames >> 2) & 31], 50 + sintab[((frames * 3) >> 4) & 31]);
        if (BTNP(J_LEFT) && stcur > 0) { stcur--; sfx(63); redraw = 1; }
        else if (BTNP(J_RIGHT) && stcur < nst - 1) { stcur++; sfx(63); redraw = 1; }
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
    static const canvas_t big = {cvbuf, 140, 0, 2, 14, 16, 2, 0, 0};
    uint8_t ti = 0, done = 0, arrow = 0;
    reset_screen();
    vampval = 10;
    set_bgpal(0, 0, 10, 2, 8);
    card_pals_load();
    copy_bgpals(5, 3, whisky_pals);
    load_bkg_banked(BANK(whisky_tiles), whisky_tiles, IMG_BASE, N_WHISKY_TILES, 1);
    blood = blood_txt = 5000;
    mult = 1;
    ticksfx = 0;
    dlg_setup(1, 1);
    dlg_say(talk[0]);
    gtxt_changed = 4;
    screen_on();
    for (;;) {
        update_gtxt();
        if (gtxt_changed & 4) {
            cv_begin(&big, 0);
            cv_bignumc(numstr(blood_txt), 64, 2, 3, 1);
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
static void draw_vial(const canvas_t *cv, int16_t ox, int16_t oy, int16_t bl) {
    // port of drawblood(): red rows on top mean "empty", black is blood
    int16_t px = (int16_t)(((int32_t)1652 * bl) / 5000), pxperc;
    uint8_t x, y, c;
    static const int16_t lookup[6] = {0, 8, 30, 80, 162, 212};
    if (px >= 212) pxperc = 10 - (px - 212) / 144;
    else {
        uint8_t i = 0;
        do i++; while (!(px <= lookup[i - 1]));
        pxperc = 15 - (i - 1);
    }
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

static const canvas_t CV_MTXT = {cvbuf, 1, 0, 0, 0, 20, 4, 0, 1};
static const canvas_t CV_VIAL = {cvbuf, 81, 0, 8, 5, 4, 3, 5, 1};
static const canvas_t CV_BAR2 = {cvbuf, 93, 0, 0, 8, 20, 2, 5, 1};

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

void scene_menu(void) BANKED {
    uint8_t i, redraw = 1;
    int16_t mx, mtarget;
    reset_screen();
    card_pals_load();
    set_bgpal(0, 0, 8, 9, 7);
    copy_objpals(0, 8, obj_pals);
    bar_setup();
    for (i = 0; i < 4; i++) draw_card(8 + i * 6, 2, CF_FACE(10 + i));
    map_fill(1, 0, 0, 20, 5, 0, 0);
    mtarget = (int16_t)mnucur * 48 - 4;
    mx = mtarget * 16;
    WY_REG = 64;
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
        if (redraw) { menu_text(); redraw = 0; }
        mtarget = (int16_t)mnucur * 48 - 4;
        mx += (mtarget * 16 - mx) / 10;
        scx = (uint8_t)(mx >> 4);
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
            if (mnucur > 0) spr_put(52 - b, 30, SPR_MARR_L, SPRPAL_MARR_L);
            if (mnucur < 3) spr_put(102 + b, 30, SPR_MARR_R, SPRPAL_MARR_R);
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
    uint8_t cur = 0, redraw = 1, arrow = 0;
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
    dlg_setup(5, 1);
    dlg_say("how about a final\nbonus round? double\nwinnings or nothing?");
    screen_on();
    for (;;) {
        if (dlg_tick()) {
            uint8_t a = (frames >> 4) & 1;
            if (a != arrow) { arrow = a; dlg_arrow(a); }
        }
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
void scene_wining(void) BANKED {
    static const canvas_t txt = {cvbuf, 100, 0, 2, 4, 16, 3, 4, 0};
    int16_t wint = 200, drip = 0, tail = 0;
    uint8_t winphase = 0, dropup, redraw = 1, rbar = 1, over = 0;
    const char *g1 = 0, *g2 = 0;
    fadeout(1);
    music(-1);
    if (wining > high[mnucur]) high[mnucur] = wining;
    dropup = wining < 0;
    reset_screen();
    rings_setup(1);
    set_bgpal(4, 0, 9, 8, 7);
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
            if (redraw || (gtxt_changed & 2)) {
                cv_begin(&txt, 0);
                if (over) {
                    cv_cprint(g2, 64, 2, 1);
                    cv_bignumc(g1, 64, 9, 2, 0);
                } else {
                    cv_cprint(losses_desc ? "losses" : "winnings", 64, 2, 1);
                    cv_bignumc(numstr(wining_txt), 64, 9, 2, 1);
                }
                cv_draw(&txt);
                redraw = 0;
            }
            if (rbar || (gtxt_changed & 4)) {
                rbar = 0;
                cv_begin(&vial, 0);
                draw_vial(&vial, 7, 3, blood_txt);
                cv_draw(&vial);
                cv_begin(&bar2, 0);
                cv_bignumc(numstr(blood_txt), 80, 3, 1, 1);
                cv_draw(&bar2);
            }
            gtxt_changed = 0;
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
                    else { wining = 0; winphase = 1; }
                }
            } else if (winphase == 1) {
                if (drip < 40) drip++;
                if (wining_txt == 0) winphase = 2;
            } else if (winphase == 2) {
                if (tail < 40) tail += 2;
                else winphase = 3;
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
                if (wint <= 0) {
                    fadeout(1);
                    music(0);
                    scene = SC_MENU;
                    ticksfx = 0;
                    return;
                }
            } else if (winphase == 5) {
                if (!over) { over = 1; redraw = 1; wint = 120; }
                if (wint <= 0) {
                    savegame();
                    fadeout(1);
                    scene = SC_END;
                    ticksfx = 0;
                    goverprint_set(g1, g2);
                    return;
                }
            }
            // drip of blood between the number and the vial
            if (winphase == 1 || winphase == 2) {
                int16_t top = 64, bot = 106, y;
                int16_t len = (bot - top) * drip / 40, cut = (bot - top) * tail / 40;
                for (y = 0; y < len; y += 16) {
                    int16_t yy = dropup ? bot - y - 16 : top + y;
                    if (y + 16 > cut) spr_put(76, yy, SPR_DRIP, SPRPAL_DRIP);
                }
                if (winphase == 1) {
                    int16_t yy = dropup ? bot - len - 6 : top + len;
                    spr_put(76, yy, SPR_DROP, SPRPAL_DROP);
                }
            }
            if (BTNP(J_A | J_B | J_START)) {
                wint = 0;
                blood_txt = blood;
                wining_txt = wining;
                gtxt_changed = 6;
                rbar = 1;
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
    uint16_t wint = 1000;
    uint8_t died = eg2[0] == 'e';
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
    cv_begin(&txt, 0);
    cv_cprint(eg2, 64, 2, 1);
    cv_bignumc(eg1, 64, 9, 2, 0);
    cv_draw(&txt);
    screen_on();
    while (wint--) {
        frame();
        if (BTNP(J_A | J_START)) break;
    }
    savegame();
    fadeout(1);
    wait_frames(120);
    music(0);
    scene = SC_START;
}

void gotowin(void) BANKED { scene = SC_WINING; }
