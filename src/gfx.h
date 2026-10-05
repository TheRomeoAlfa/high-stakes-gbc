#ifndef GFX_H
#define GFX_H
#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>

// ---- PICO-8 palette (RGB555)
extern const uint16_t pico[16];

// ---- canvases: rectangles of unique tiles that are software rendered
typedef struct {
    uint8_t *buf;
    uint8_t tile;   // first VRAM tile (BG index)
    uint8_t bank;   // VRAM bank
    uint8_t x, y;   // map position (tiles)
    uint8_t w, h;   // size in tiles
    uint8_t pal;    // BG palette
    uint8_t win;    // 1 = window map
} canvas_t;

extern uint8_t cvbuf[];   // shared scratch (160 tiles)
extern uint8_t dlgbuf[];  // persistent buffer for dialog / help text (80 tiles)

void cv_begin(const canvas_t *c, uint8_t col);  // select + clear
void cv_select(const canvas_t *c);              // select without clearing
void cv_pset(int16_t x, int16_t y, uint8_t col);
void cv_rect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t col);
void cv_frame(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t col);
void cv_rrect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t col);
int16_t cv_print(const char *s, int16_t x, int16_t y, uint8_t col);
int16_t cv_putc(char ch, int16_t x, int16_t y, uint8_t col);
void cv_cprint(const char *s, int16_t cx, int16_t y, uint8_t col);
void cv_rprint(const char *s, int16_t rx, int16_t y, uint8_t col);
void cv_bignum(const char *s, int16_t x, int16_t y, uint8_t col);
void cv_bignumc(const char *s, int16_t cx, int16_t y, uint8_t col, uint8_t ml);
void cv_hline(int16_t x0, int16_t x1, int16_t y, uint8_t col);
uint8_t text_w(const char *s);
uint8_t bignum_w(const char *s);
void cv_flush(const canvas_t *c);                 // upload tiles
void cv_flush_tile(const canvas_t *c, uint16_t i); // upload one tile
uint16_t cv_tile_at(int16_t x, int16_t y);       // tile index within canvas
void cv_place(const canvas_t *c);                 // write map + attributes
void cv_draw(const canvas_t *c);                  // flush + place (placement cached)
void cv_invalidate(void);                         // forget cached placements
void cv_flush_rows(const canvas_t *c, uint8_t r0, uint8_t n);
void cv_place_tiles(const canvas_t *c);           // tile indices only, uncached
void cv_place_attrs(const canvas_t *c);
void map_tiles(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tile);

// ---- map helpers
void map_fill(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tile, uint8_t attr);
void map_put(uint8_t win, uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *tiles, const uint8_t *attrs);
void map_put_vbl(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *tiles, const uint8_t *attrs);  // BG map, at next VBlank (tiles NULL = clear)
void load_bkg_banked(uint8_t rombank, const uint8_t *src, uint8_t first, uint8_t n, uint8_t vbank);
void load_spr_banked(uint8_t rombank, const uint8_t *src, uint8_t first, uint8_t n, uint8_t vbank);
void make_solid_tiles(void);
void vram_copy(uint8_t *dst, const uint8_t *src, uint16_t len);   // len multiple of 4
void vram_fill(uint8_t *dst, uint8_t v, uint16_t len);
void bkg_tiles_upload(uint8_t first, uint16_t n, const uint8_t *src);   // tiles 252..255 bank 0 = solid colour 0..3
#define TILE_SOLID(c) (252 + (c))

// ---- palettes & fades
extern uint16_t bgpal[32], objpal[32];
extern uint8_t fadelvl;   // 0 = normal, 16 = black
extern uint8_t pal_dirty;
void set_bgpal(uint8_t n, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3);
void set_objpal(uint8_t n, uint8_t c1, uint8_t c2, uint8_t c3);
void copy_bgpals(uint8_t first, uint8_t n, const uint16_t *src);
void copy_objpals(uint8_t first, uint8_t n, const uint16_t *src);

// ---- sprites
extern int8_t cam_x, cam_y;
void spr_put(int16_t x, int16_t y, uint8_t tile, uint8_t prop);
void spr_put16(int16_t x, int16_t y, uint8_t tile, uint8_t prop);  // 16x16 (2 tiles pairs)
void spr_end(void);

// ---- frame & input
extern uint8_t frames;
#ifdef DEBUG_FPS
extern uint16_t dbg_loops;   // main-loop frames vs sys_time (VBlanks) shows dropped frames
#endif
extern uint8_t joy, joyp;
extern uint8_t scx, scy;
#define BTNP(b) (joyp & (b))
void frame(void);
void fadeout(uint8_t spd);
void fadein_start(void);
void wait_frames(uint8_t n);
void screen_off(void);
void screen_on(void);
void clear_bg(void);

uint8_t rnd(uint8_t n);
extern uint8_t win_cut;  // sprites below this window line are hidden (0 = off)

// string builder (bank 0 so literals from any ROM bank are readable)
extern char sbuf[48];
void s_begin(void);
void s_str(const char *s);
void s_num(int16_t v);
char *s_end(void);
const char *numstr(int16_t v);               // "123" in a rotating buffer
const char *mlstr(int16_t v, uint8_t sign);  // "+12ML"

#endif
