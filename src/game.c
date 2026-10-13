// Core gameplay: a direct port of the PICO-8 game's update_game / draw_game.
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include <rand.h>
#include "common.h"
#include "lift.h"
#include "wave.h"
#include "board.h"

uint8_t scene;
int16_t blood, tries, chain, bchain;
uint8_t wbin;
uint8_t unlocked[4];
int16_t high[4];

int16_t blood_txt, wining, wining_txt, stakes, stakes_txt, mult;
uint8_t ticksfx, losses_desc;
uint8_t vampval, dropboxes, droptoken, scoremode, mnucur;
uint8_t round_, bonusmode;
char wins[8];
uint8_t gtxt_changed;


// ------------------------------------------------------------------ counters
static void stepv(int16_t *v, int16_t target) {
    int16_t d = target - *v;
    if (d < 0 ? -d < mult : d < mult) *v = target;
    else *v += d > 0 ? mult : -mult;
}

void update_gtxt(void) BANKED {
    static uint8_t tick5;
    if (++tick5 < 5) return;
    tick5 = 0;
    if (stakes_txt != stakes) {
        stepv(&stakes_txt, stakes);
        if (ticksfx) sfx(ticksfx);
        gtxt_changed |= 1;
    }
    if (wining_txt != wining) {
        stepv(&wining_txt, wining);
        if (!losses_desc && wining_txt < 0) losses_desc = 1;
        if (losses_desc && wining_txt > 0) losses_desc = 0;
        gtxt_changed |= 2;
    }
    if (blood_txt != blood) {
        stepv(&blood_txt, blood);
        if (ticksfx) sfx(ticksfx);
        gtxt_changed |= 4;
    }
}

// ------------------------------------------------------------------ cards
void draw_card(uint8_t tx, uint8_t ty, uint8_t fr) BANKED {
    if (fr == CARD_NONE) map_fill(0, tx, ty, 3, 4, 0, 0);
    else map_put(0, tx, ty, 3, 4, card_fmap + fr * 12, card_fattr + fr * 12);
}

void card_pals_load(void) BANKED {
    copy_bgpals(1, 4, card_pals);
}

void reset_screen(void) BANKED {
    screen_off();
    win_show = 0;
    win_split = 0;
    win_cut = 0;
    WY_REG = 144;
    box_on = 0;
    ring_flip = 0;
    spr_split = spl_ly = cut_ly = vq_first = 0;
    LCDC_REG |= LCDCF_OBJON;
    LCDC_REG &= ~LCDCF_BG8000;
    if (ring_vram & 1)   // the title, menu and winnings rings used VRAM bank 1
        load_bkg_banked(BANK(card_tiles), card_tiles, 0, N_CARD_TILES, 1);
    if (ring_vram & 2)   // the menu's text over the rings used bank 0's sprite tiles
        load_spr_banked(BANK(spr_tiles), spr_tiles, 0, N_SPR_TILES, 0);
    ring_vram = 0;
    SPRITES_8x16;
    LYC_REG = 111;
    scx = scy = 0;
    cam_x = cam_y = 0;
    clear_bg();
    spr_end();
}

// ================================================================== game board
#define CELLX(i) (5 + ((i) % 3) * 3)
#define CELLY(i) (2 + ((i) / 3) * 4)
#define CCX(i) ((int16_t)CELLX(i) * 8 + 12)
#define CCY(i) ((int16_t)CELLY(i) * 8 + 15)

card_t cards[9];
// cards' height outside flips (the original's c.z / c.zd / c.zbase, 8.8, gravity 0.15): a
// face-up card bounces when A is pressed on it, a face-down one rests 1 px up under the
// cursor. Drawn by lift.c's raised frames while it is off the board.
static int16_t cz[9], czd[9], czb[9];
// dealing (dealround): each card flies in from above the screen to its cell, easing 1/5 a
// frame, dropping from 10 px up (gravity 0.15); drawn as sprites until it lands.
// fx / fy 12.4 (the flight spans more than 8.8 holds), fz / fzd 8.8
int16_t fx[9], fy[9], fz[9], fzd[9];
uint8_t vcard, autoflip;

chip_t chips[MAXCH];
uint8_t chip_hi;   // slots >= chip_hi are unused (loops stop there)

uint8_t gs[6];                // gchip state per slot (0 = gone)
static uint8_t gs_drawn[6];
// slot chips hop like the original's gchips: height, speed and rest height in 1/16 px.
// While a chip is off its rest the slot shows the empty base and the chip is a sprite.
static int8_t gz[6], gzd[6], gzb[6];
uint8_t cur, oldcur, showcur, curchip, curchipi, curstab, curshake;
static int16_t curx, cury;    // 8.8
int16_t stabx, stabdx, staby, stabdy;  // 12.4
int8_t stabonus;
int16_t pflockx, pflocky, pflockdest;
uint8_t pflockard;
int8_t dangrect;
static int16_t drectsani;     // 8.8
static uint8_t ldrect;
static uint8_t shake;
uint8_t result;               // 0 none, 1 won, 2 lost, 3 pass
uint8_t helpi, help_drawn;
static int16_t helpwy;

// cursor positions: top-left of the hand sprite, whose fingertip is at (+3, 0).
// As in the original, the fingertip is centred on chips/stake/pass and sits
// horizontally centred, 8px below centre on cards. Then navigation: down, left, right, up.
#define FT(x, y) (x) - 3, (y)
static const uint8_t curpos[17][6] = {
    {FT(51, 39), 4, 16, 2, 10}, {FT(75, 39), 5, 1, 3, 11}, {FT(99, 39), 6, 2, 13, 12},
    {FT(51, 71), 7, 16, 5, 1}, {FT(75, 71), 8, 4, 6, 2}, {FT(99, 71), 9, 5, 14, 3},
    {FT(51, 103), 17, 16, 8, 4}, {FT(75, 103), 17, 7, 9, 5}, {FT(99, 103), 17, 8, 15, 6},
    {FT(51, 8), 1, 10, 11, 10}, {FT(75, 8), 2, 10, 12, 11}, {FT(99, 8), 3, 11, 12, 12},
    {FT(123, 32), 14, 3, 13, 13}, {FT(123, 64), 15, 6, 14, 13}, {FT(123, 96), 15, 9, 15, 14},
    {FT(20, 45), 17, 16, 4, 16}, {FT(20, 102), 17, 17, 7, 16}};
static const uint8_t slot_tx[6] = {5, 8, 11, 14, 14, 14};
static const uint8_t slot_ty[6] = {0, 0, 0, 3, 7, 11};
static const uint8_t lines[6][3] = {{0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 1, 2}, {3, 4, 5}, {6, 7, 8}};
static const uint8_t bxs[4][4] = {{0, 1, 3, 4}, {1, 2, 4, 5}, {4, 5, 7, 8}, {3, 4, 6, 7}};
static const uint8_t brect_c[4] = {0, 1, 1, 0}, brect_r[4] = {0, 0, 1, 1};

// HUD canvases (bank 0 tiles)
static const canvas_t CV_STAKES = {cvbuf, 40, 0, 5, 15, 10, 2, 0, 0};
static const canvas_t CV_WINS = {cvbuf, 60, 0, 0, 15, 5, 1, 0, 0};
static const canvas_t CV_ROUND = {cvbuf, 65, 0, 0, 16, 5, 1, 1, 0};
static const canvas_t CV_WINV = {cvbuf, 70, 0, 13, 15, 7, 1, 0, 0};
static const canvas_t CV_WIND = {cvbuf, 77, 0, 13, 16, 7, 1, 1, 0};
static const canvas_t CV_STLBL = {cvbuf, 84, 0, 7, 17, 6, 1, 1, 0};
// bonus box and pass button keep a pre-rendered highlighted variant in separate
// tiles, so a highlight change only rewrites a few map bytes
// stake bonus box: rendered here (4x2 tiles), shown as sprites so it slides with the stake
static const canvas_t CV_BONUS = {cvbuf, 0, 0, 0, 0, 4, 2, 0, 0};
#define BONUS_TILE 116   // OBJ tiles 116-123 (spare in the game scene)
// At rest it is BG (sprites would crowd the 10-a-line limit with cards flying in, tokens,
// chips); only while the stake slides it is sprites, which follow the stake.
static const canvas_t CV_BONUS_BG = {cvbuf, 90, 0, 0, 1, 5, 2, 1, 0};
static uint8_t bonus_bg;   // the BG box is up
static int16_t bg_val;     // and what it shows
static uint8_t bg_hl;
static const canvas_t CV_PASS = {cvbuf, 100, 0, 1, 12, 3, 2, 1, 0};
static const canvas_t CV_PASS_HL = {cvbuf, 106, 0, 1, 12, 3, 2, 1, 0};
static const canvas_t CV_COST = {cvbuf, 235, 0, 0, 11, 5, 1, 0, 0};
static const canvas_t CV_HELP = {dlgbuf, 115, 0, 0, 0, 20, 4, 1, 1};

static void hud_stakes(void) {
    cv_begin(&CV_STAKES, 0);
    cv_bignumc(numstr(stakes_txt), 36, 1, 2, 1);
    cv_draw(&CV_STAKES);
}

static void hud_wins(void) {
    uint8_t i;
    char c[2] = {0, 0};
    cv_begin(&CV_WINS, 0);
    for (i = 0; i < 5; i++) {
        c[0] = wins[i];
        cv_print(c, 8 + i * 4, 1, (i + 1 == round_) ? 2 : 3);
    }
    cv_draw(&CV_WINS);
    cv_begin(&CV_ROUND, 0);
    if (bonusmode) cv_print("bonus", 8, 1, 1);
    else {
        s_begin(); s_str("round "); s_num(round_);
        cv_print(s_end(), 8, 1, 1);
    }
    cv_draw(&CV_ROUND);
}

static void hud_winv(void) {
    cv_begin(&CV_WINV, 0);
    cv_rprint(mlstr(wining_txt, 1), 52, 1, 3);
    cv_draw(&CV_WINV);
    cv_begin(&CV_WIND, 0);
    cv_rprint(losses_desc ? "losses" : "winnings", 52, 1, 1);
    cv_draw(&CV_WIND);
}

static int16_t hidcount(void) {
    uint8_t i, n = 0;
    for (i = 0; i < 9; i++)
        if (cards[i].hidden || cards[i].badstab) n++;
    return n;
}

static int16_t calcstabonus(void) {
    return (stabonus + hidcount() - 1) * mult;
}

static int16_t passcost(void) {
    return (scoremode ? -2 : -2 - chain) * mult;
}

static uint8_t bonus_shown, pass_shown, cost_shown;
static int16_t bonus_val;
uint8_t bonus_dirty = 1;   // stake bonus only changes when cards flip / a round starts

// HUD canvases are redrawn through a queue: at most one per frame (see game_frame),
// so several HUD changes landing together never push a frame over budget.
static uint8_t hud_pend, help_busy;
#define HP_STAKES 1
#define HP_WINV 2
#define HP_BONUS 4
#define HP_PASS 8
static uint8_t bonus_want, pass_want;

static void hud_bonus_check(void) {
    uint8_t vis = 1;   // slides away with the stake instead (draw_sprites)
    uint8_t hl = cur == 16 || curstab;
    if (bonus_dirty) {
        int16_t v = calcstabonus();
        bonus_dirty = 0;
        if (v != bonus_val) {
            bonus_val = v;
            bonus_shown = 255;
        }
    }
    bonus_want = vis | (hl << 1);
    if (bonus_want != bonus_shown) hud_pend |= HP_BONUS;
}

static int16_t bonus_rend[2] = {-32768, -32768};   // [0]: value in the sprite tiles, [1]: its hl

// colours: frame 3 and text 2 in obj palette 4 (stake: 10, 2), highlight text 1 in palette 5
// (14); the black inside is left transparent over the black board
static void hud_bonus_draw(void) {
    uint8_t hl = bonus_want >> 1, c;
    bonus_shown = bonus_want;
    if (bonus_rend[0] != bonus_val || bonus_rend[1] != hl) {
        const char *t = mlstr(bonus_val, 0);
        // DMA source: past what the HUD canvases, the board swap and the wave use of cvbuf
        uint8_t w = text_w(t) + 7, *b = (uint8_t *)(((uint16_t)cvbuf + 15) & ~15u) + 2048;
        cv_begin(&CV_BONUS, 0);
        cv_frame(16 - w / 2, 0, w + 1, 9, 3);   // same pixels as rrectfill + inner black
        cv_cprint(t, 17, 2, hl ? 1 : 2);
        for (c = 0; c < 4; c++) {   // canvas rows of tiles -> 8x16 sprite pairs
            memcpy(b + c * 32, cvbuf + c * 16, 16);
            memcpy(b + c * 32 + 16, cvbuf + (4 + c) * 16, 16);
        }
        vram_dma_vbl(b, (uint8_t *)0x8000u + (BONUS_TILE << 4), 0, 8, _current_bank);
        bonus_rend[0] = bonus_val;
        bonus_rend[1] = hl;
    }
}

// the stake at rest: the box in the BG; moving: off the BG (draw_sprites shows sprites)
static void bonus_place(void) {
    // drawn from what is wanted now, not the sprite tiles: those wait in the HUD queue
    // (behind the help text after a deal), and the highlight would linger
    uint8_t hl = bonus_want >> 1;
    if (!stabx && bonus_rend[0] != -32768) {
        if (!bonus_bg || bg_val != bonus_val || bg_hl != hl) {
            const char *t = mlstr(bonus_val, 0);
            uint8_t w = text_w(t) + 7;
            cv_begin(&CV_BONUS_BG, 0);
            cv_frame(20 - w / 2, 2, w + 1, 9, 1);
            cv_cprint(t, 21, 4, hl ? 3 : 2);
            cv_draw(&CV_BONUS_BG);
            bonus_bg = 1;
            bg_val = bonus_val;
            bg_hl = hl;
        }
    } else if (bonus_bg) {
        map_tiles(0, 0, 1, 5, 2, 0);
        bonus_bg = 0;
    }
}

static void hud_bonus(void) {
    hud_bonus_check();
    if (hud_pend & HP_BONUS) {
        hud_pend &= ~HP_BONUS;
        hud_bonus_draw();
    }
}

static void hud_pass_check(void) {
    uint8_t vis = !bonusmode && result == 0;
    pass_want = vis | ((cur == 17) << 1);
    if (pass_want != pass_shown) hud_pend |= HP_PASS;
}

// renders both pass button variants and the cost label (cost only changes per match)
static void hud_pass_render(void) {
    uint8_t k;
    for (k = 0; k < 2; k++) {
        const canvas_t *c = k ? &CV_PASS_HL : &CV_PASS;
        cv_begin(c, 0);
        cv_frame(1, 2, 23, 10, 2);
        cv_print("pass", 5, 4, k ? 1 : 2);
        cv_flush(c);
    }
    cv_begin(&CV_COST, 0);
    cv_cprint(mlstr(passcost(), 0), 21, 2, 2);
    cv_flush(&CV_COST);
    cv_place_attrs(&CV_PASS);
    cv_place_attrs(&CV_COST);
}

static void hud_pass_draw(void) {
    uint8_t vis = pass_want & 1, sel = pass_want >> 1;
    pass_shown = pass_want;
    if (!vis) {
        map_tiles(0, 1, 12, 3, 2, 0);
        map_tiles(0, 0, 11, 5, 1, 0);
        return;
    }
    cv_place_tiles(sel ? &CV_PASS_HL : &CV_PASS);
    if (sel) cv_place_tiles(&CV_COST);
    else map_tiles(0, 0, 11, 5, 1, 0);
}

static void hud_pass(void) {
    hud_pass_check();
    if (hud_pend & HP_PASS) {
        hud_pend &= ~HP_PASS;
        hud_pass_draw();
    }
}

static void draw_slot(uint8_t i, uint8_t s) {
    static const uint8_t sidx[11] = {0, 1, 2, 3, 4, 0, 0, 0, 0, 5, 6};
    uint8_t k = sidx[s];
    if (i < 3) lift_top[i] = k;
    map_put_vbl(slot_tx[i], slot_ty[i], 3, 2, slot_map + k * 6, slot_attr + k * 6);
    gs_drawn[i] = s;
    if (i < 3 && cards[i].lift) lift_redraw(i);   // the card's overlap row was just covered
}

static void hud_all(void) {
    bonus_shown = pass_shown = 255;
    bonus_rend[0] = bonus_rend[1] = -32768;
    hud_pend = 0;
    hud_pass_render();
    hud_stakes();
    hud_wins();
    hud_winv();
    hud_bonus();
    hud_pass();
}

static void set_card_fr(uint8_t i, uint8_t fr) {
    if (cards[i].fr == fr) return;
    cards[i].fr = lift_cfr[i] = fr;
    if (fr == CARD_NONE) map_put_vbl(CELLX(i), CELLY(i), 3, 4, 0, 0);
    else map_put_vbl(CELLX(i), CELLY(i), 3, 4, card_fmap + fr * 12, card_fattr + fr * 12);
}

static uint8_t face_of(uint8_t i) {
    uint8_t v = cards[i].val;
    return v == 10 ? vampval : v;
}

// card hop / flip: see lift.c
static void card_land(uint8_t ci) {
    uint8_t a, f;
    lift_end(cards[ci].lift);
    cards[ci].lift = 0;
    set_card_fr(ci, cards[ci].hidden ? CF_BACK : CF_FACE(face_of(ci)));
    // give the overlapped tile row back to its owner
    if (ci < 3) gs_drawn[ci] = 255;
    else {
        a = ci - 3;
        if (!cards[a].lift) {
            f = cards[a].fr;
            cards[a].fr = LIFT_FR_LIFTED;
            set_card_fr(a, f);
        }
    }
}

// ------------------------------------------------------------------ chips
static chip_t *newchip(void) {
    uint8_t i;
    for (i = 0; i < MAXCH; i++)
        if (!chips[i].used) {
            memset(&chips[i], 0, sizeof(chip_t));
            chips[i].used = 1;
            if (i >= chip_hi) chip_hi = i + 1;
            return &chips[i];
        }
    return &chips[MAXCH - 1];
}

static void dochip(chip_t *c) {
    if (c->rest) return;
    if (c->delay) { c->delay--; return; }
    if (c->dx - c->x >= 1 || c->x - c->dx >= 1) c->x += c->dx > c->x ? 1 : -1;
    if (c->dy - c->y >= 1 || c->y - c->dy >= 1) c->y += c->dy > c->y ? 1 : -1;
    c->z += c->zd;
    c->zd -= 64;
    if (c->z <= c->zbase) {
        c->z = c->zbase;
        if (c->zd > 384 || c->zd < -384) {
            c->zd = -((c->zd >> 2) + (c->zd >> 5) + (c->zd >> 6));   // bounce: ~0.3x
            sfx(58);
        } else c->zd = 0;
    }
    if ((c->delme && c->y - (c->z >> 8) <= -10) || c->x <= -14) c->used = 0;
    else if (!c->zd && c->z == c->zbase && c->x == c->dx && c->y == c->dy && !c->delme) c->rest = 1;
}

void dochips(void) BANKED {
    chip_t *c = chips;
    uint8_t i;
    for (i = chip_hi; i; i--, c++)
        if (c->used && !c->rest) dochip(c);
}

static void swipechips(uint8_t ci) {
    uint8_t i;
    uint16_t m = 1 << ci;
    for (i = 0; i < chip_hi; i++)
        if (chips[i].used && (chips[i].cardmask & m)) {
            chips[i].zd = 8 * 256;
            chips[i].delme = 1;
            chips[i].rest = 0;
            sfx(58);
        }
}

static void dropchip1(uint8_t ci, uint8_t delay, int16_t z) {
    chip_t *c = newchip();
    uint8_t h = cards[ci].hint;
    if (h < 2) h = 2;
    c->x = c->dx = CCX(ci) - 8;
    c->y = c->dy = CCY(ci) - 3;
    c->z = z;
    c->zd = z ? 0 : 512;
    c->tile = SPR_TOK2 + (h - 2) * 4;
    c->pal = SPRPAL_TOK2;
    c->w16 = 1;
    c->delay = delay;
    c->cardmask = 1 << ci;
    cards[ci].had1 = 1;
}

static uint8_t neighbor(uint8_t ci, uint8_t dir) {
    // 0 left, 1 right, 2 up, 3 down ; 255 = none
    uint8_t col = ci % 3, row = ci / 3;
    if (dir == 0) return col ? ci - 1 : 255;
    if (dir == 1) return col < 2 ? ci + 1 : 255;
    if (dir == 2) return row ? ci - 3 : 255;
    return row < 2 ? ci + 3 : 255;
}

static uint8_t hasouts(uint8_t ci) {
    uint8_t d, n;
    for (d = 0; d < 4; d++) {
        n = neighbor(ci, d);
        if (n != 255 && cards[n].hidden) return 1;
    }
    return 0;
}

static void dropchip3(uint8_t ci) {
    static const int8_t ox[4] = {-15, 8, -4, -4}, oy[4] = {-3, -3, -18, 11};
    static const uint8_t lowt[4] = {SPR_ARR_L, SPR_ARR_R, SPR_ARR_U, SPR_ARR_D};
    static const uint8_t hight[4] = {SPR_ARR_R, SPR_ARR_L, SPR_ARR_D, SPR_ARR_U};
    uint8_t d, n;
    cards[ci].had3 = 1;
    for (d = 0; d < 4; d++) {
        n = neighbor(ci, d);
        if (n != 255 && cards[n].hidden) {
            chip_t *c = newchip();
            c->x = CCX(ci) - 4;
            c->y = CCY(ci) - 3;
            c->dx = CCX(ci) + ox[d];
            c->dy = CCY(ci) + oy[d];
            c->zd = 512;
            c->tile = cards[n].val < cards[ci].val ? lowt[d] : hight[d];
            c->pal = SPRPAL_ARR_L;
            c->cardmask = (1 << ci) | (1 << n);
        }
    }
}

static void dropchip9(void) {
    uint8_t i, j, ten, hid, bestc = 0, r;
    int16_t best = -1, sc;
    for (i = 0; i < 4; i++) {
        ten = 0;
        hid = 0;
        for (j = 0; j < 4; j++) {
            if (cards[bxs[i][j]].val == 10) ten = 1;
            if (cards[bxs[i][j]].hidden) hid++;
        }
        if (ten) {
            r = rnd(255);
            sc = (int16_t)hid * 256 + r;
            if (sc > best) { best = sc; bestc = i; }
        }
    }
    dangrect = bestc;
    drectsani = 20 * 256;
    ldrect = 5;
}

static void dodroptoken(void) {
    uint8_t cand[9], nc = 0, i, k;
    for (i = 0; i < 9; i++)
        if (cards[i].hidden) cand[nc++] = i;
    for (i = 0; i < droptoken && nc; i++) {
        k = rnd(nc);
        // falls in from above the screen
        dropchip1(cand[k], i * 10, (CCY(cand[k]) - 3 + 12) * 256);
        cand[k] = cand[--nc];
    }
}

// ------------------------------------------------------------------ round flow
uint8_t pend_n, pend_i;
const char *pend_lbl[5];
int16_t pend_val[5];
uint8_t pend_kind[5];         // 0 value, 1 no value, 2 continue
static char lblbuf[5][20];

static void scoreline(const char *lbl, int16_t v, uint8_t kind) {
    strcpy(lblbuf[pend_n], lbl);
    pend_lbl[pend_n] = lblbuf[pend_n];
    pend_val[pend_n] = v;
    pend_kind[pend_n] = kind;
    pend_n++;
}

static void stabcard(uint8_t ci) {
    swipechips(ci);
    stabdy = -128 * 16;
    pflockdest = CCY(ci) - 17;
    pflockx = CCX(ci) - 4;
}

// the cursor rests on face-down card ci (255: none): it and its chips sit 1 px up
static void card_hover(uint8_t ci) {
    uint8_t i;
    uint16_t m = ci < 9 && cards[ci].hidden ? 1 << ci : 0;
    for (i = 0; i < 9; i++) czb[i] = (m >> i) & 1 ? 256 : 0;
    for (i = 0; i < MAXCH; i++)
        if (chips[i].used && (chips[i].zbase != 0) != ((chips[i].cardmask & m) != 0)) {
            chips[i].zbase = (chips[i].cardmask & m) ? 256 : 0;
            chips[i].rest = 0;
        }
}

enum { EG_WIN, EG_LOSE, EG_PASS, EG_STAB, EG_BADSTAB };

static void endgame(uint8_t how) {
    uint8_t crds;
    card_hover(255);
    tempsave();
    musiclvl(0);
    result = 1;
    wins[round_ - 1] = 'w';
    showcur = 0;
    pend_n = pend_i = 0;
    crds = 9 - hidcount();
    if (!bonusmode) {
        s_begin(); s_str("flipped "); s_num(crds); s_str(" cards");
        scoreline(s_end(), crds * mult, 0);
    }
    switch (how) {
    case EG_WIN:
        stabcard(vcard);
        if (bonusmode) scoreline("winnings doubled", 0, 1);
        else scoreline("vampire evaded", 2 * mult, 0);
        break;
    case EG_LOSE:
        result = 2;
        wins[round_ - 1] = 'l';
        shake = 2;
        sfx(40);
        if (bonusmode) scoreline("winnings lost", 0, 1);
        break;
    case EG_PASS:
        result = 3;
        wins[round_ - 1] = 'p';
        scoreline("pass penalty", passcost(), 0);
        break;
    case EG_STAB:
        scoreline("vampire evaded", 2 * mult, 0);
        scoreline("stab bonus", calcstabonus(), 0);
        break;
    case EG_BADSTAB:
        result = 2;
        wins[round_ - 1] = 'l';
        scoreline("stab penalty", calcstabonus(), 0);
        sfx(40);
        break;
    }
    scoreline("\x01 continue", 0, 2);
    hud_wins();
}

static void st_raise(int16_t v) {
    stakes += mult * v;
}

static void flipbegin(uint8_t ci) {
    swipechips(ci);
    cards[ci].ani = 1;
    cards[ci].t = 0;
    sfx(62);
}

static void cardflip(uint8_t ci) {
    uint8_t hc, l, k, n;
    bonus_dirty = 1;
    if (cards[ci].badstab) {
        endgame(EG_BADSTAB);
        return;
    } else if (!bonusmode) {
        st_raise(1);
        stabonus--;
        if (stabonus <= 0) stabdx = -30 * 16;
    }
    hc = hidcount();
    for (l = 0; l < 6; l++) {
        n = 0;
        for (k = 0; k < 3; k++)
            if (!cards[lines[l][k]].hidden) n++;
        if (n == 3 && (gs[l] == 2 || gs[l] == 4 || gs[l] == 10)) {
            gs[l]--;
            gzd[l] = 24;   // 1.5 px/frame
        }
    }
    if (cards[ci].val == 10) endgame(EG_LOSE);
    else if (hc == 1) endgame(EG_WIN);
    else if (!bonusmode) {
        if (hc <= 3) musiclvl(3);
        else if (hc <= 5) musiclvl(2);
        else if (hc <= 7) musiclvl(1);
    }
}

void startmatch(void) BANKED {
    musiclvl(0);
    music(1);
    round_ = 0;
    wining = wining_txt = 0;
    losses_desc = 0;
    strcpy(wins, ".....");
    stakes_txt = 0;
    stabx = -20 * 16;
    staby = 0;
    dealround();
}

// ------------------------------------------------------------------ input
static void returnchip(void) {
    gs[curchipi] = curchip;
    gzd[curchipi] = 13;   // 0.8 px/frame
    curchip = 0;
    sfx(59);
}

static void dobutts(void) {
    if (BTNP(J_A)) {
        if (cur <= 9) {
            card_t *c = &cards[cur - 1];
            if (curchip) {
                if (curchip == 1) {
                    if (c->hidden && !c->had1) {
                        dropchip1(cur - 1, 0, 0);
                        curchip = 0;
                        sfx(60);
                    } else { sfx(61); curshake = 10; }
                } else if (curchip == 3) {
                    if (!c->hidden && !c->had3 && hasouts(cur - 1)) {
                        dropchip3(cur - 1);
                        curchip = 0;
                        sfx(60);
                    } else { sfx(61); curshake = 10; }
                }
            } else if (curstab) {
                if (c->hidden) {
                    stabcard(cur - 1);
                    pflockard = cur - 1;
                } else { sfx(61); curshake = 10; }
            } else if (c->hidden) {
                flipbegin(cur - 1);
            } else if (!c->ani) {
                int16_t *d = &czd[cur - 1];
                *d = *d ? -*d / 2 : 205;   // 0.8
            }
        } else if (cur < 16) {
            if (curchip) returnchip();
            else if (curstab) { curstab = 0; sfx(59); }
            else {
                uint8_t s = gs[cur - 10];
                if (s == 1 || s == 3) {
                    curchip = s;
                    curchipi = cur - 10;
                    gs[cur - 10] = 0;
                    sfx(60);
                } else if (s == 9) {
                    dropchip9();
                    gs[cur - 10] = 0;
                    sfx(60);
                }
            }
        } else if (cur == 16) {
            if (curchip) returnchip();
            else if (curstab) { curstab = 0; sfx(59); }
            else { curstab = 1; sfx(60); }
        } else if (cur == 17) {
            if (curchip) returnchip();
            else if (curstab) { curstab = 0; sfx(59); }
            else {
                endgame(EG_PASS);
                sfx(51);
            }
        }
    } else if (BTNP(J_B)) {
        if (curchip) returnchip();
        else if (curstab) { curstab = 0; sfx(59); }
    }
}

// ------------------------------------------------------------------ rendering
static const uint8_t held_tile[11] = {0, SPR_CHIP_PLUS, 0, SPR_CHIP_ARROWS, 0, 0, 0, 0, 0, SPR_CHIP_BOX, 0};
static const uint8_t held_pal[11] = {0, SPRPAL_CHIP_PLUS, 0, SPRPAL_CHIP_ARROWS, 0, 0, 0, 0, 0, SPRPAL_CHIP_BOX, 0};

static void draw_sprites(void) {
    uint8_t i;
    int16_t x = curx >> 8, y = cury >> 8;
    // cursor first so it always wins the per-line sprite limit
    if (showcur) {
        if (curshake) {
            curshake--;
            x += (frames & 2) ? 1 : -1;
        }
        if (curstab) {
            spr_put16(x + 3, y - (cur <= 9 ? 7 : 0), SPR_STABCUR, SPRPAL_STABCUR);
        } else {
            spr_put16(x, y, SPR_HAND, SPRPAL_HAND);
        }
        if (curchip) spr_put16(x - 3, y - 6, held_tile[curchip], held_pal[curchip]);
    }
    // stake
    if (!curstab) {
        int16_t sx = 16 + (stabx >> 4), sy = 28 + (staby >> 4);
        for (i = 0; i < 4; i++) spr_put(sx, sy + i * 16, SPR_STAKE + i * 2, SPRPAL_STAKE);
        if (cur == 16 && showcur) {
            // pink outline drawn behind the stake (lower OAM priority)
            for (i = 0; i < 4; i++) spr_put16(sx - 1, sy - 1 + i * 16, SPR_STAKE_HL + i * 4, SPRPAL_STAKE_HL);
        }
    }
    // falling stake
    if (pflockdest > 0) {
        uint8_t t = pflocky >= pflockdest ? SPR_PFLOCKCUT : SPR_PFLOCK;
        spr_put(pflockx, pflocky, t, SPRPAL_PFLOCK);
        if (pflocky + 16 < pflockdest + 20) spr_put(pflockx, pflocky + 16, t + 2, SPRPAL_PFLOCK);
    }
    // danger rect corners
    if (dangrect >= 0) {
        uint8_t dr;
        int16_t x0, y0;
        drectsani -= drectsani >> 6;
        if (drectsani < 256) drectsani = 0;
        dr = (uint8_t)(dangrect + (drectsani >> 8)) & 3;
        if (ldrect != dr) { ldrect = dr; sfx(57); }
        x0 = (5 + brect_c[dr] * 3) * 8;
        y0 = (2 + brect_r[dr] * 4) * 8;
        spr_put(x0, y0, SPR_CORNER, SPRPAL_CORNER);
        spr_put(x0 + 40, y0, SPR_CORNER, SPRPAL_CORNER | 0x20);
        spr_put(x0, y0 + 48, SPR_CORNER, SPRPAL_CORNER | 0x40);
        spr_put(x0 + 40, y0 + 48, SPR_CORNER, SPRPAL_CORNER | 0x60);
    }
    {
        chip_t *c = chips;
        for (i = chip_hi; i; i--, c++) {
            int16_t sy;
            if (!c->used) continue;
            sy = c->y - (int8_t)((uint8_t *)&c->z)[1];   // z is 8.8: integer part = high byte
            if (c->w16) spr_put16(c->x, sy, c->tile, c->pal);
            else spr_put(c->x, sy, c->tile, c->pal);
        }
    }
    // slot chips in the air (gs_drawn 0: the slot shows the empty base), and until the
    // slot's own chip is back on screen (map blocks can be deferred a frame)
    for (i = 0; i < 6; i++) {
        uint8_t t = held_tile[gs[i]];
        if (t && (!gs_drawn[i] || map_vbl_pending(slot_tx[i], slot_ty[i])))
            spr_put16(slot_tx[i] * 8 + 5, slot_ty[i] * 8 + 3 - ((gz[i] + 15) >> 4), t, held_pal[gs[i]]);
    }
    {   // stake bonus box, moving with the stake: centred 4 px right of it (bonx = 17 - bonw / 2
        // + stabx). Before the cards flying in: on its lines (10-25) at most two of them fly at
        // once, 6 sprites, so it keeps its 4 of the 10 a line holds.
        int16_t bx = 4 + (stabx >> 4);
        if (stabx && bx > -32 && bonus_rend[0] != -32768)
            for (i = 0; i < 4; i++)
                spr_put(bx + i * 8, 10, BONUS_TILE + i * 2, bonus_rend[1] ? SPRPAL_PFLOCK : SPRPAL_STAKE);
    }
    {   // cards flying in (dealing)
        uint8_t k;
        int16_t x, y;
        for (k = 0; k < 9; k++) {
            if (cards[k].ani != 2 || !cards[k].t) continue;
            x = fx[k] >> 4;
            y = (fy[k] >> 4) - (fz[k] >> 8);
            for (i = 0; i < 6; i++) {
                int16_t py = y + (i / 3) * 16, px = x + (i % 3) * 8;
                if (py > -16 && px > -8 && px < 168)
                    spr_put(px, py, SPR_CARDBACK + i * 2, SPRPAL_CARDBACK);
            }
        }
    }
    lift_sprites();   // card shadows under everything else
}

static void doshake(void) {
    if (shake) {
        cam_x = (int8_t)rnd(shake) - shake / 2;
        cam_y = (int8_t)rnd(shake) - shake / 2;
        shake--;
        if (shake > 10) shake = shake * 9 / 10;
    } else cam_x = cam_y = 0;
}

// The help box is rendered into its own buffer, then uploaded one tile row per
// frame while the window slides up. Returns 1 if it did VRAM work this frame.
static uint8_t help_rows = 4;

static uint8_t help_update(void) {
    uint8_t busy = 0;
    int16_t target = helpi ? 112 : 144;
    if (helpi && helpi != help_drawn) {
        cv_begin(&CV_HELP, 0);
        cv_frame(2, 1, 156, 29, 1);
        {
            char *t = (char *)cvbuf + 2200;   // the texts live in save.c's bank (spare cvbuf)
            help_text(helpi - 1, t);
            cv_print(t, 7, 5, 2);
        }
        cv_place(&CV_HELP);
        help_drawn = helpi;
        help_rows = 0;
    } else if (!helpi && help_drawn && helpwy < 144) {   // slides out empty, as the original
        cv_begin(&CV_HELP, 0);
        cv_frame(2, 1, 156, 29, 1);
        help_drawn = 0;
        help_rows = 0;
    }
    if (help_rows < 4) {
        cv_flush_rows(&CV_HELP, help_rows++, 1);
        busy = 1;
    }
    if (helpwy != target) helpwy += (target - helpwy + (target > helpwy ? 3 : -3)) >> 2;
    if (helpwy > 144) helpwy = 144;
    WY_REG = (uint8_t)helpwy;
    win_show = helpwy < 144;
    return busy;
}

void game_frame(void) BANKED {
    uint8_t i;
    doshake();
    for (i = 0; i < 6; i++) {
        uint8_t s = gs[i];
        if (s) {   // dochip: z += zd, gravity 0.25, bounce at |zd| > 1.5
            int8_t z = gz[i] + gzd[i];
            gzd[i] -= 4;
            if (z <= gzb[i]) {
                z = gzb[i];
                if (gzd[i] < -24) {
                    gzd[i] = (int8_t)((uint8_t)(-gzd[i]) * 5 >> 4);   // * 0.3
                    sfx(58);
                } else gzd[i] = 0;
            }
            gz[i] = z;
            if (z && held_tile[s]) s = 0;
        } else gz[i] = gzd[i] = 0;
        if (s != gs_drawn[i]) draw_slot(i, s);
    }
    lift_update();
    draw_sprites();
    if (gtxt_changed & 1) hud_pend |= HP_STAKES;
    if (gtxt_changed & 2) hud_pend |= HP_WINV;
    gtxt_changed = 0;
    hud_bonus_check();
    hud_pass_check();
    // one HUD canvas per frame, cursor feedback first; none while the help box renders (but
    // the stake's box when it has none yet: it comes in with the stake as the cards are dealt)
    if ((hud_pend & HP_BONUS) && bonus_rend[0] == -32768) {
        hud_pend &= ~HP_BONUS;
        hud_bonus_draw();
    } else if (hud_pend && !help_busy) {
        if (hud_pend & HP_PASS) { hud_pend &= ~HP_PASS; hud_pass_draw(); }
        else if (hud_pend & HP_BONUS) { hud_pend &= ~HP_BONUS; hud_bonus_draw(); }
        else if (hud_pend & HP_STAKES) { hud_pend &= ~HP_STAKES; hud_stakes(); }
        else { hud_pend &= ~HP_WINV; hud_winv(); }
    }
    help_busy = 0;
    bonus_place();
#ifdef DEBUG_FPS
    if ((frames & 63) == 0) {
        cv_begin(&CV_STLBL, 0);
        s_begin(); s_num((int16_t)(dbg_loops & 0x7FFF)); s_str("/"); s_num((int16_t)(sys_time & 0x7FFF));
        cv_print(s_end(), 0, 1, 1);
        cv_draw(&CV_STLBL);
    }
#endif
    frame();
}

static void board_setup(void) {
    uint8_t i;
    reset_screen();
    load_bkg_banked(BANK(game_tiles), game_tiles, 1, N_GAME_TILES, 0);
    set_bgpal(0, 0, 2, 8, 9);
    card_pals_load();
    copy_bgpals(5, 3, card_pals);  // placeholder, replaced below
    set_bgpal(5, 0, 1, 7, 8);
    set_bgpal(6, 0, 1, 8, 15);
    set_bgpal(7, 0, 1, 13, 6);
    copy_objpals(0, 8, obj_pals);
    lift_luts();
    map_put(0, 0, 14, 20, 1, sep_map, sep_attr);
    cv_begin(&CV_STLBL, 0);
    cv_cprint("stakes", 24, 1, 1);
    cv_draw(&CV_STLBL);
    for (i = 0; i < 6; i++) gs_drawn[i] = 255;
    for (i = 0; i < 9; i++) cards[i].fr = 254;
    win_show = 0;
    helpwy = 144;
    help_drawn = 0;
    hud_all();
    screen_on();
}

// Swaps the board for the new round's in one VBlank, like the original (all cards gone at
// once, slots updated): BG map rows 0-13 are copied to RAM, the card area cleared and the
// slots drawn there, and the rows go back by CGB DMA (896 bytes, about 4 lines of VBlank).
// Block by block the cards would visibly vanish over a few frames.
#define RS_ROWS 14
static void board_reset_map(void) {
    static const uint8_t sidx[11] = {0, 1, 2, 3, 4, 0, 0, 0, 0, 5, 6};
    uint8_t *buf = (uint8_t *)(((uint16_t)cvbuf + 15) & ~15u);   // attrs, then tiles
    uint8_t *vp, *p, i, r, c, k, plane, hid;
    const uint8_t *src;
    // the old round's sprites stay up until the swap (the new round has cleared its chips)
    spr_hold = 1;
    while (map_vbl_queued() || vram_dma_busy()) frame();   // nothing pending may overwrite these rows
    for (plane = 0, p = buf; plane < 2; plane++) {
        VBK_REG = 1 - plane;
        for (vp = (uint8_t *)0x9800u; vp < (uint8_t *)0x9800u + RS_ROWS * 32; ) {
            CRITICAL {
                while (STAT_REG & 2);
                *p++ = *vp++;
            }
        }
    }
    VBK_REG = 0;
    for (plane = 0; plane < 2; plane++)
        for (r = CELLY(0); r < CELLY(6) + 4; r++) memset(buf + plane * RS_ROWS * 32 + r * 32 + CELLX(0), 0, 9);
    for (i = 0; i < 6; i++) {
        k = sidx[gs[i]];
        if (i < 3) lift_top[i] = k;
        gs_drawn[i] = gs[i];
        for (plane = 0; plane < 2; plane++) {
            src = (plane ? slot_map : slot_attr) + k * 6;
            for (r = 0; r < 2; r++)
                for (c = 0; c < 3; c++) buf[plane * RS_ROWS * 32 + (slot_ty[i] + r) * 32 + slot_tx[i] + c] = *src++;
        }
    }
    // 896 bytes take more than a VBlank (gfx.c vram_dma_vbl): the rows under the score panel
    // go a frame early, unseen, the rest with the panel. At a match start the screen is still
    // black and all of it goes together, over as many VBlanks as it takes.
    hid = win_show ? (uint8_t)((WY_REG + 7) >> 3) : RS_ROWS;
    if (hid < RS_ROWS) {
        vram_dma_vbl(buf + hid * 32, (uint8_t *)0x9800u + hid * 32, 1, (RS_ROWS - hid) * 2, _current_bank);
        vram_dma_vbl(buf + RS_ROWS * 32 + hid * 32, (uint8_t *)0x9800u + hid * 32, 0, (RS_ROWS - hid) * 2,
                     _current_bank);
        do frame(); while (vram_dma_busy());
    }
    spr_hold = 0;
    vram_dma_vbl(buf, (uint8_t *)0x9800u, 1, hid * 2, _current_bank);
    vram_dma_vbl(buf + RS_ROWS * 32, (uint8_t *)0x9800u, 0, hid * 2, _current_bank);
    draw_sprites();   // frame() drops sprites not drawn again
    // the score panel goes in the same VBlank as the old cards: the VBlank interrupt applies
    // win_show, so it must not change just before (or in) the VBlank before frame()'s one;
    // the split and sprite cut stay on until then, like the panel
    while ((uint8_t)(LY_REG - 136) < 18u);
    win_show = 0;
    frame();
    win_split = 0;
    win_cut = 0;
}

static void round_setup_visuals(void) {
    uint8_t i;
    lift_reset();
    for (i = 0; i < 9; i++) cards[i].fr = lift_cfr[i] = CARD_NONE;
    memset(gz, 0, sizeof(gz));
    memset(gzd, 0, sizeof(gzd));
    memset(gzb, 0, sizeof(gzb));
    memset(cz, 0, sizeof(cz));
    memset(czd, 0, sizeof(czd));
    memset(czb, 0, sizeof(czb));
    hud_all();   // before the swap, so its map rows are part of it
    board_reset_map();
    curx = (int16_t)curpos[cur - 1][0] << 8;
    cury = (int16_t)curpos[cur - 1][1] << 8;
}

// an idle card's height: raised frames while it is off the board, landing when it is back
static void card_z(uint8_t i) {
    card_t *c = &cards[i];
    uint8_t off;
    if (cz[i] == czb[i] && !czd[i] && !c->lift) return;   // at rest
    cz[i] += czd[i];
    czd[i] -= 38;   // 0.15
    if (cz[i] <= czb[i]) {
        cz[i] = czb[i];
        czd[i] = 0;
    }
    off = (uint8_t)((cz[i] + 128) >> 8);   // drawn at y - z, rounded
    if (off > 3) off = 3;
    if (!off) {
        if (c->lift) card_land(i);
        return;
    }
    if (!c->lift) {
        if (!(c->lift = lift_begin(i))) return;   // both slots flipping: stays flat
        c->fr = LIFT_FR_LIFTED;
    }
    lift_set(i, c->hidden ? LIFT_FR_HOVER : LIFT_FR_RAISED + off, face_of(i));
}

// card animations; returns flags: bit0 noani, bit1 canact
static uint8_t do_cards(void) {
    uint8_t i, noani = 1, canact = 1;
    card_t *c = cards;
    for (i = 0; i < 9; i++, c++) {
        if (!c->ani && !c->delay) {   // idle card
            card_z(i);
            continue;
        }
        if (c->delay) {
            c->delay--;
            canact = 0;
            continue;
        }
        if (c->ani == 2) {  // dealing: flying in
            int16_t tx = (int16_t)(CELLX(i) * 8) << 4, ty = (int16_t)(CELLY(i) * 8) << 4, d;
            if (!c->t) {
                c->t = 1;
                sfx(58);
            }
            d = tx - fx[i];
            // within a pixel: there (the original's 0.3 px tail would keep several cards
            // in the air on the same lines, past the 10 sprites a line)
            fx[i] = d > -16 && d < 16 ? tx : fx[i] + d / 5;
            d = ty - fy[i];
            fy[i] = d > -16 && d < 16 ? ty : fy[i] + d / 5;
            fz[i] += fzd[i];
            fzd[i] -= 38;
            if (fz[i] <= 0) fz[i] = fzd[i] = 0;
            canact = 0;
            if (fx[i] == tx && fy[i] == ty) {   // landed: on the board
                c->ani = 0;
                set_card_fr(i, c->hidden ? CF_BACK : CF_FACE(face_of(i)));
            }
        } else if (c->ani == 1) {
            uint8_t t = c->t, f = CF_FACE(face_of(i));
            noani = 0;
            if (t == 0) {
                cz[i] = czd[i] = czb[i] = 0;
                if (!c->lift) {
                    if (!(c->lift = lift_begin(i))) {   // both slots busy: a raised card gives one up
                        uint8_t k;
                        for (k = 0; k < 9; k++)
                            if (cards[k].lift && !cards[k].ani) { card_land(k); cz[k] = czd[k] = 0; break; }
                        c->lift = lift_begin(i);
                    }
                }
                if (c->lift) c->fr = LIFT_FR_LIFTED;
            }
            if (c->lift) lift_set(i, t, face_of(i));
            else if (t < 3) set_card_fr(i, CF_BACK);
            else if (t < 6) set_card_fr(i, CF_BACK + 1);
            else if (t < 9) set_card_fr(i, CF_BACK + 2);
            else if (t < 10) set_card_fr(i, CF_EDGE);
            else if (t < 13) set_card_fr(i, f + 2);
            else if (t < 17) set_card_fr(i, f + 1);
            else set_card_fr(i, f);
            if (t == 10) {
                c->hidden = 0;
                cardflip(i);
            }
            if (t < 10) canact = 0;
            c->t++;
            if (c->t > 20) {
                c->ani = 0;
                if (c->lift) card_land(i);
            }
        }
    }
    return noani | (canact << 1);
}

static void wait_board(uint8_t n) {
    while (n--) {
        dochips();
        game_frame();
    }
}

// ------------------------------------------------------------------ main game scene
void scene_game(void) BANKED {
    uint8_t fl, noani, canact, i;
    board_setup();
    round_setup_visuals();
    if (bonusmode) musiclvl(3);
    for (;;) {
        update_gtxt();
        if (wining_txt == wining && ticksfx && !bonusmode) ticksfx = 0;
        fl = do_cards();
        noani = fl & 1;
        canact = (fl >> 1) & 1;
        if (canact && autoflip != 255) {
            cards[autoflip].hidden = 0;
            dodroptoken();
            cards[autoflip].hidden = 1;
            flipbegin(autoflip);
            autoflip = 255;
            canact = 0;
        }
        dochips();
        // stake motion
        // easing with shifts instead of 16-bit divisions (~1/10 and 1/4 per frame)
        if (stabx != stabdx) {
            int16_t d = stabdx - stabx;
            int16_t s = (d >> 3) - (d >> 5);
            stabx += s ? s : (d > 0 ? 1 : -1);
        }
        if (staby != stabdy) {
            int16_t d = stabdy - staby;
            int16_t s = d >> 2;
            staby += s ? s : (d > 0 ? 1 : -1);
        }
        if (pflockdest > 0) {
            canact = 0;
            showcur = 0;
            if (pflockdest != pflocky && (staby - stabdy < 16 && stabdy - staby < 16)) {
                if (pflockdest - pflocky <= 4) {
                    pflocky = pflockdest;
                    shake = 10;
                    sfx(56);
                    for (i = 0; i < MAXCH; i++)
                        if (chips[i].used) {
                            chips[i].zd = 384;
                            chips[i].rest = 0;
                            chips[i].dx += (int16_t)rnd(20) - 10;
                            chips[i].dy += (int16_t)rnd(20) - 10;
                        }
                } else pflocky += 4;
            }
            if (pflockdest == pflocky && pflockard != 255) {
                if (pflockard == vcard) {
                    pflockard = 255;
                    endgame(EG_STAB);
                } else {
                    flipbegin(vcard);
                    cards[vcard].badstab = 1;
                    bonus_dirty = 1;
                    pflockard = 255;
                }
            }
        }
        if (result) {
            helpi = 0;
            help_update();
            if (noani && pflockdest == pflocky) {
                wait_board(20);
                if (result == 1) sfx(41);
                wait_board(50);
                if (score_phase()) {
                    dealround();
                    round_setup_visuals();
                    continue;
                }
                // match over
                {
                    uint8_t w = 0;
                    for (i = 0; i < 5; i++) if (wins[i] == 'w') w++;
                    scene = (w >= 3 && !bonusmode) ? SC_ASK : SC_WINING;
                }
                return;
            }
            game_frame();
            continue;
        }
        oldcur = cur;
        if (showcur) {
            if (BTNP(J_LEFT)) cur = curpos[cur - 1][3];
            else if (BTNP(J_RIGHT)) cur = curpos[cur - 1][4];
            else if (BTNP(J_UP)) cur = curpos[cur - 1][5];
            else if (BTNP(J_DOWN)) cur = curpos[cur - 1][2];
        }
        if (stabonus <= 0 && cur == 16) cur = oldcur;
        if (curchip && cur >= 16) cur = oldcur;
        if (bonusmode && cur >= 16) cur = oldcur;
        if (curstab && (cur > 16 || (cur > 9 && cur < 16))) cur = oldcur;
        if (oldcur != cur) {
            sfx(63);
            memset(gzb, 0, sizeof(gzb));
            card_hover(curchip || cur > 9 ? 255 : cur - 1);
            if (!curchip && cur >= 10 && cur <= 15) {
                uint8_t s = gs[cur - 10];
                if (s == 1 || s == 3) gzb[cur - 10] = 16;   // (the original also lifts the inactive box)
            }
        }
        if (canact && !result) {
            showcur = 1;
            dobutts();
        }
        {
            int16_t tx = (int16_t)curpos[cur - 1][0] << 8;
            int16_t ty = (int16_t)(curpos[cur - 1][1] - ((cur <= 9 && curchip) ? 7 : 0)) << 8;
            int16_t dx = tx - curx, dy = ty - cury;   // move 2/3 of the way (no 16-bit overflow)
            curx += (dx >> 1) + (dx >> 3) + (dx >> 4);   // ~2/3 of the way
            cury += (dy >> 1) + (dy >> 3) + (dy >> 4);
            if (dx > -256 && dx < 256) curx = tx;
            if (dy > -256 && dy < 256) cury = ty;
        }
        helpi = 0;
        if (canact && showcur) {
            if (curchip || (cur >= 10 && cur <= 15)) {
                uint8_t s = curchip ? curchip : gs[cur - 10];
                if (s == 1) helpi = 1;
                else if (s == 3) helpi = 2;
                else if (s == 9) helpi = 6;
                else if (s) helpi = cur <= 12 ? 4 : 5;
            } else if (cur == 16 || curstab) helpi = 3;
        }
        help_busy = help_update();
        game_frame();
    }
}

// ================================================================== main loop
void game_main(void) BANKED {
    initrand(DIV_REG);
    loadgame();
    scene = SC_CREDITS;
    music(0);
    for (;;) {
        switch (scene) {
        case SC_CREDITS: scene_credits(); break;
        case SC_START: scene_start(); break;
        case SC_INTRO: scene_intro(); break;
        case SC_MENU: scene_menu(); break;
        case SC_GAME: scene_game(); break;
        case SC_ASK: scene_ask(); break;
        case SC_WINING: scene_wining(); break;
        case SC_END: scene_end(); break;
        }
    }
}
