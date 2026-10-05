// Core gameplay: a direct port of the PICO-8 game's update_game / draw_game.
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include <rand.h>
#include "common.h"

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


static const char *const helptxt[6] = {
    "place on a face-down card.\ncard value is equal or\nhigher than token number",
    "place on a face-up card.\ncompares the values of\nadjecent face-down cards",
    "stab a card if you think\nit is a vampire. bonus ML\nif you get it right.",
    "flip all cards in this\ncolumn face-up to unlock\nthis hint token.",
    "flip all cards in this line\nface-up to unlock this\nhint token.",
    "highlights a 2x2 area where\nthe vampire card will be."};

// ------------------------------------------------------------------ save (cart SRAM)
typedef struct {
    uint8_t magic[4];
    int16_t blood, tries, chain, bchain;
    uint8_t unl[4];
    int16_t high[4];
    uint8_t wbin;
} save_t;
static save_t *const SAVEP = (save_t *)0xA000;
#define SAVE SAVEP

void savegame(void) BANKED {
    uint8_t i;
    ENABLE_RAM;
    SWITCH_RAM(0);
    SAVE->magic[0] = 'H'; SAVE->magic[1] = 'S'; SAVE->magic[2] = 'G'; SAVE->magic[3] = '1';
    SAVE->blood = blood;
    SAVE->tries = tries;
    SAVE->chain = chain;
    SAVE->bchain = bchain;
    SAVE->wbin = wbin;
    for (i = 0; i < 4; i++) {
        SAVE->unl[i] = unlocked[i];
        SAVE->high[i] = high[i];
    }
    DISABLE_RAM;
}

static void loadgame(void) {
    uint8_t i;
    ENABLE_RAM;
    SWITCH_RAM(0);
    if (SAVE->magic[0] == 'H' && SAVE->magic[1] == 'S' && SAVE->magic[2] == 'G' && SAVE->magic[3] == '1') {
        blood = SAVE->blood;
        tries = SAVE->tries;
        chain = SAVE->chain;
        bchain = SAVE->bchain;
        wbin = SAVE->wbin;
        for (i = 0; i < 4; i++) {
            unlocked[i] = SAVE->unl[i];
            high[i] = SAVE->high[i];
        }
        unlocked[0] = 1;
        DISABLE_RAM;
    } else {
        DISABLE_RAM;
        blood = tries = chain = bchain = 0;
        wbin = 0;
        for (i = 0; i < 4; i++) { unlocked[i] = i == 0; high[i] = 0; }
        savegame();
    }
}

void tempsave(void) BANKED {
    int16_t tb;
    if (scoremode) return;
    tb = blood;
    blood += wining;
    if (blood < 0) blood = 0;
    if (blood > 5000) blood = 5000;
    savegame();
    blood = tb;
}

void newgame(void) BANKED {
    tries++;
    blood = 20;
    unlocked[0] = 1;
    unlocked[1] = unlocked[2] = unlocked[3] = 0;
    savegame();
}

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

typedef struct {
    uint8_t val, hidden, hint, had1, had3, badstab;
    uint8_t ani;     // 0 none, 1 flip, 2 deal
    uint8_t t, fr, delay;
} card_t;
static card_t cards[9];
static uint8_t vcard, autoflip;

typedef struct {
    uint8_t used, tile, pal, w16, delay, delme;
    uint8_t rest;          // settled: skip physics until something moves it
    int16_t x, y, dx, dy;
    int16_t z, zd, zbase;  // 8.8
    uint16_t cardmask;
} chip_t;
#define MAXCH 24
static chip_t chips[MAXCH];
static uint8_t chip_hi;   // slots >= chip_hi are unused (loops stop there)

uint8_t gs[6];                // gchip state per slot (0 = gone)
static uint8_t gs_drawn[6];
uint8_t cur, oldcur, showcur, curchip, curchipi, curstab, curshake;
static int16_t curx, cury;    // 8.8
static int16_t stabx, stabdx, staby, stabdy;  // 12.4
static int8_t stabonus;
static int16_t pflockx, pflocky, pflockdest;
static uint8_t pflockard;
static int8_t dangrect;
static int16_t drectsani;     // 8.8
static uint8_t ldrect;
static uint8_t shake;
static uint8_t result;        // 0 none, 1 won, 2 lost, 3 pass
static uint8_t helpi, help_drawn;
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
static const canvas_t CV_BONUS = {cvbuf, 90, 0, 0, 1, 5, 2, 1, 0};
static const canvas_t CV_BONUS_HL = {cvbuf, 240, 0, 0, 1, 5, 2, 1, 0};
static const canvas_t CV_PASS = {cvbuf, 100, 0, 1, 12, 3, 2, 1, 0};
static const canvas_t CV_PASS_HL = {cvbuf, 106, 0, 1, 12, 3, 2, 1, 0};
static const canvas_t CV_COST = {cvbuf, 235, 0, 0, 11, 5, 1, 0, 0};
static const canvas_t CV_HELP = {dlgbuf, 115, 0, 0, 0, 20, 4, 1, 1};
static const canvas_t CV_BANNER = {cvbuf, 115, 0, 0, 0, 20, 2, 0, 1};
static canvas_t cv_line = {cvbuf, 155, 0, 0, 2, 20, 1, 0, 1};

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
static uint8_t bonus_dirty = 1;   // stake bonus only changes when cards flip / a round starts

// HUD canvases are redrawn through a queue: at most one per frame (see game_frame),
// so several HUD changes landing together never push a frame over budget.
static uint8_t hud_pend, help_busy;
#define HP_STAKES 1
#define HP_WINV 2
#define HP_BONUS 4
#define HP_PASS 8
static uint8_t bonus_want, pass_want;

static void hud_bonus_check(void) {
    uint8_t vis = stabdx == 0;
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

static int16_t bonus_rend[2] = {-32768, -32768};   // value rendered in each variant

static void hud_bonus_draw(void) {
    uint8_t vis = bonus_want & 1, hl = bonus_want >> 1;
    const canvas_t *c = hl ? &CV_BONUS_HL : &CV_BONUS;
    bonus_shown = bonus_want;
    if (!vis) {
        map_tiles(0, 0, 1, 5, 2, 0);
        return;
    }
    if (bonus_rend[hl] != bonus_val) {
        const char *t = mlstr(bonus_val, 0);
        uint8_t w = text_w(t) + 7;
        cv_begin(c, 0);
        cv_frame(20 - w / 2, 2, w + 1, 9, 1);   // same pixels as rrectfill + inner black
        cv_cprint(t, 21, 4, hl ? 1 : 2);
        cv_flush(c);
        bonus_rend[hl] = bonus_val;
    }
    cv_place_tiles(c);
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
    cv_place_attrs(&CV_BONUS);
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

static void draw_slot(uint8_t i) {
    static const uint8_t sidx[11] = {0, 1, 2, 3, 4, 0, 0, 0, 0, 5, 6};
    uint8_t k = sidx[gs[i]];
    map_put(0, slot_tx[i], slot_ty[i], 3, 2, slot_map + k * 6, slot_attr + k * 6);
    gs_drawn[i] = gs[i];
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
    cards[i].fr = fr;
    if (fr == CARD_NONE) map_put_vbl(CELLX(i), CELLY(i), 3, 4, 0, 0);
    else map_put_vbl(CELLX(i), CELLY(i), 3, 4, card_fmap + fr * 12, card_fattr + fr * 12);
}

static uint8_t face_of(uint8_t i) {
    uint8_t v = cards[i].val;
    return v == 10 ? vampval : v;
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

static void dochips(void) {
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
static uint8_t pend_n, pend_i;
static const char *pend_lbl[5];
static int16_t pend_val[5];
static uint8_t pend_kind[5];  // 0 value, 1 no value, 2 continue
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

enum { EG_WIN, EG_LOSE, EG_PASS, EG_STAB, EG_BADSTAB };

static void endgame(uint8_t how) {
    uint8_t crds;
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
            sfx(58);
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

void dealround(void) BANKED {
    uint8_t vals[9], nv = 9, i, k, h, nt;
    uint8_t targs[9];
    bonusmode = 0;
    stabdx = 0;
    stabdy = 0;
    stabonus = 7;
    pflockx = 0;
    pflocky = pflockdest = -28;
    pflockard = 255;
    curchip = 0;
    curstab = 0;
    showcur = 0;
    dangrect = -1;
    helpi = 0;
    if (staby != 0) { stabx = -20 * 16; staby = 0; }
    musiclvl(0);
    for (i = 0; i < 9; i++) vals[i] = i + 2;
    for (i = 0; i < 9; i++) {
        k = rnd(nv);
        memset(&cards[i], 0, sizeof(card_t));
        cards[i].val = vals[k];
        vals[k] = vals[--nv];
        cards[i].hidden = 1;
        cards[i].delay = 10 * (i + 1);
        cards[i].fr = 254;
        cards[i].ani = 2;
        if (cards[i].val == 10) vcard = i;
    }
    do autoflip = rnd(9); while (cards[autoflip].val == 10);
    cards[autoflip].hidden = 0;
    cur = autoflip + 1;
    for (h = 9; h >= 2; h--) {
        if (vampval == 13 && h == cards[autoflip].val) continue;  // asshole mode
        nt = 0;
        for (i = 0; i < 9; i++)
            if (cards[i].hidden && cards[i].val >= h && cards[i].hint == 0 && !(vampval == 13 && i == vcard)) targs[nt++] = i;
        if (nt) cards[targs[rnd(nt)]].hint = h;
    }
    if (vampval == 13) cards[vcard].hint = cards[autoflip].val;
    cards[autoflip].hidden = 1;
    for (i = 0; i < 6; i++) gs[i] = rnd(2) ? 4 : 2;
    if (dropboxes) {
        if (vampval == 13) {
            uint8_t va = vcard % 3, vb = 3 + vcard / 3;
            if (rnd(5) < 3) gs[rnd(2) ? va : vb] = 10;
            else {
                do k = rnd(6); while (k == va || k == vb);
                gs[k] = 10;
            }
        } else gs[rnd(6)] = 10;
    }
    memset(chips, 0, sizeof(chips));
    chip_hi = 0;
    result = 0;
    stakes = 0;
    round_++;
    bonus_dirty = 1;
}

void bonusround(void) BANKED {
    dealround();
    bonusmode = 1;
    stakes = wining;
    stabdx = -30 * 16;
}

// ------------------------------------------------------------------ input
static void returnchip(void) {
    gs[curchipi] = curchip;
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
static const uint8_t held_tile[10] = {0, SPR_CHIP_PLUS, 0, SPR_CHIP_ARROWS, 0, 0, 0, 0, 0, SPR_CHIP_BOX};
static const uint8_t held_pal[10] = {0, SPRPAL_CHIP_PLUS, 0, SPRPAL_CHIP_ARROWS, 0, 0, 0, 0, 0, SPRPAL_CHIP_BOX};

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
        cv_print(helptxt[helpi - 1], 7, 5, 2);
        cv_place(&CV_HELP);
        help_drawn = helpi;
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

static void game_frame(void) {
    uint8_t i;
    doshake();
    draw_sprites();
    for (i = 0; i < 6; i++)
        if (gs[i] != gs_drawn[i]) draw_slot(i);
    if (gtxt_changed & 1) hud_pend |= HP_STAKES;
    if (gtxt_changed & 2) hud_pend |= HP_WINV;
    gtxt_changed = 0;
    hud_bonus_check();
    hud_pass_check();
    // one HUD canvas per frame, cursor feedback first; none while the help box renders
    if (hud_pend && !help_busy) {
        if (hud_pend & HP_PASS) { hud_pend &= ~HP_PASS; hud_pass_draw(); }
        else if (hud_pend & HP_BONUS) { hud_pend &= ~HP_BONUS; hud_bonus_draw(); }
        else if (hud_pend & HP_STAKES) { hud_pend &= ~HP_STAKES; hud_stakes(); }
        else { hud_pend &= ~HP_WINV; hud_winv(); }
    }
    help_busy = 0;
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

static void round_setup_visuals(void) {
    uint8_t i;
    for (i = 0; i < 9; i++) {
        cards[i].fr = 254;
        set_card_fr(i, CARD_NONE);
        gs_drawn[i < 6 ? i : 0] = 255;
    }
    curx = (int16_t)curpos[cur - 1][0] << 8;
    cury = (int16_t)curpos[cur - 1][1] << 8;
    hud_all();
}

// card animations; returns flags: bit0 noani, bit1 canact
static uint8_t do_cards(void) {
    uint8_t i, noani = 1, canact = 1;
    card_t *c = cards;
    for (i = 0; i < 9; i++, c++) {
        if (!c->ani && !c->delay) continue;   // idle card
        if (c->delay) {
            c->delay--;
            canact = 0;
            continue;
        }
        if (c->ani == 2) {  // dealing: unfold the card back
            static const uint8_t dealfr[4] = {CF_EDGE, CF_BACK + 2, CF_BACK + 1, CF_BACK};
            set_card_fr(i, dealfr[c->t >> 1]);
            if (c->t == 0) sfx(58);
            c->t++;
            canact = 0;
            if (c->t >= 8) {
                c->ani = 0;
                set_card_fr(i, c->hidden ? CF_BACK : CF_FACE(face_of(i)));
            }
        } else if (c->ani == 1) {
            uint8_t t = c->t, f = CF_FACE(face_of(i));
            noani = 0;
            if (t < 3) set_card_fr(i, CF_BACK);
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
            if (c->t > 20) c->ani = 0;
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

// ------------------------------------------------------------------ score panel
static uint8_t score_phase(void) {
    // returns 1 = next round, 0 = match over
    int16_t wint = 100, wintstep = 60;
    int16_t wy = 112 * 16, wyd;
    uint8_t nl = 0, i, blink = 0;
    const char *btxt = result == 3 ? "pass" : result == 1 ? "round won" : "round lost";
    // window: banner + orange panel
    map_fill(1, 0, 0, 20, 18, TILE_SOLID(3), 0);
    cv_begin(&CV_BANNER, 2);
    cv_bignumc(btxt, 80, 3, 0, 0);
    cv_draw(&CV_BANNER);
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
        if (wint <= 0 && pend_i < pend_n) {
            i = pend_i++;
            cv_line.y = 2 + nl;
            cv_line.tile = 155 + nl * 20;
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
        // blink "continue"
        if (pend_i == pend_n && nl) {
            uint8_t b = (frames >> 5) & 1;
            if (b != blink) {
                blink = b;
                cv_begin(&cv_line, 3);
                if (b) cv_print(pend_lbl[pend_n - 1], 58, 1, 1);
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
                    while (stakes_txt != stakes || wining_txt != wining) {
                        update_gtxt();
                        game_frame();
                    }
                    win_split = 0;
                    win_cut = 0;
                    return 0;
                }
                win_split = 0;
                win_cut = 0;
                win_show = 0;
                return 1;
            }
        }
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
        if (oldcur != cur) sfx(63);
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
