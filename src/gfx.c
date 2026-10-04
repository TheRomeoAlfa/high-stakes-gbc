// Core rendering helpers (ROM bank 0): canvases, fonts, palettes, sprites, frame loop.
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include <rand.h>
#include "gen/assets.h"
#include "gfx.h"
#include "sound.h"

// colours 3, 4, 9, 10 use the cart's screen-palette remap (PICO-8 128, 129, 136, 130)
#define RGB5(r, g, b) ((uint16_t)((r) >> 3) | ((uint16_t)((g) >> 3) << 5) | ((uint16_t)((b) >> 3) << 10))
const uint16_t pico[16] = {
    RGB5(0, 0, 0), RGB5(29, 43, 83), RGB5(126, 37, 83), RGB5(0x29, 0x18, 0x14),
    RGB5(0x11, 0x1D, 0x35), RGB5(95, 87, 79), RGB5(194, 195, 199), RGB5(255, 241, 232),
    RGB5(255, 0, 77), RGB5(0xBE, 0x12, 0x50), RGB5(0x42, 0x21, 0x36), RGB5(0, 228, 54),
    RGB5(41, 173, 255), RGB5(131, 118, 156), RGB5(255, 119, 168), RGB5(255, 204, 170)};

uint8_t cvbuf[160 * 16];
uint8_t dlgbuf[64 * 16];

static uint8_t *cb;
static uint8_t cw, chh;   // canvas width/height in tiles
static int16_t cpw, cph;  // in pixels
static uint16_t rowbase[18];

void cv_select(const canvas_t *c) {
    uint8_t r;
    uint16_t o = 0;
    cb = c->buf;
    cw = c->w;
    chh = c->h;
    cpw = (int16_t)c->w * 8;
    cph = (int16_t)c->h * 8;
    for (r = 0; r < chh && r < 18; r++, o += (uint16_t)cw << 4) rowbase[r] = o;
}

void cv_begin(const canvas_t *c, uint8_t col) {
    uint16_t n;
    cv_select(c);
    n = (uint16_t)cw * chh * 16;
    if (col == 0) memset(cb, 0, n);
    else if (col == 3) memset(cb, 0xFF, n);
    else {
        uint8_t lo = (col & 1) ? 0xFF : 0, hi = (col & 2) ? 0xFF : 0;
        uint8_t *p = cb, *e = cb + n;
        while (p != e) { *p++ = lo; *p++ = hi; }
    }
}

void cv_pset(int16_t x, int16_t y, uint8_t col) {
    uint8_t *p, m;
    if (x < 0 || y < 0 || x >= cpw || y >= cph) return;
    p = cb + rowbase[(uint8_t)y >> 3] + ((uint16_t)((uint8_t)x >> 3) << 4) + (((uint8_t)y & 7) << 1);
    m = 0x80 >> ((uint8_t)x & 7);
    if (col & 1) p[0] |= m; else p[0] &= ~m;
    if (col & 2) p[1] |= m; else p[1] &= ~m;
}

// set the pixels of an 8-bit mask (bit7 = leftmost) whose left edge is at x
static void blit_row(int16_t x, int16_t y, uint8_t bits, uint8_t col) {
    uint8_t *p, sh, m;
    int16_t tx;
    if (y < 0 || y >= cph || !bits) return;
    if (x < 0) {
        if (x <= -8) return;
        bits <<= (uint8_t)(-x);
        x = 0;
    }
    if (x >= cpw) return;
    sh = (uint8_t)x & 7;
    tx = (uint8_t)x >> 3;
    p = cb + rowbase[(uint8_t)y >> 3] + ((uint16_t)tx << 4) + (((uint8_t)y & 7) << 1);
    m = bits >> sh;
    if (col & 1) p[0] |= m; else p[0] &= ~m;
    if (col & 2) p[1] |= m; else p[1] &= ~m;
    if (sh && tx + 1 < cw) {
        m = bits << (8 - sh);
        p += 16;
        if (col & 1) p[0] |= m; else p[0] &= ~m;
        if (col & 2) p[1] |= m; else p[1] &= ~m;
    }
}


// ---- hand-written SM83 inner loops (arguments passed through these globals)
static uint8_t *a_p;
static uint16_t a_skip;
static uint8_t a_r0, a_rows, a_m, a_lom, a_him;
static const uint8_t *a_g;
static uint8_t a_sh, a_col, a_two;

// fill a_rows pixel rows of one tile column: byte = (byte & ~a_m) | plane_value
static void asm_colfill(void) __naked {
    __asm
    ld  a, (_a_m)
    cpl
    ld  d, a
    ld  a, (_a_lom)
    ld  b, a
    ld  hl, #_a_p
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    call 3$
    ld  a, (_a_him)
    ld  b, a
    ld  hl, #_a_p
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    inc hl
    call 3$
    ret
3$:
    ld  a, (_a_r0)
    ld  e, a
    ld  a, (_a_rows)
    ld  c, a
1$:
    ld  a, (hl)
    and a, d
    or  a, b
    ld  (hl), a
    inc hl
    inc hl
    inc e
    ld  a, e
    and a, #7
    jr  nz, 2$
    ld  a, (_a_skip)
    add a, l
    ld  l, a
    ld  a, (_a_skip+1)
    adc a, h
    ld  h, a
2$:
    dec c
    jr  nz, 1$
    ret
    __endasm;
}

// blit a_rows bytes from a_g (bit7 = leftmost) at pixel offset a_sh;
// a_two = also write the spill-over into the next tile
static void asm_blit(void) __naked {
    __asm
    ld  hl, #_a_g
    ld  a, (hl+)
    ld  d, (hl)
    ld  e, a
    ld  hl, #_a_p
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  a, (_a_rows)
    ld  c, a
1$:
    ld  a, (de)
    inc de
    or  a, a
    jr  z, 5$
    push de
    ld  b, a
    ld  a, (_a_sh)
    or  a, a
    jr  z, 7$
    ld  e, a
    ld  a, b
6$:
    srl a
    dec e
    jr  nz, 6$
    ld  d, a
    jr  8$
7$:
    ld  d, b
8$:
    call 20$
    ld  a, (_a_two)
    or  a, a
    jr  z, 4$
    ld  a, (_a_sh)
    ld  e, a
    ld  a, #8
    sub a, e
    ld  e, a
    ld  a, b
9$:
    add a, a
    dec e
    jr  nz, 9$
    or  a, a
    jr  z, 4$
    ld  d, a
    push hl
    ld  a, l
    add a, #16
    ld  l, a
    jr  nc, 11$
    inc h
11$:
    call 20$
    pop hl
4$:
    pop de
5$:
    inc hl
    inc hl
    ld  a, (_a_r0)
    inc a
    ld  (_a_r0), a
    and a, #7
    jr  nz, 10$
    push de
    ld  a, (_a_skip)
    ld  e, a
    ld  a, (_a_skip+1)
    ld  d, a
    add hl, de
    pop de
10$:
    dec c
    jr  nz, 1$
    ret
20$:
    ld  a, (_a_col)
    rrca
    jr  nc, 21$
    ld  a, (hl)
    or  a, d
    jr  22$
21$:
    ld  a, d
    cpl
    and a, (hl)
22$:
    ld  (hl+), a
    ld  a, (_a_col)
    and a, #2
    jr  z, 23$
    ld  a, (hl)
    or  a, d
    jr  24$
23$:
    ld  a, d
    cpl
    and a, (hl)
24$:
    ld  (hl-), a
    ret
    __endasm;
}

// blit n rows (bit7 = leftmost) at x,y; rows fully inside the canvas vertically
static void fast_blit(int16_t x, int16_t y, const uint8_t *rows, uint8_t n, uint8_t col) {
    uint8_t tx, yy;
    if (x < 0 || y < 0 || x >= cpw || y + n > cph) {
        uint8_t r;
        for (r = 0; r < n; r++) blit_row(x, y + r, rows[r], col);
        return;
    }
    tx = (uint8_t)x >> 3;
    yy = (uint8_t)y;
    a_g = rows;
    a_rows = n;
    a_sh = (uint8_t)x & 7;
    a_two = (uint8_t)(tx + 1) < cw;
    a_col = col;
    a_r0 = yy & 7;
    a_skip = ((uint16_t)cw << 4) - 16;
    a_p = cb + rowbase[yy >> 3] + ((uint16_t)tx << 4) + ((yy & 7) << 1);
    asm_blit();
}

void cv_rect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t col) {
    uint8_t tx, tx0, tx1, m, lo, hi, ya;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= cpw) x1 = cpw - 1;
    if (y1 >= cph) y1 = cph - 1;
    if (x0 > x1 || y0 > y1) return;
    lo = (col & 1) ? 0xFF : 0;
    hi = (col & 2) ? 0xFF : 0;
    tx0 = (uint8_t)x0 >> 3;
    tx1 = (uint8_t)x1 >> 3;
    ya = (uint8_t)y0;
    a_rows = (uint8_t)(y1 - y0 + 1);
    a_skip = ((uint16_t)cw << 4) - 16;
    for (tx = tx0; tx <= tx1; tx++) {
        m = 0xFF;
        if (tx == tx0) m &= 0xFF >> ((uint8_t)x0 & 7);
        if (tx == tx1) m &= (uint8_t)(0xFF << (7 - ((uint8_t)x1 & 7)));
        a_m = m;
        a_lom = lo & m;
        a_him = hi & m;
        a_r0 = ya & 7;
        a_p = cb + rowbase[ya >> 3] + ((uint16_t)tx << 4) + ((ya & 7) << 1);
        asm_colfill();
    }
}

void cv_hline(int16_t x0, int16_t x1, int16_t y, uint8_t col) {
    cv_rect(x0, y, x1, y, col);
}

// 1px rounded outline (what rrectfill + inner black fill looks like)
void cv_frame(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t col) {
    cv_rect(x + 1, y, x + w - 2, y, col);
    cv_rect(x + 1, y + h - 1, x + w - 2, y + h - 1, col);
    cv_rect(x, y + 1, x, y + h - 2, col);
    cv_rect(x + w - 1, y + 1, x + w - 1, y + h - 2, col);
}

// PICO-8 style rounded rect fill (rrectfill)
void cv_rrect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t col) {
    if (w <= 2) {
        cv_rect(x, y, x + w - 1, y + h - 1, col);
        return;
    }
    cv_rect(x + 1, y, x + w - 2, y + h - 1, col);
    cv_rect(x, y + 1, x + w - 1, y + h - 2, col);
}

static uint8_t glyph_w(uint8_t ch) {
    return font_w[ch & 127];
}

uint8_t text_w(const char *s) {
    uint8_t w = 0;
    while (*s) {
        if (*s != '\n') w += glyph_w(*s);
        s++;
    }
    return w;
}

int16_t cv_putc(char ch, int16_t x, int16_t y, uint8_t col) {
    fast_blit(x, y, font_rows + (uint8_t)(ch & 127) * 5, 5, col);
    return x + glyph_w(ch);
}

int16_t cv_print(const char *s, int16_t x, int16_t y, uint8_t col) {
    int16_t x0 = x;
    uint8_t *base = 0;
    uint8_t fast = 0, tx;
    char ch;
    while ((ch = *s++)) {
        if (ch == '\n') {
            x = x0;
            y += 6;
            fast = 0;
            continue;
        }
        if (!fast) {
            fast = 2;
            if (y >= 0 && y + 5 <= cph) {
                fast = 1;
                base = cb + rowbase[(uint8_t)y >> 3] + (((uint8_t)y & 7) << 1);
                a_col = col;
                a_skip = ((uint16_t)cw << 4) - 16;
            }
        }
        if (fast == 1 && x >= 0 && x < cpw) {
            tx = (uint8_t)x >> 3;
            a_g = font_rows + (uint8_t)(ch & 127) * 5;
            a_rows = 5;
            a_sh = (uint8_t)x & 7;
            a_two = (uint8_t)(tx + 1) < cw && a_sh + glyph_w(ch) > 9;
            a_r0 = (uint8_t)y & 7;
            a_p = base + ((uint16_t)tx << 4);
            asm_blit();
        } else {
            cv_putc(ch, x, y, col);
        }
        x += glyph_w(ch);
    }
    return x;
}

void cv_cprint(const char *s, int16_t cx, int16_t y, uint8_t col) {
    cv_print(s, cx - text_w(s) / 2, y, col);
}

void cv_rprint(const char *s, int16_t rx, int16_t y, uint8_t col) {
    cv_print(s, rx - text_w(s), y, col);
}

static int8_t big_idx(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return big_w[c - 'a' + 10] ? c - 'a' + 10 : -1;
    return -1;
}

uint8_t bignum_w(const char *s) {
    uint8_t w = 0;
    int8_t i;
    for (; *s; s++) {
        i = big_idx(*s);
        if (i >= 0) w += big_w[i] + (int8_t)big_kern[i] * 2 + 2;
        else w += 6;
    }
    return w - 2;
}

static uint8_t rev8(uint8_t v) {
    v = (v >> 4) | (v << 4);
    v = ((v & 0xCC) >> 2) | ((v & 0x33) << 2);
    return ((v & 0xAA) >> 1) | ((v & 0x55) << 1);
}

void cv_bignum(const char *s, int16_t x, int16_t y, uint8_t col) {
    int8_t i, k;
    uint8_t r, w;
    uint16_t bits;
    const uint16_t *g;
    for (; *s; s++) {
        i = big_idx(*s);
        if (i >= 0) {
            g = big_rows + i * 10;
            w = big_w[i];
            k = (int8_t)big_kern[i];
            {
                static uint8_t lrow[10], rrow[10];
                for (r = 0; r < 10; r++) {
                    bits = g[r];
                    lrow[r] = rev8((uint8_t)bits);
                    rrow[r] = rev8((uint8_t)(bits >> 8));
                }
                fast_blit(x + k, y, lrow, 10, col);
                if (w > 8) fast_blit(x + k + 8, y, rrow, 10, col);
            }
            x += w + k * 2 + 2;
        } else if (*s == '-') {
            cv_rect(x, y + 4, x + 3, y + 5, col);
            x += 6;
        } else {
            x += 6;
        }
    }
}

void cv_bignumc(const char *s, int16_t cx, int16_t y, uint8_t col, uint8_t ml) {
    uint8_t w = bignum_w(s);
    cv_bignum(s, cx - w / 2, y, col);
    if (ml) cv_print("ML", cx + w / 2 + 2, y + 5, col);
}

uint16_t cv_tile_at(int16_t x, int16_t y) {
    return (uint16_t)((uint8_t)y >> 3) * cw + ((uint8_t)x >> 3);
}

void cv_flush(const canvas_t *c) {
    VBK_REG = c->bank;
    set_bkg_data(c->tile, c->w * c->h, c->buf);
    VBK_REG = 0;
}

void cv_flush_tile(const canvas_t *c, uint16_t i) {
    VBK_REG = c->bank;
    set_bkg_data(c->tile + i, 1, c->buf + i * 16);
    VBK_REG = 0;
}

static uint8_t rowbuf[32];

void cv_place(const canvas_t *c) {
    uint8_t r, i, t = c->tile;
    for (r = 0; r < c->h; r++) {
        for (i = 0; i < c->w; i++) rowbuf[i] = t++;
        if (c->win) set_win_tiles(c->x, c->y + r, c->w, 1, rowbuf);
        else set_bkg_tiles(c->x, c->y + r, c->w, 1, rowbuf);
    }
    map_fill(c->win | 0x80, c->x, c->y, c->w, c->h, 0, c->pal | (c->bank ? 8 : 0));
}

void cv_draw(const canvas_t *c) {
    cv_flush(c);
    cv_place(c);
}

// win bit7 set = attributes only
void map_fill(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tile, uint8_t attr) {
    uint8_t r, i;
    for (i = 0; i < w; i++) rowbuf[i] = attr;
    VBK_REG = 1;
    for (r = 0; r < h; r++) {
        if (win & 1) set_win_tiles(x, y + r, w, 1, rowbuf);
        else set_bkg_tiles(x, y + r, w, 1, rowbuf);
    }
    VBK_REG = 0;
    if (win & 0x80) return;
    for (i = 0; i < w; i++) rowbuf[i] = tile;
    for (r = 0; r < h; r++) {
        if (win & 1) set_win_tiles(x, y + r, w, 1, rowbuf);
        else set_bkg_tiles(x, y + r, w, 1, rowbuf);
    }
}

void map_put(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *tiles, const uint8_t *attrs) {
    VBK_REG = 1;
    if (win) set_win_tiles(x, y, w, h, attrs);
    else set_bkg_tiles(x, y, w, h, attrs);
    VBK_REG = 0;
    if (win) set_win_tiles(x, y, w, h, tiles);
    else set_bkg_tiles(x, y, w, h, tiles);
}

void load_bkg_banked(uint8_t rombank, const uint8_t *src, uint8_t first, uint8_t n, uint8_t vbank) {
    uint8_t save = _current_bank;
    SWITCH_ROM(rombank);
    VBK_REG = vbank;
    set_bkg_data(first, n, src);
    VBK_REG = 0;
    SWITCH_ROM(save);
}

void load_spr_banked(uint8_t rombank, const uint8_t *src, uint8_t first, uint8_t n, uint8_t vbank) {
    uint8_t save = _current_bank;
    SWITCH_ROM(rombank);
    VBK_REG = vbank;
    set_sprite_data(first, n, src);
    VBK_REG = 0;
    SWITCH_ROM(save);
}

void make_solid_tiles(void) {
    uint8_t c, i;
    for (c = 0; c < 4; c++) {
        for (i = 0; i < 16; i += 2) {
            rowbuf[i] = (c & 1) ? 0xFF : 0;
            rowbuf[i + 1] = (c & 2) ? 0xFF : 0;
        }
        set_bkg_data(TILE_SOLID(c), 1, rowbuf);
    }
}

// ---------------------------------------------------------------- palettes
uint16_t bgpal[32], objpal[32];
uint8_t fadelvl = 25;
uint8_t pal_dirty = 1;
static uint8_t fading_out;

void set_bgpal(uint8_t n, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3) {
    uint16_t *p = bgpal + n * 4;
    p[0] = pico[c0];
    p[1] = pico[c1];
    p[2] = pico[c2];
    p[3] = pico[c3];
    pal_dirty = 1;
}

void set_objpal(uint8_t n, uint8_t c1, uint8_t c2, uint8_t c3) {
    uint16_t *p = objpal + n * 4;
    p[0] = 0;
    p[1] = pico[c1];
    p[2] = pico[c2];
    p[3] = pico[c3];
    pal_dirty = 1;
}

void copy_bgpals(uint8_t first, uint8_t n, const uint16_t *src) {
    memcpy(bgpal + first * 4, src, n * 8);
    pal_dirty = 1;
}

void copy_objpals(uint8_t first, uint8_t n, const uint16_t *src) {
    memcpy(objpal + first * 4, src, n * 8);
    pal_dirty = 1;
}

static uint16_t tmppal[32];
static uint8_t scale[32];

static void fade_conv(const uint16_t *src) {
    uint8_t i;
    uint16_t c;
    for (i = 0; i < 32; i++) {
        c = src[i];
        tmppal[i] = scale[c & 31] | ((uint16_t)scale[(c >> 5) & 31] << 5) | ((uint16_t)scale[(c >> 10) & 31] << 10);
    }
}

static void pal_apply(void) {
    uint8_t i;
    if (fadelvl == 0) {
        set_bkg_palette(0, 8, bgpal);
        set_sprite_palette(0, 8, objpal);
    } else {
        uint8_t k = 25 - fadelvl;
        // quadratic falloff looks closer to PICO-8's fade table
        for (i = 0; i < 32; i++) scale[i] = (uint8_t)(((uint16_t)i * k * k) / 625);
        fade_conv(bgpal);
        set_bkg_palette(0, 8, tmppal);
        fade_conv(objpal);
        set_sprite_palette(0, 8, tmppal);
    }
}

// ---------------------------------------------------------------- sprites
int8_t cam_x, cam_y;
uint8_t win_cut;
static uint8_t nspr;

void spr_put(int16_t x, int16_t y, uint8_t tile, uint8_t prop) {
    x += cam_x;
    y += cam_y;
    if (nspr >= 40 || x <= -8 || x >= 168 || y <= -16 || y >= 160) return;
    if (win_cut && y + 16 > win_cut && y < 112) return;
    set_sprite_tile(nspr, tile);
    set_sprite_prop(nspr, prop);
    move_sprite(nspr, (uint8_t)(x + 8), (uint8_t)(y + 16));
    nspr++;
}

void spr_put16(int16_t x, int16_t y, uint8_t tile, uint8_t prop) {
    if (prop & 0x20) {
        spr_put(x, y, tile + 2, prop);
        spr_put(x + 8, y, tile, prop);
    } else {
        spr_put(x, y, tile, prop);
        spr_put(x + 8, y, tile + 2, prop);
    }
}

void spr_end(void) {
    uint8_t i;
    for (i = nspr; i < 40; i++) move_sprite(i, 0, 0);
    nspr = 0;
}

// ---------------------------------------------------------------- frame loop
uint8_t frames;
uint8_t joy, joyp;
uint8_t scx, scy;
extern volatile uint8_t joy_latch;

void frame(void) {
    spr_end();
    vsync();
    if (!fading_out && fadelvl) {
        fadelvl--;
        pal_dirty = 1;
    }
    if (pal_dirty) {
        pal_apply();
        pal_dirty = 0;
    }
    SCX_REG = scx - cam_x;
    SCY_REG = scy - cam_y;
    {
        uint8_t save = _current_bank;
        SWITCH_ROM(BANK(sound_bank));
        snd_update();
        SWITCH_ROM(save);
    }
    frames++;
    CRITICAL {
        joyp = joy_latch;
        joy_latch = 0;
    }
    joy = joypad();
}

void fadeout(uint8_t spd) {
    fading_out = 1;
    while (fadelvl < 25) {
        fadelvl += spd;
        if (fadelvl > 25) fadelvl = 25;
        pal_dirty = 1;
        frame();
    }
    fading_out = 0;
}

void wait_frames(uint8_t n) {
    while (n--) frame();
}

void screen_off(void) {
    // keep the LCD running (turning it off flashes white on GBC);
    // instead black out all palettes while the screen is rebuilt
    fadelvl = 25;
    fading_out = 1;
    spr_end();
    vsync();
    pal_apply();
    pal_dirty = 0;
}

void screen_on(void) {
    fading_out = 0;
}

void clear_bg(void) {
    VBK_REG = 1;
    fill_bkg_rect(0, 0, 32, 18, 0);
    fill_win_rect(0, 0, 20, 18, 0);
    VBK_REG = 0;
    fill_bkg_rect(0, 0, 32, 18, 0);
    fill_win_rect(0, 0, 20, 18, 0);
}

uint8_t rnd(uint8_t n) {
    return n ? (uint8_t)(rand() % n) : 0;
}

// ---------------------------------------------------------------- strings (bank 0: callers pass literals from any bank)
char sbuf[48];
static char *sp;
static char nbuf[3][12];
static uint8_t nbi;

void s_begin(void) { sp = sbuf; }
void s_str(const char *s) { while (*s) *sp++ = *s++; }
void s_num(int16_t v) {
    char tmp[7];
    uint8_t n = 0;
    uint16_t u;
    if (v < 0) { *sp++ = '-'; u = (uint16_t)(-v); } else u = (uint16_t)v;
    do { tmp[n++] = '0' + (u % 10); u /= 10; } while (u);
    while (n) *sp++ = tmp[--n];
}
char *s_end(void) { *sp = 0; return sbuf; }

const char *numstr(int16_t v) {
    char *save = sp, *out;
    char tmp[7];
    uint8_t n = 0;
    uint16_t u;
    out = nbuf[nbi];
    nbi = (nbi + 1) % 3;
    sp = out;
    if (v < 0) { *sp++ = '-'; u = (uint16_t)(-v); } else u = (uint16_t)v;
    do { tmp[n++] = '0' + (u % 10); u /= 10; } while (u);
    while (n) *sp++ = tmp[--n];
    *sp = 0;
    sp = save;
    return out;
}

const char *mlstr(int16_t v, uint8_t sign) {
    char *save = sp, *out;
    out = nbuf[nbi];
    nbi = (nbi + 1) % 3;
    sp = out;
    if (sign && v > 0) *sp++ = '+';
    {
        const char *n = numstr(v);
        while (*n) *sp++ = *n++;
    }
    *sp++ = 'M';
    *sp++ = 'L';
    *sp = 0;
    sp = save;
    return out;
}

