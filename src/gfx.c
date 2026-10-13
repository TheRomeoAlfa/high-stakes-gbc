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
uint8_t dlgbuf[80 * 16];

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

// RAM fill (GBDK's memset costs ~80 cycles/byte); fills fl_n bytes at fl_dst with fl_v
static uint8_t *fl_dst;
static uint16_t fl_n;
static uint8_t fl_v;

static void asm_fill(void) __naked {
    __asm
    ld  hl, #_fl_n
    ld  a, (hl+)
    ld  c, a
    ld  b, (hl)
    ld  hl, #_fl_dst
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  a, (_fl_v)
    inc b
    inc c
    jr  2$
1$:
    ld  (hl+), a
2$:
    dec c
    jr  nz, 1$
    dec b
    jr  nz, 1$
    ret
    __endasm;
}

static void fast_fill(uint8_t *dst, uint8_t v, uint16_t n) {
    fl_dst = dst;
    fl_v = v;
    fl_n = n;
    asm_fill();
}

void cv_begin(const canvas_t *c, uint8_t col) {
    uint16_t n;
    cv_select(c);
    n = (uint16_t)cw * chh * 16;
    if (col == 0) fast_fill(cb, 0, n);
    else if (col == 3) fast_fill(cb, 0xFF, n);
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

static const uint8_t lmask[8] = {0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01};
static const uint8_t rmask[8] = {0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF};

void cv_rect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t col) {
    uint8_t tx, tx0, tx1, m, lo, hi, ya, r0;
    uint8_t *p;
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
    r0 = ya & 7;
    a_rows = (uint8_t)(y1 - y0 + 1);
    a_skip = ((uint16_t)cw << 4) - 16;
    p = cb + rowbase[ya >> 3] + ((uint16_t)tx0 << 4) + (r0 << 1);
    for (tx = tx0; tx <= tx1; tx++, p += 16) {
        m = 0xFF;
        if (tx == tx0) m = lmask[(uint8_t)x0 & 7];
        if (tx == tx1) m &= rmask[(uint8_t)x1 & 7];
        a_m = m;
        a_lom = lo & m;
        a_him = hi & m;
        a_r0 = r0;
        a_p = p;
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

#define glyph_w(ch) (font_w[(uint8_t)(ch) & 127])

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
    uint8_t fast = 0, tx, xx, w, c, r0 = 0, cpw8 = (uint8_t)cpw;
    char ch;
    while ((ch = *s++)) {
        if (ch == '\n') {
            x = x0;
            y += 6;
            fast = 0;
            continue;
        }
        c = (uint8_t)ch & 127;
        w = font_w[c];
        if (!fast) {
            fast = 2;
            if (y >= 0 && y + 5 <= cph) {
                fast = 1;
                r0 = (uint8_t)y & 7;
                base = cb + rowbase[(uint8_t)y >> 3] + (r0 << 1);
                a_col = col;
                a_skip = ((uint16_t)cw << 4) - 16;
            }
        }
        // 8-bit fast path while the glyph starts inside the canvas
        if (fast == 1 && !((uint16_t)x >> 8) && (xx = (uint8_t)x) < cpw8) {
            tx = xx >> 3;
            a_g = font_rows + ((uint16_t)c << 2) + c;
            a_rows = 5;
            a_sh = xx & 7;
            a_two = (uint8_t)(tx + 1) < cw && (uint8_t)(a_sh + w) > 9;
            a_r0 = r0;
            a_p = base + ((uint16_t)tx << 4);
            asm_blit();
        } else {
            cv_putc(ch, x, y, col);
        }
        x += w;
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

void cv_bignum(const char *s, int16_t x, int16_t y, uint8_t col) {
    int8_t i, k;
    uint8_t w;
    for (; *s; s++) {
        i = big_idx(*s);
        if (i >= 0) {
            w = big_w[i];
            k = (int8_t)big_kern[i];
            fast_blit(x + k, y, big_l + i * 10, 10, col);
            if (w > 8) fast_blit(x + k + 8, y, big_r + i * 10, 10, col);
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


// ---- fast VRAM access. VRAM is only blocked in mode 3; once STAT shows mode 0/1
// the following mode 2 (80 dots) guarantees time for 4 more writes, so we test
// STAT once per 4 bytes (interrupts masked so an ISR can't delay the writes).
static uint8_t *vc_dst;
static const uint8_t *vc_src;
static uint16_t vc_n;   // number of 4-byte groups
static uint8_t vc_val;

static void asm_vram_copy(void) __naked {
    __asm
    ld  hl, #_vc_src
    ld  a, (hl+)
    ld  e, a
    ld  d, (hl)
    ld  hl, #_vc_n
    ld  a, (hl+)
    ld  c, a
    ld  b, (hl)
    ld  hl, #_vc_dst
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    inc b
    inc c
    jr  3$
1$:
    di
2$:
    ldh a, (_STAT_REG + 0)
    and a, #2
    jr  nz, 2$
    ld  a, (de)
    ld  (hl+), a
    inc de
    ld  a, (de)
    ld  (hl+), a
    inc de
    ld  a, (de)
    ld  (hl+), a
    inc de
    ld  a, (de)
    ld  (hl+), a
    inc de
    ei
3$:
    dec c
    jr  nz, 1$
    dec b
    jr  nz, 1$
    ret
    __endasm;
}

static void asm_vram_fill(void) __naked {
    __asm
    ld  a, (_vc_val)
    ld  e, a
    ld  hl, #_vc_n
    ld  a, (hl+)
    ld  c, a
    ld  b, (hl)
    ld  hl, #_vc_dst
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    inc b
    inc c
    jr  3$
1$:
    di
2$:
    ldh a, (_STAT_REG + 0)
    and a, #2
    jr  nz, 2$
    ld  a, e
    ld  (hl+), a
    ld  (hl+), a
    ld  (hl+), a
    ld  (hl+), a
    ei
3$:
    dec c
    jr  nz, 1$
    dec b
    jr  nz, 1$
    ret
    __endasm;
}

// BG/window tile index -> VRAM address (LCDC.4 = 0: tiles 0-127 at 0x9000, 128-255 at 0x8800)
static uint8_t *bg_tile_addr(uint8_t t) {
    return (uint8_t *)((t < 128 ? 0x9000u : 0x8000u) + ((uint16_t)t << 4));
}

void vram_copy(uint8_t *dst, const uint8_t *src, uint16_t len) {
    uint8_t r = len & 3;
    vc_dst = dst;
    vc_src = src;
    vc_n = len >> 2;
    if (vc_n) asm_vram_copy();
    dst += len - r;
    src += len - r;
    while (r--) {
        CRITICAL {
            while (STAT_REG & 2);   // wait for mode 0/1
            *dst++ = *src++;
        }
    }
}

void vram_fill(uint8_t *dst, uint8_t v, uint16_t len) {
    uint8_t r = len & 3;
    vc_dst = dst;
    vc_val = v;
    vc_n = len >> 2;
    if (vc_n) asm_vram_fill();
    dst += len - r;
    while (r--) {
        CRITICAL {
            while (STAT_REG & 2);   // wait for mode 0/1
            *dst++ = v;
        }
    }
}

// tile map row helpers (current VBK bank): BG map 0x9800, window map 0x9C00
static uint8_t *map_addr(uint8_t win, uint8_t x, uint8_t y) {
    return (uint8_t *)((win ? 0x9C00u : 0x9800u) + ((uint16_t)y << 5) + x);
}

// upload n BG tiles; a run that crosses tile 127 -> 128 wraps from 0x97F0 to 0x8800
void bkg_tiles_upload(uint8_t first, uint16_t n, const uint8_t *src) {
    while (n) {
        uint16_t run = first < 128 ? 128 - first : 256 - first;
        if (run > n) run = n;
        vram_copy(bg_tile_addr(first), src, run << 4);
        src += run << 4;
        first += (uint8_t)run;
        n -= run;
    }
}

void cv_flush(const canvas_t *c) {
    VBK_REG = c->bank;
    bkg_tiles_upload(c->tile, (uint16_t)c->w * c->h, c->buf);
    VBK_REG = 0;
}

void cv_flush_tile(const canvas_t *c, uint16_t i) {
    VBK_REG = c->bank;
    vram_copy(bg_tile_addr(c->tile + i), c->buf + i * 16, 16);
    VBK_REG = 0;
}

static uint8_t rowbuf[32];

// map/attribute placement is cached per first tile, so redraws only upload pixels
// (a few canvases are live at a time: 16 entries, oldest replaced)
#define PLACED_N 16
static uint8_t placed_t[PLACED_N], placed_n, placed_next;
static uint16_t placed_k[PLACED_N];

static void place_attrs(const canvas_t *c);

void cv_place(const canvas_t *c) {
    uint8_t r, i, t = c->tile;
    uint16_t key = 0x8000u | c->x | ((uint16_t)c->y << 5) | ((uint16_t)c->win << 10) | ((uint16_t)c->pal << 11);
    for (r = 0; r < placed_n; r++)
        if (placed_t[r] == t) {
            if (placed_k[r] == key) return;
            break;
        }
    if (r == placed_n) {
        if (placed_n < PLACED_N) placed_n++;
        else {
            r = placed_next;
            if (++placed_next == PLACED_N) placed_next = 0;
        }
    }
    placed_t[r] = t;
    placed_k[r] = key;
    for (r = 0; r < c->h; r++) {
        for (i = 0; i < c->w; i++) rowbuf[i] = t++;
        vram_copy(map_addr(c->win, c->x, c->y + r), rowbuf, c->w);
    }
    place_attrs(c);
}

// upload n tile rows of a canvas starting at row r0
void cv_flush_rows(const canvas_t *c, uint8_t r0, uint8_t n) {
    uint16_t first = (uint16_t)r0 * c->w;
    VBK_REG = c->bank;
    bkg_tiles_upload(c->tile + (uint8_t)first, (uint16_t)c->w * n, c->buf + (first << 4));
    VBK_REG = 0;
}

// write only the canvas tile indices into the map (no cache, attributes untouched);
// used to swap between pre-rendered variants of the same box
void cv_place_tiles(const canvas_t *c) {
    uint8_t r, i, t = c->tile;
    for (r = 0; r < c->h; r++) {
        for (i = 0; i < c->w; i++) rowbuf[i] = t++;
        vram_copy(map_addr(c->win, c->x, c->y + r), rowbuf, c->w);
    }
}

void cv_place_attrs(const canvas_t *c) {
    place_attrs(c);
}

// fill tile indices only (attributes untouched, placement cache untouched)
void map_tiles(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tile) {
    uint8_t r;
    for (r = 0; r < h; r++) vram_fill(map_addr(win, x, y + r), tile, w);
}

void cv_invalidate(void) {
    placed_n = placed_next = 0;
}

void cv_draw(const canvas_t *c) {
    cv_flush(c);
    cv_place(c);
}

static void attr_rows(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t attr) {
    uint8_t r;
    VBK_REG = 1;
    for (r = 0; r < h; r++) vram_fill(map_addr(win, x, y + r), attr, w);
    VBK_REG = 0;
}

static void place_attrs(const canvas_t *c) {
    attr_rows(c->win, c->x, c->y, c->w, c->h, c->pal | (c->bank ? 8 : 0));
}

// win bit7 set = attributes only
void map_fill(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tile, uint8_t attr) {
    uint8_t r, wn = win & 1;
    if (!(win & 0x80)) cv_invalidate();   // may have covered placed canvases
    VBK_REG = 1;
    for (r = 0; r < h; r++) vram_fill(map_addr(wn, x, y + r), attr, w);
    VBK_REG = 0;
    if (win & 0x80) return;
    for (r = 0; r < h; r++) vram_fill(map_addr(wn, x, y + r), tile, w);
}

void map_put(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *tiles, const uint8_t *attrs) {
    uint8_t r;
    VBK_REG = 1;
    for (r = 0; r < h; r++, attrs += w) vram_copy(map_addr(win, x, y + r), attrs, w);
    VBK_REG = 0;
    for (r = 0; r < h; r++, tiles += w) vram_copy(map_addr(win, x, y + r), tiles, w);
}

// map blocks queued for the next VBlank: tiles and attributes then change together
// (written mid-frame, a block can show new attributes over old tiles for a frame)
#define VQ_MAX 12
typedef struct { uint8_t x, y, w, h; const uint8_t *tiles, *attrs; uint8_t *dst; } vq_t;   // dst: map address
static vq_t vq[VQ_MAX];
static uint8_t vq_n;

static void vq_run(const vq_t *j) {
    if (j->tiles) map_put(0, j->x, j->y, j->w, j->h, j->tiles, j->attrs);
    else map_fill(0, j->x, j->y, j->w, j->h, 0, 0);
}

uint8_t map_vbl_queued(void) {
    return vq_n;
}

uint8_t map_vbl_pending(uint8_t x, uint8_t y) {
    uint8_t i;
    for (i = 0; i < vq_n; i++)
        if (vq[i].x == x && vq[i].y == y) return 1;
    return 0;
}

void map_put_vbl(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *tiles, const uint8_t *attrs) {
    uint8_t i;
    vq_t *j = vq;
    for (i = 0; i < vq_n; i++, j++)
        if (j->x == x && j->y == y && j->w == w && j->h == h) break;   // replaces a pending draw of the same block
    if (i == VQ_MAX) {                       // full: draw now
        vq_t t = {x, y, w, h, tiles, attrs, 0};
        vq_run(&t);
        return;
    }
    if (i == vq_n) vq_n++;
    j->x = x; j->y = y; j->w = w; j->h = h; j->tiles = tiles; j->attrs = attrs;
    j->dst = map_addr(0, x, y);
}

// CGB general-purpose DMA jobs for the next VBlank: 16-byte blocks, 16-byte aligned source
// (ROM in rombank, or RAM) and destination, the CPU halted meanwhile (64 cycles a block in
// double speed, 14 a line). A transfer still running once the screen is drawn writes stray
// bytes into both VRAM banks (the PPU's fetches collide with it), so each VBlank sends the
// jobs that end in time and the rest wait (every job fits a whole VBlank); map blocks wait
// with them (frame), so tiles and map stay in step.
#define VD_MAX 12
typedef struct { const uint8_t *src; uint8_t *dst; uint8_t vbank, blocks, rombank; } vd_t;
static vd_t vd[VD_MAX];
static uint8_t vd_n;

void vram_dma_vbl(const void *src, void *dst, uint8_t vbank, uint8_t blocks, uint8_t rombank) {
    vd_t *j;
    if (vd_n == VD_MAX || !blocks) return;
    j = &vd[vd_n++];
    j->src = src;
    j->dst = dst;
    j->vbank = vbank;
    j->blocks = blocks;
    j->rombank = rombank;
}

uint8_t vram_dma_busy(void) {
    return vd_n;
}

// vd_t is 7 bytes: src, dst, vbank, blocks, rombank. Jobs go while LY is 144-151 and the
// job's blocks fit (153 - LY) * 12; the rest move to the front of the queue.
static void vd_flush(void) __naked {
    __asm
    ld  a, (__current_bank)
    push af
    ld  hl, #_vd
    ld  a, (_vd_n)
    ld  c, a
1$:
    ld  a, c
    or  a, a
    jr  z, 9$
    ldh a, (_LY_REG + 0)
    cp  a, #144
    jr  c, 8$
    cp  a, #152
    jr  nc, 8$
    ld  b, a
    ld  a, #153
    sub a, b
    add a, a
    add a, a
    ld  b, a
    add a, a
    add a, b
    ld  b, a                ; blocks that end in time
    push hl
    ld  de, #5
    add hl, de
    ld  a, (hl)             ; job blocks
    pop hl
    cp  a, b
    jr  z, 2$
    jr  nc, 8$
2$:
    dec a
    ld  b, a
    ld  a, (hl+)
    ld  e, a
    ld  a, (hl+)
    ldh (_HDMA1_REG + 0), a
    ld  a, e
    ldh (_HDMA2_REG + 0), a
    ld  a, (hl+)
    ld  e, a
    ld  a, (hl+)
    ldh (_HDMA3_REG + 0), a
    ld  a, e
    ldh (_HDMA4_REG + 0), a
    ld  a, (hl+)
    ldh (_VBK_REG + 0), a
    inc hl
    ld  a, (hl+)
    ld  (__current_bank), a
    ld  (_rROMB0), a
    ld  a, b
    ldh (_HDMA5_REG + 0), a ; bit 7 clear: general purpose, runs now
    dec c
    jr  1$
8$:
    ld  a, c                ; c jobs left from hl: move them to the front
    ld  (_vd_n), a
    ld  b, a
    add a, a
    add a, a
    add a, a
    sub a, b
    ld  b, a
    ld  de, #_vd
3$:
    ld  a, (hl+)
    ld  (de), a
    inc de
    dec b
    jr  nz, 3$
    jr  10$
9$:
    ld  (_vd_n), a
10$:
    xor a, a
    ldh (_VBK_REG + 0), a
    pop af
    ld  (__current_bank), a
    ld  (_rROMB0), a
    ret
    __endasm;
}

// copies a w x h block to the map at vb_dst (vb_src NULL: zeros). Rows written in VBlank
// go at full speed; once VBlank is over, each byte waits for mode 0/1 (writes during
// mode 3 are lost), like vram_copy.
static uint8_t *vb_dst;
static const uint8_t *vb_src;
static uint8_t vb_w, vb_h;

static void vb_blit(void) __naked {
    __asm
    ld  hl, #_vb_dst
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  a, (_vb_src)
    ld  e, a
    ld  a, (_vb_src + 1)
    ld  d, a
    ld  a, (_vb_h)
    ld  b, a
1$:
    push hl
    ld  a, (_vb_w)
    ld  c, a
    ld  a, d
    or  a, e
    jr  z, 6$
    ldh a, (_LY_REG + 0)
    cp  a, #144
    jr  c, 4$
2$:                      ; copy, in VBlank
    ld  a, (de)
    inc de
    ld  (hl+), a
    dec c
    jr  nz, 2$
    jr  9$
4$:                      ; copy, waiting on STAT
    di
5$:
    ldh a, (_STAT_REG + 0)
    and a, #2
    jr  nz, 5$
    ld  a, (de)
    ld  (hl+), a
    ei
    inc de
    dec c
    jr  nz, 4$
    jr  9$
6$:                      ; zeros
    ldh a, (_LY_REG + 0)
    cp  a, #144
    jr  c, 8$
    xor a, a
7$:
    ld  (hl+), a
    dec c
    jr  nz, 7$
    jr  9$
8$:
    di
10$:
    ldh a, (_STAT_REG + 0)
    and a, #2
    jr  nz, 10$          ; leaves a = 0
    ld  (hl+), a
    ei
    dec c
    jr  nz, 8$
9$:
    pop hl
    ld  a, l
    add a, #32
    ld  l, a
    jr  nc, 3$
    inc h
3$:
    dec b
    jr  nz, 1$
    ret
    __endasm;
}

// Blocks that don't fit in what is left of this VBlank wait for the next one: drawn
// mid-frame, a block could show new attributes over old tiles. A block costs about 1
// line per 10 cells (both planes) plus 2 for overhead; VBlank is lines 144-153. (If the
// estimate is short, vb_blit still writes safely, just slower.)
static void vq_flush(void) {
    uint8_t i, ly;
    vq_t *j = vq;
    for (i = 0; i < vq_n; i++, j++) {
        ly = LY_REG;
        if (ly < 144 || ly + (uint8_t)(j->w * j->h) / 10 + 2 > 153) break;
        vb_dst = j->dst;
        vb_w = j->w;
        vb_h = j->h;
        vb_src = j->attrs;   // NULL: clear to tile 0, attribute 0
        VBK_REG = 1;
        vb_blit();
        vb_src = j->tiles;
        VBK_REG = 0;
        vb_blit();
        if (!j->tiles) cv_invalidate();
    }
    if (i < vq_n) {   // keep the rest, in order
        uint8_t k;
        for (k = 0; i < vq_n; i++, k++) vq[k] = vq[i];
        vq_n = k;
    } else vq_n = 0;
}

// Card hop overlap row (see lift.c): for each of 3 tiles, out = base tile, with the card's
// pixels from the ROM record (overlap counts at +15, masks at +18, tiles at +48) where its
// mask is set. Runs here in bank 0 so it can map the record's bank.
uint8_t lb_bank;
const uint8_t *lb_rec;
const uint8_t *lb_src[3];
uint8_t *lb_out;
static uint8_t lb_j;

static void lb_blend(void) __naked {
    __asm
    xor a, a
    ld  (_lb_j), a
1$:
    ; de = lb_src[j], hl = lb_out + j * 16
    ld  a, (_lb_j)
    add a, a
    ld  e, a
    ld  d, #0
    ld  hl, #_lb_src
    add hl, de
    ld  a, (hl+)
    ld  e, a
    ld  d, (hl)
    ld  a, (_lb_j)
    swap a
    ld  c, a
    ld  b, #0
    ld  hl, #_lb_out
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    add hl, bc
    push hl
    ld  c, #16
2$:
    ld  a, (de)
    ld  (hl+), a
    inc de
    dec c
    jr  nz, 2$
    pop hl
    ; card pixels? count at rec + 15 + j
    ld  a, (_lb_j)
    add a, #15
    ld  c, a
    ld  b, #0
    push hl
    ld  hl, #_lb_rec
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    add hl, bc
    ld  a, (hl)
    pop hl
    or  a, a
    jr  z, 4$
    ; de = card tile rec + 48 + j * 16, bc = mask rec + 18 + j * 8
    push hl
    ld  a, (_lb_j)
    swap a
    add a, #48
    ld  e, a
    ld  a, (_lb_j)
    add a, a
    add a, a
    add a, a
    add a, #18
    ld  c, a
    ld  hl, #_lb_rec
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    push hl
    ld  d, #0
    add hl, de
    ld  d, h
    ld  e, l
    pop hl
    ld  b, #0
    add hl, bc
    ld  b, h
    ld  c, l
    pop hl
    ld  a, #8
3$:
    push af
    ld  a, (bc)
    inc bc
    push bc
    ld  b, a
    ld  a, (de)
    xor a, (hl)
    and a, b
    xor a, (hl)
    ld  (hl+), a
    inc de
    ld  a, (de)
    xor a, (hl)
    and a, b
    xor a, (hl)
    ld  (hl+), a
    inc de
    pop bc
    pop af
    dec a
    jr  nz, 3$
4$:
    ld  hl, #_lb_j
    inc (hl)
    ld  a, (hl)
    cp  a, #3
    jp  nz, 1$
    ret
    __endasm;
}

void lift_blend(void) {
    uint8_t save = _current_bank;
    SWITCH_ROM(lb_bank);
    lb_blend();
    SWITCH_ROM(save);
}

void banked_copy(void *dst, uint8_t rombank, const void *src, uint16_t n) {
    uint8_t save = _current_bank;
    SWITCH_ROM(rombank);
    memcpy(dst, src, n);
    SWITCH_ROM(save);
}

void load_bkg_banked(uint8_t rombank, const uint8_t *src, uint8_t first, uint8_t n, uint8_t vbank) {
    uint8_t save = _current_bank;
    SWITCH_ROM(rombank);
    VBK_REG = vbank;
    bkg_tiles_upload(first, n, src);
    VBK_REG = 0;
    SWITCH_ROM(save);
}

void load_spr_banked(uint8_t rombank, const uint8_t *src, uint8_t first, uint8_t n, uint8_t vbank) {
    uint8_t save = _current_bank;
    SWITCH_ROM(rombank);
    VBK_REG = vbank;
    vram_copy((uint8_t *)0x8000u + ((uint16_t)first << 4), src, (uint16_t)n << 4);
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

static uint16_t tmpobj[32];

// the faded palettes, worked out before VBlank (pal_apply then only writes them)
static void pal_prepare(void) {
    fade_hue(bgpal, tmppal, fadelvl);
    fade_hue(objpal, tmpobj, fadelvl);
}

static void pal_apply(void) {
    if (fadelvl == 0) {
        set_bkg_palette(0, 8, bgpal);
        set_sprite_palette(0, 8, objpal);
    } else {
        set_bkg_palette(0, 8, tmppal);
        set_sprite_palette(0, 8, tmpobj);
    }
}

// ---------------------------------------------------------------- sprites
int8_t cam_x, cam_y;
uint8_t win_cut;
static uint8_t nspr, nspr_prev;
static uint8_t *oam_p = (uint8_t *)shadow_OAM;

// a w x h block (w <= 8) of 8x8 sprites, tiles t, t + 1, ... row by row, attributes
// pr[k] | or_, clipped per sprite as spr_put (no win_cut): one call for a menu card's 12
void spr_grid(int16_t x, int16_t y, uint8_t t, const uint8_t *pr, uint8_t or_, uint8_t w, uint8_t h) {
    int16_t sy = y + cam_y + 16, sx = x + cam_x + 8;
    uint8_t r, c, cm = 0, xs[8], *o = oam_p, n = nspr;
    for (c = 0; c < w; c++, sx += 8) {
        xs[c] = (uint8_t)sx;
        if (sx > 0 && sx < 168) cm |= 1 << c;
    }
    for (r = 0; r < h; r++, sy += 8) {
        if (sy <= 0 || sy >= 160 || !cm) { t += w; pr += w; continue; }
        for (c = 0; c < w; c++, t++, pr++) {
            if (!(cm & (1 << c)) || n >= 40) continue;
            *o++ = (uint8_t)sy;
            *o++ = xs[c];
            *o++ = t;
            *o++ = *pr | or_;
            n++;
        }
    }
    oam_p = o;
    nspr = n;
}

// writes straight into shadow OAM; sprites are hidden again only if unused this frame.
// SDCC sm83 convention: DE = x, BC = y, tile and prop are bytes on the stack (callee pops).
void spr_put(int16_t x, int16_t y, uint8_t tile, uint8_t prop) __naked {
    (void)x; (void)y; (void)tile; (void)prop;
    __asm
    ld  a, (_nspr)
    cp  a, #40
    jr  nc, 9$
    ; sx = x + cam_x + 8, valid 1..167
    ld  a, (_cam_x)
    ld  l, a
    rlca
    sbc a, a
    ld  h, a
    add hl, de
    ld  de, #8
    add hl, de
    ld  a, h
    or  a, a
    jr  nz, 9$
    ld  a, l
    or  a, a
    jr  z, 9$
    cp  a, #168
    jr  nc, 9$
    ld  e, l
    ; sy = y + cam_y + 16, valid 1..159
    ld  a, (_cam_y)
    ld  l, a
    rlca
    sbc a, a
    ld  h, a
    add hl, bc
    ld  bc, #16
    add hl, bc
    ld  a, h
    or  a, a
    jr  nz, 9$
    ld  a, l
    or  a, a
    jr  z, 9$
    cp  a, #160
    jr  nc, 9$
    ld  d, l
    ; hidden under the score panel window?
    ld  a, (_win_cut)
    or  a, a
    jr  z, 5$
    ld  b, a
    ld  a, d
    cp  a, b
    jr  c, 5$
    jr  z, 5$
    cp  a, #128
    jr  c, 9$
5$:
    ldhl sp, #2
    ld  a, (hl+)
    ld  b, a
    ld  c, (hl)
    ld  hl, #_oam_p
    ld  a, (hl+)
    ld  h, (hl)
    ld  l, a
    ld  a, d
    ld  (hl+), a
    ld  a, e
    ld  (hl+), a
    ld  a, b
    ld  (hl+), a
    ld  a, c
    ld  (hl+), a
    ld  a, l
    ld  (_oam_p), a
    ld  a, h
    ld  (_oam_p + 1), a
    ld  hl, #_nspr
    inc (hl)
9$:
    pop hl
    pop af
    jp  (hl)
    __endasm;
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
    uint8_t i = nspr, *o = oam_p;
    for (; i < nspr_prev; i++, o += 4) *o = 0;
    nspr_prev = nspr;
    nspr = 0;
    oam_p = (uint8_t *)shadow_OAM;
}

// ---------------------------------------------------------------- frame loop
uint8_t frames;
#ifdef DEBUG_FPS
uint16_t dbg_loops;
#endif
uint8_t joy, joyp;
uint8_t scx, scy;
extern volatile uint8_t joy_latch;

uint8_t spr_hold;   // 1: keep last frame's sprites (nothing drawn this frame)

// Sliding dialog box: lines box_ly0..box_ly1-1 show BG map rows 20-23 (box_scy), the rest
// scrolls normally; the LCD interrupt switches SCY in the HBlank before each change (main.c).
uint8_t box_on, box_ly0, box_ly1, box_scy;
uint8_t ring_flip, ring_vram;
uint8_t box_nly0, box_nly1, box_nscy;   // next frame's, taken in VBlank

void frame(void) {
    if (!fading_out && fadelvl) {
        fadelvl--;
        pal_dirty = 1;
    }
    if (pal_dirty && fadelvl) pal_prepare();
    if (!spr_hold) spr_end();
    // the sprites are complete: copy them to OAM at this VBlank only. Between frames the
    // copy is off, so a loop that runs into VBlank while drawing (it happens) shows the last
    // frame's sprites rather than a mix of two frames.
    ENABLE_OAM_DMA;
    vsync();
    DISABLE_OAM_DMA;
    if (ring_flip) {   // the hidden ring tiles are complete: show them (scenes.c rings)
        LCDC_REG ^= LCDCF_BG8000;
        ring_flip = 0;
    }
    // VBlank work: DMA first (it must finish before the screen starts), the palettes, then
    // map blocks
    if (vd_n) vd_flush();
    if (pal_dirty) {
        pal_apply();
        pal_dirty = 0;
    }
    if (!vd_n && vq_n) vq_flush();
    SCX_REG = scx - cam_x;
    SCY_REG = scy - cam_y;
    if (box_on) {
        box_ly0 = box_nly0;
        box_ly1 = box_nly1;
        box_scy = box_nscy;
        if (!box_ly0) SCY_REG = box_scy;
        LYC_REG = (box_ly0 ? box_ly0 : box_ly1) - 1;
    }
    {
        uint8_t save = _current_bank;
        SWITCH_ROM(BANK(sound_bank));
        snd_update();
        SWITCH_ROM(save);
    }
    frames++;
#ifdef DEBUG_FPS
    dbg_loops++;
#endif
    CRITICAL {
        joyp = joy_latch;
        joy_latch = 0;
    }
    joy = joypad();
}

void fadeout(uint8_t spd) {   // spd 0: one level every 4 frames (the ending's fadeout(0.01))
    uint8_t t = 0;
    fading_out = 1;
    while (fadelvl < 25) {
        if (spd) fadelvl += spd;
        else if (!(++t & 3)) fadelvl++;
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
    pal_prepare();
    spr_end();
    ENABLE_OAM_DMA;
    vsync();
    DISABLE_OAM_DMA;
    pal_apply();
    pal_dirty = 0;
}

void screen_on(void) {
    fading_out = 0;
}

void clear_bg(void) {
    vq_n = vd_n = 0;
    cv_invalidate();
    // rows 0-17 of both maps are contiguous 32-byte rows
    VBK_REG = 1;
    vram_fill((uint8_t *)0x9800u, 0, 32 * 18);
    vram_fill((uint8_t *)0x9C00u, 0, 32 * 18);
    VBK_REG = 0;
    vram_fill((uint8_t *)0x9800u, 0, 32 * 18);
    vram_fill((uint8_t *)0x9C00u, 0, 32 * 18);
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
static const uint16_t pow10[4] = {10000, 1000, 100, 10};

// decimal digits by repeated subtraction (no 16-bit division on the SM83)
static char *put_dec(char *p, int16_t v) {
    uint16_t u;
    uint8_t i, d, started = 0;
    if (v < 0) { *p++ = '-'; u = (uint16_t)(-v); } else u = (uint16_t)v;
    for (i = 0; i < 4; i++) {
        d = 0;
        while (u >= pow10[i]) { u -= pow10[i]; d++; }
        if (d || started) { *p++ = '0' + d; started = 1; }
    }
    *p++ = '0' + (uint8_t)u;
    return p;
}

void s_num(int16_t v) { sp = put_dec(sp, v); }
char *s_end(void) { *sp = 0; return sbuf; }

const char *numstr(int16_t v) {
    char *out = nbuf[nbi];
    if (++nbi == 3) nbi = 0;
    *put_dec(out, v) = 0;
    return out;
}

const char *mlstr(int16_t v, uint8_t sign) {
    char *save = sp, *out;
    out = nbuf[nbi];
    if (++nbi == 3) nbi = 0;
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

