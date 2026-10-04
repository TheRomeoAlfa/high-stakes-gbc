#!/usr/bin/env python3
"""Convert the original PICO-8 High Stakes cart into GBC assets.

Reads assets/highstakes.p8.png and writes:
  src/gen/assets.h      declarations + constants
  src/gen/assets0.c     small tables kept in ROM bank 0 (maps, palettes, fonts, audio)
  src/gen/assetsb.c     bulk tile data (autobanked)
  build/preview/*.png   previews of converted graphics
"""
import math
import os
import random
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from p8cart import read_cart  # noqa: E402

ROM = read_cart(os.path.join(ROOT, 'assets', 'highstakes.p8.png'))

PICO = [(0, 0, 0), (29, 43, 83), (126, 37, 83), (0, 135, 81), (171, 82, 54), (95, 87, 79),
        (194, 195, 199), (255, 241, 232), (255, 0, 77), (255, 163, 0), (255, 236, 39),
        (0, 228, 54), (41, 173, 255), (131, 118, 156), (255, 119, 168), (255, 204, 170)]
# The cart remaps its screen palette every frame (fadepal(0) with its fadetable):
# 3 -> 128, 4 -> 129, 9 -> 136, 10 -> 130 from PICO-8's secret palette.
PICO[3] = (0x29, 0x18, 0x14)
PICO[4] = (0x11, 0x1D, 0x35)
PICO[9] = (0xBE, 0x12, 0x50)
PICO[10] = (0x42, 0x21, 0x36)
T = -1  # transparent marker


def sheet(x, y):
    b = ROM[y * 64 + x // 2]
    return (b >> 4) if x & 1 else b & 15


def crop(x0, y0, w, h, transp=()):
    return [[(T if sheet(x, y) in transp else sheet(x, y)) for x in range(x0, x0 + w)]
            for y in range(y0, y0 + h)]


def blank(w, h, c=T):
    return [[c] * w for _ in range(h)]


def blit(dst, src, dx, dy, transp=True):
    for y, row in enumerate(src):
        for x, c in enumerate(row):
            if transp and c == T:
                continue
            yy, xx = dy + y, dx + x
            if 0 <= yy < len(dst) and 0 <= xx < len(dst[0]):
                dst[yy][xx] = c


def dist(a, b):
    if a == b:
        return 0
    if a == T or b == T:
        return 10 ** 9
    ra, ga, ba = PICO[a]
    rb, gb, bb = PICO[b]
    # weighted RGB distance
    return 2 * (ra - rb) ** 2 + 4 * (ga - gb) ** 2 + 3 * (ba - bb) ** 2


def rgb555(c):
    r, g, b = PICO[c]
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


# --------------------------------------------------------------------------- tiles

def enc_tile(px):
    """px: 8 rows of 8 indices 0..3 -> 16 bytes"""
    out = []
    for row in px:
        lo = hi = 0
        for x, v in enumerate(row):
            if v & 1:
                lo |= 0x80 >> x
            if v & 2:
                hi |= 0x80 >> x
        out += [lo, hi]
    return tuple(out)


def flip_t(px, fx, fy):
    rows = px[::-1] if fy else px
    return [r[::-1] if fx else list(r) for r in rows]


class Pool:
    """Deduplicating tile pool (uses CGB tile flip attributes)."""

    def __init__(self, base=0, cap=256, flips=True):
        self.tiles, self.index, self.base, self.cap, self.flips = [], {}, base, cap, flips

    def add(self, px):
        for fx in (0, 1) if self.flips else (0,):
            for fy in (0, 1) if self.flips else (0,):
                k = enc_tile(flip_t(px, fx, fy))
                if k in self.index:
                    return self.index[k], (fx << 5) | (fy << 6)
        k = enc_tile(px)
        idx = self.base + len(self.tiles)
        self.tiles.append(k)
        self.index[k] = idx
        assert len(self.tiles) <= self.cap, 'tile pool overflow'
        return idx, 0

    def data(self):
        return [b for t in self.tiles for b in t]


def tiles_of(img):
    h, w = len(img), len(img[0])
    for ty in range(h // 8):
        for tx in range(w // 8):
            yield tx, ty, [row[tx * 8:tx * 8 + 8] for row in img[ty * 8:ty * 8 + 8]]


def tile_err(tile, pal):
    e = 0
    for row in tile:
        for c in row:
            e += min(dist(c, p) for p in pal)
    return e


def map_tile(tile, pal):
    out = []
    for row in tile:
        r = []
        for c in row:
            best = min(range(len(pal)), key=lambda i: dist(c, pal[i]))
            r.append(best)
        out.append(r)
    return out


def derive_palettes(tiles, n, forced):
    """Cluster tile colour sets into n palettes of 4 colours (forced colours first)."""
    from collections import Counter
    hists = []
    for t in tiles:
        c = Counter(v for row in t for v in row)
        hists.append(c)
    sets = []
    for h in hists:
        cols = list(forced) + [c for c, _ in h.most_common() if c not in forced]
        s = tuple(sorted(cols[:4]))
        if s not in sets:
            sets.append(s)

    def trim(cols, weight):
        cols = list(forced) + sorted([c for c in cols if c not in forced], key=lambda c: -weight.get(c, 0))
        return tuple(sorted(cols[:4]))

    total = Counter()
    for h in hists:
        total.update(h)
    # merge pass
    sets = [tuple(s) for s in sets]
    while len(sets) > n:
        curs = [min(tile_err(t, s) for s in sets) for t in tiles]
        best = None
        for i in range(len(sets)):
            for j in range(i + 1, len(sets)):
                u = set(sets[i]) | set(sets[j])
                m = trim(u, total)
                cost = 0
                for t, h, cur in zip(tiles, hists, curs):
                    if set(h) <= set(sets[i]) or set(h) <= set(sets[j]):
                        cost += tile_err(t, m) - cur
                cost += 0.001 * (len(u) - 4 if len(u) > 4 else 0)
                if best is None or cost < best[0]:
                    best = (cost, i, j, m)
        _, i, j, m = best
        sets = [s for k, s in enumerate(sets) if k not in (i, j)] + [m]
    # order: forced colours first
    res = []
    for s in sets:
        cols = list(forced) + [c for c in s if c not in forced]
        while len(cols) < 4:
            cols.append(cols[-1])
        res.append(cols[:4])
    return res


def convert_bg(img, pals, pool, palbase, bank):
    """img (pico colours, T->0). Returns (map ids, attrs) lists row-major."""
    ids, attrs = [], []
    h, w = len(img) // 8, len(img[0]) // 8
    for tx, ty, t in tiles_of([[0 if c == T else c for c in row] for row in img]):
        pi = min(range(len(pals)), key=lambda i: tile_err(t, pals[i]))
        idx, fl = pool.add(map_tile(t, pals[pi]))
        ids.append(idx & 255)
        attrs.append((palbase + pi) | fl | (8 if bank else 0))
    return ids, attrs, w, h


# --------------------------------------------------------------------------- sprites (8x16)

class SprPool:
    def __init__(self, cap=128):
        self.tiles = []
        self.cap = cap

    def add_pair(self, top, bot):
        idx = len(self.tiles)
        self.tiles += [enc_tile(top), enc_tile(bot)]
        assert len(self.tiles) <= self.cap, 'sprite pool overflow'
        return idx

    def data(self):
        return [b for t in self.tiles for b in t]


def spr_convert(img, pal):
    """pal: 3 pico colours (index 1..3). T -> 0"""
    out = []
    for row in img:
        r = []
        for c in row:
            if c == T:
                r.append(0)
            else:
                r.append(1 + min(range(3), key=lambda i: dist(c, pal[i])))
        out.append(r)
    return out


def add_sprite(pool, img, pal, keep_empty=False):
    """Pads img to multiple of 8x16, returns list of (dx,dy,tile) parts."""
    w = (len(img[0]) + 7) // 8 * 8
    h = (len(img) + 15) // 16 * 16
    pad = blank(w, h)
    blit(pad, img, 0, 0)
    px = spr_convert(pad, pal)
    parts = []
    for sy in range(0, h, 16):
        for sx in range(0, w, 8):
            top = [r[sx:sx + 8] for r in px[sy:sy + 8]]
            bot = [r[sx:sx + 8] for r in px[sy + 8:sy + 16]]
            if not keep_empty and all(v == 0 for r in top + bot for v in r):
                continue
            parts.append((sx, sy, pool.add_pair(top, bot)))
    return parts


# --------------------------------------------------------------------------- small font

FONT = {
    'a': "### #.# ### #.# #.#", 'b': "### #.# ##. #.# ###", 'c': "### #.. #.. #.. ###",
    'd': "##. #.# #.# #.# ###", 'e': "### #.. ##. #.. ###", 'f': "### #.. ##. #.. #..",
    'g': "### #.. #.. #.# ###", 'h': "#.# #.# ### #.# #.#", 'i': "### .#. .#. .#. ###",
    'j': "### .#. .#. .#. ##.", 'k': "#.# #.# ##. #.# #.#", 'l': "#.. #.. #.. #.. ###",
    'm': "### ### #.# #.# #.#", 'n': "##. #.# #.# #.# #.#", 'o': ".## #.# #.# #.# ##.",
    'p': "### #.# ### #.. #..", 'q': ".#. #.# #.# ##. .##", 'r': "### #.# ##. #.# #.#",
    's': ".## #.. ### ..# ##.", 't': "### .#. .#. .#. .#.", 'u': "#.# #.# #.# #.# .##",
    'v': "#.# #.# #.# ### .#.", 'w': "#.# #.# #.# ### ###", 'x': "#.# #.# .#. #.# #.#",
    'y': "#.# #.# ### ..# ###", 'z': "### ..# .#. #.. ###",
    '0': "### #.# #.# #.# ###", '1': "##. .#. .#. .#. ###", '2': "### ..# ### #.. ###",
    '3': "### ..# .## ..# ###", '4': "#.# #.# ### ..# ..#", '5': "### #.. ### ..# ###",
    '6': "#.. #.. ### #.# ###", '7': "### ..# ..# ..# ..#", '8': "### #.# ### #.# ###",
    '9': "### #.# ### ..# ..#",
    '.': "... ... ... ... .#.", ',': "... ... ... .#. #..", '!': ".#. .#. .#. ... .#.",
    '?': "### ..# .## ... .#.", ':': "... .#. ... .#. ...", "'": ".#. .#. ... ... ...",
    '-': "... ... ### ... ...", '+': "... .#. ### .#. ...", '/': "..# .#. .#. .#. #..",
    '<': "..# .#. #.. .#. ..#", '>': "#.. .#. ..# .#. #..", '(': ".#. #.. #.. #.. .#.",
    ')': ".#. ..# ..# ..# .#.", '=': "... ### ... ### ...", '_': "... ... ... ... ###",
    '*': "#.# .#. ### .#. #.#", '"': "#.# #.# ... ... ...", '#': "#.# ### #.# ### #.#",
    'M': "#.# ### #.# #.# #.#", 'L': "#.. #.. #.. #.. ###", 'X': "#.# #.# .#. #.# #.#",
    ' ': "... ... ... ... ...", '@': ".## #.# #.# #.. .##",
    # specials (7 wide): \x01 A button, \x02 left, \x03 right, \x04 down, \x05 B button
    '\x01': ".#####. ###.### ##.#.## ##...## .#.#.#.",
    '\x02': "...#... ..##... .###### ..##... ...#...",
    '\x03': "...#... ...##.. ######. ...##.. ...#...",
    '\x04': "..###.. ..###.. ####### .#####. ...#...",
    '\x05': ".#####. ##..### ##.#.## ##..### ##...##",
}


def font_table():
    """96+6 glyphs: for codes 0..127, 5 row bytes (bit7 = leftmost) + width"""
    rows = []
    widths = []
    for code in range(128):
        ch = chr(code)
        if ch.isupper() and ch not in FONT:
            ch = ch.lower()
        g = FONT.get(ch)
        if g is None:
            rows.append([0] * 5)
            widths.append(4)
            continue
        parts = g.split()
        w = len(parts[0])
        rb = []
        for p in parts:
            v = 0
            for i, c in enumerate(p):
                if c == '#':
                    v |= 0x80 >> i
            rb.append(v)
        rows.append(rb)
        widths.append(w + 1)
    return rows, widths


# --------------------------------------------------------------------------- big font

BNUMI = "0,1,2,3,4,5,6,7,8,9,h,i,g,s,t,a,k,e,r,o,u,n,d,w,l,y,p".split(',')
BNUMX = [15, 23, 29, 37, 45, 53, 61, 69, 77, 85, 15, 23, 29, 45, 53, 61, 69, 77, 15, 23, 31, 39, 47, 55, 65, 72, 80]
BNUMY = [118] * 18 + [108] * 9
BNUMW = [8, 6, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4, 8, 8, 8, 8, 8, 6, 8, 8, 8, 8, 8, 10, 7, 8, 8]


def bigfont():
    glyphs = {}
    for i, ch in enumerate(BNUMI):
        alt = i >= 10
        on = (5, 7) if alt else (6, 7)
        rows = []
        for y in range(10):
            v = 0
            for x in range(BNUMW[i]):
                if sheet(BNUMX[i] + x, BNUMY[i] + y) in on:
                    v |= 1 << x
            rows.append(v)
        glyphs[ch] = (BNUMW[i], -1 if ch == 't' else 0, rows)
    return glyphs


# --------------------------------------------------------------------------- cards

CW, CH = 21, 29          # GB card size inside a 24x32 cell
OX, OY = 1, 1
HEART = crop(0, 8, 7, 7, transp=(11,))
CLOUT = "5,4|5,13#5,2|5,9|5,16#1,3|9,3|1,15|9,15#1,3|9,3|1,15|9,15|5,9#1,3|9,3|1,9|9,9|1,15|9,15#1,3|9,3|1,9|9,9|1,15|9,15|5,6#1,3|9,3|1,9|9,9|1,15|9,15|5,6|5,12#1,1|9,1|1,6|9,6|1,11|9,11|1,16|9,16|5,4".split('#')
CLOUT = [[tuple(map(int, p.split(','))) for p in s.split('|')] for s in CLOUT]
DIG = {d: FONT[d].split() for d in '0123456789'}


def card_frame_img():
    img = blank(CW, CH)
    for y in range(CH):
        for x in range(CW):
            if x in (0, CW - 1) and y in (0, CH - 1):
                continue
            img[y][x] = 10 if x in (0, CW - 1) or y in (0, CH - 1) else 0
    return img


def card_face(v):
    img = card_frame_img()
    if v <= 9:
        for (px, py) in CLOUT[v - 2]:
            gx = {1: 2, 5: 7, 9: 12}[px]
            gy = 1 + round((py - 1) * 20 / 15)
            blit(img, HEART, gx, gy)
        for y in range(1, 8):
            for x in range(1, 6):
                img[y][x] = 0
        for y, r in enumerate(DIG[str(v)]):
            for x, c in enumerate(r):
                if c == '#':
                    img[2 + y][2 + x] = 6
    else:
        face = crop(28 + (v - 10) * 15, 0, 15, 21)
        blit(img, face, 3, 4)
    return img


def card_back():
    img = card_frame_img()
    rng = random.Random(1031)
    for y in range(1, CH - 1):
        for x in range(1, CW - 1):
            ring = min(x, y, CW - 1 - x, CH - 1 - y)
            img[y][x] = 2 if ring == 1 else 9 if ring == 2 else 2
    # random 4-way symmetric pattern (like genbacks())
    qw, qh = 7, 11
    for y in range(qh):
        for x in range(qw):
            c = 9 if rng.random() < 0.4 else 2
            for (xx, yy) in ((3 + x, 3 + y), (CW - 4 - x, 3 + y), (3 + x, CH - 4 - y), (CW - 4 - x, CH - 4 - y)):
                img[yy][xx] = c
    return img


def squash(img, f, skew):
    out = blank(24, 32)
    w = len(img[0])
    for x in range(w):
        col = math.floor((x - 10) / f + 10) if f > 0 else -100
        if not 0 <= col < w:
            continue
        s = round((x - 10) / 10 * skew)
        for y in range(len(img)):
            c = img[y][col]
            if c != T:
                out[OY + y + s][OX + x] = c
    return out


def cell(img):
    out = blank(24, 32)
    blit(out, img, OX, OY)
    return out


def edge_img():
    out = blank(24, 32)
    for y in range(1, CH - 1):
        out[OY + y][OX + 10] = 10
    return out


FRONT_PAL = [0, 10, 2, 6]
BACK_PAL = [0, 10, 9, 2]

# --------------------------------------------------------------------------- outputs

H, C0, CB, AUD = [], [], [], []
PREVIEW = os.path.join(ROOT, 'build', 'preview')
os.makedirs(PREVIEW, exist_ok=True)


def carr(name, data, typ='uint8_t', banked=False, per=16):
    tgt = CB if banked else C0
    body = ',\n'.join('  ' + ','.join(str(v) for v in data[i:i + per]) for i in range(0, len(data), per))
    if banked:
        tgt.append(f'BANKREF({name})')
    tgt.append(f'const {typ} {name}[{len(data)}] = {{\n{body}\n}};\n')
    H.append(f'extern const {typ} {name}[{len(data)}];')
    if banked:
        H.append(f'BANKREF_EXTERN({name})')


def aud_arr(name, data, typ='uint8_t', per=16):
    body = ',\n'.join('  ' + ','.join(str(v) for v in data[i:i + per]) for i in range(0, len(data), per))
    AUD.append(f'static const {typ} {name}[{len(data)}] = {{\n{body}\n}};\n')


def define(name, v):
    H.append(f'#define {name} {v}')


def pal_arr(name, pals):
    data = [rgb555(c) for p in pals for c in p]
    carr(name, data, 'uint16_t', per=8)


def wpng(fn, img, pal_rgb=None):
    h, w = len(img), len(img[0])
    raw = b''
    for row in img:
        raw += b'\0' + bytes(v for c in row for v in ((40, 80, 40) if c == T else PICO[c]))

    def ch(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    open(os.path.join(PREVIEW, fn), 'wb').write(
        b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
        ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))


def render_map(pool_tiles, base, ids, attrs, w, h, pals, palbase):
    """Reconstruct an image from map for previews."""
    img = blank(w * 8, h * 8, 0)
    for i, (t, a) in enumerate(zip(ids, attrs)):
        tile = pool_tiles[t - base]
        px = [[((tile[r * 2] >> (7 - x)) & 1) | (((tile[r * 2 + 1] >> (7 - x)) & 1) << 1) for x in range(8)] for r in range(8)]
        px = flip_t(px, a & 0x20, a & 0x40)
        pal = pals[(a & 7) - palbase]
        tx, ty = i % w, i // w
        for y in range(8):
            for x in range(8):
                img[ty * 8 + y][tx * 8 + x] = pal[px[y][x]]
    return img


def main():
    H.append('#ifndef ASSETS_H\n#define ASSETS_H\n#include <gbdk/platform.h>\n#include <stdint.h>\n')

    # ---------------- fonts
    rows, widths = font_table()
    carr('font_rows', [b for r in rows for b in r])
    carr('font_w', widths)
    bf = bigfont()
    order = '0123456789abcdefghijklmnopqrstuvwxyz'
    bw, bk, brow = [], [], []
    for ch in order:
        if ch in bf:
            w, k, rr = bf[ch]
            bw.append(w)
            bk.append(k & 255)
            brow += rr
        else:
            bw.append(0)
            bk.append(0)
            brow += [0] * 10
    carr('big_w', bw)
    carr('big_kern', bk)
    carr('big_rows', brow, 'uint16_t', per=10)

    # ---------------- cards (bank 1, base 0)
    faces = {v: card_face(v) for v in range(2, 14)}
    back = card_back()
    frames = [cell(back), squash(back, 0.66, 1), squash(back, 0.3, 1), edge_img()]
    for v in range(2, 14):
        frames += [cell(faces[v]), squash(faces[v], 0.66, -1), squash(faces[v], 0.3, -1)]
    vtiles = []
    for v in range(10, 14):
        for fr in frames[4 + (v - 2) * 3: 4 + (v - 2) * 3 + 3]:
            for _, _, t in tiles_of([[0 if c == T else c for c in r] for r in fr]):
                if not any(c == 10 for row in t for c in row) and any(c != 0 for row in t for c in row):
                    vtiles.append(t)
    vpals = [[0, 6, 7, 13], [0, 5, 13, 8]]
    card_pals = [FRONT_PAL, BACK_PAL] + vpals
    print('vampire palettes', vpals)
    cpool = Pool(0, 256)
    fmap, fattr = [], []
    for fr in frames:
        ids, attrs, _, _ = convert_bg(fr, card_pals, cpool, 1, 1)
        fmap += ids
        fattr += attrs
    print('card tiles', len(cpool.tiles))
    define('N_CARD_TILES', len(cpool.tiles))
    define('CF_BACK', 0)
    define('CF_EDGE', 3)
    define('CF_FACE(v)', '(4 + ((v) - 2) * 3)')
    carr('card_tiles', cpool.data(), banked=True)
    carr('card_fmap', fmap)
    carr('card_fattr', fattr)
    pal_arr('card_pals', card_pals)
    prev = blank(24 * 10, 32 * 4, 0)
    for i in range(40):
        img = render_map(cpool.tiles, 0, fmap[i * 12:i * 12 + 12], fattr[i * 12:i * 12 + 12], 3, 4, card_pals, 1)
        blit(prev, img, (i % 10) * 24, (i // 10) * 32)
    wpng('cards.png', prev)

    # ---------------- game static BG tiles (bank 0, base 1): chip slots + sepline
    gpool = Pool(1, 120)
    slot_bg = crop(54, 21, 13, 10, transp=(11,))
    chip_spr = {1: (28, 21, 13, 10), 2: (41, 21, 13, 10), 3: (67, 21, 13, 10), 4: (80, 21, 13, 10),
                5: (93, 21, 7, 10), 6: (100, 21, 7, 10), 7: (19, 15, 9, 8), 8: (19, 23, 9, 8),
                9: (28, 31, 13, 10), 10: (41, 31, 13, 10)}

    def chip_img(s, cap=None):
        x, y, w, h = chip_spr[s]
        img = crop(x, y, w, h, transp=(11,))
        if cap:
            cx = 7 - len(cap) * 2
            for i, ch in enumerate(cap):
                for yy, r in enumerate(FONT[ch].split()):
                    for xx, c in enumerate(r):
                        if c == '#':
                            img[2 + yy][cx + i * 4 + xx] = 8
        return img

    slot_pals = [FRONT_PAL, [0, 1, 7, 8], [0, 1, 8, 15], [0, 1, 13, 6]]
    smaps, sattrs = [], []
    prev = blank(24 * 7, 16, 0)
    for k, s in enumerate([0, 1, 2, 3, 4, 9, 10]):
        img = blank(24, 16)
        blit(img, slot_bg, 5, 3)
        if s:
            blit(img, chip_img(s, '+' if s == 1 else None), 5, 3)
        ids, attrs, _, _ = convert_bg(img, slot_pals, gpool, 1, 0)
        # map palette index: 1->P1, 2->P5, 3->P6, 4->P7
        attrs = [(a & ~7) | {1: 1, 2: 5, 3: 6, 4: 7}[a & 7] for a in attrs]
        smaps += ids
        sattrs += attrs
        rp = {1: FRONT_PAL, 5: slot_pals[1], 6: slot_pals[2], 7: slot_pals[3]}
        im = blank(24, 16, 0)
        for i, (t, a) in enumerate(zip(ids, attrs)):
            tile = gpool.tiles[t - 1]
            px = [[((tile[r * 2] >> (7 - x)) & 1) | (((tile[r * 2 + 1] >> (7 - x)) & 1) << 1) for x in range(8)] for r in range(8)]
            px = flip_t(px, a & 0x20, a & 0x40)
            for y in range(8):
                for x in range(8):
                    im[(i // 3) * 8 + y][(i % 3) * 8 + x] = rp[a & 7][px[y][x]]
        blit(prev, im, k * 24, 0)
    wpng('slots.png', prev)
    carr('slot_map', smaps)
    carr('slot_attr', sattrs)
    # sepline: 20 tiles, line at y=3, x 8..151, caps "aa2a2a"
    sep = blank(160, 8, 0)
    for x in range(8, 152):
        sep[3][x] = 2
    cap = [sheet(x, 108) for x in range(6)]
    for i, c in enumerate(cap):
        sep[3][8 + i] = c
        sep[3][151 - i] = c
    ids, attrs, _, _ = convert_bg(sep, [FRONT_PAL], gpool, 1, 0)
    carr('sep_map', ids)
    carr('sep_attr', attrs)
    print('game static tiles', len(gpool.tiles))
    define('N_GAME_TILES', len(gpool.tiles))
    carr('game_tiles', gpool.data(), banked=True)

    # ---------------- sprites (bank 0, sprite tiles 0..127, 8x16)
    sp = SprPool(128)
    OPAL = [[1, 7, 0], [1, 7, 8], [1, 8, 15], [1, 13, 6], [9, 2, 10], [14, 2, 10], [14, 15, 7], [9, 8, 10]]
    spr_defs = []

    def sdef(name, img, pal, keep_empty=False):
        parts = add_sprite(sp, img, OPAL[pal], keep_empty)
        spr_defs.append((name, pal, parts))

    hand = crop(10, 15, 9, 10, transp=(11,))
    for y in range(10):
        if hand[y][8] == 1 and hand[y][7] == 7:
            hand[y][7] = 1
        hand[y] = hand[y][:8]
    sdef('HAND', hand, 0)
    for v in range(2, 10):
        sdef('TOK%d' % v, chip_img(1, '%d+' % v), 1)
    sdef('CHIP_PLUS', chip_img(1, '+'), 1)
    sdef('CHIP_ARROWS', chip_img(3), 2)
    sdef('CHIP_BOX', chip_img(9), 3)
    sdef('ARR_L', chip_img(5), 2)
    sdef('ARR_R', chip_img(6), 2)
    up = [[T if c == '.' else {'1': 1, '8': 8, 'f': 15, '9': 9}[c] for c in r] for r in
          [".111111.", "18888881", "188ff881", "18f88f81", "18888881", "19999991", ".111111."]]
    sdef('ARR_U', up, 2)
    dn = [[T if c == '.' else {'1': 1, '8': 8, 'f': 15, '9': 9}[c] for c in r] for r in
          [".111111.", "18888881", "18f88f81", "188ff881", "18888881", "19999991", ".111111."]]
    sdef('ARR_D', dn, 2)
    stake = crop(0, 16, 8, 56, transp=(0,))
    sdef('STAKE', stake, 4)
    # highlight: the original draws the stake in pink at +-1 offsets behind itself
    outl = blank(16, 58)
    for y in range(56):
        for x in range(8):
            if stake[y][x] != T:
                for dx, dy in ((0, 1), (2, 1), (1, 0), (1, 2)):
                    outl[y + dy][x + dx] = 14
    sdef('STAKE_HL', outl, 6, keep_empty=True)
    sdef('PFLOCK', crop(8, 25, 8, 28, transp=(11,)), 5)
    pfc = crop(8, 25, 8, 28, transp=(11,))
    for y in range(20, 28):
        pfc[y] = [T] * 8
    sdef('PFLOCKCUT', pfc, 5)
    sdef('STABCUR', crop(16, 31, 12, 13, transp=(11,)), 5)
    corner = blank(8, 16)
    for x in range(0, 8, 2):
        corner[0][x] = 9
    for y in range(0, 16, 2):
        corner[y][0] = 9
    sdef('CORNER', corner, 7)
    drip = blank(8, 16)
    for y in range(16):
        drip[y][3] = drip[y][4] = 8
    sdef('DRIP', drip, 7)
    drop = [[T if c == '.' else 8 for c in r] for r in ["...#....", "..###...", ".#####..", ".#####..", "..###..."]]
    sdef('DROP', drop, 7)
    for nm, g in (('MARR_L', '\x02'), ('MARR_R', '\x03')):
        img = blank(8, 5)
        for y, r in enumerate(FONT[g].split()):
            for x, c in enumerate(r):
                if c == '#':
                    img[y][x] = 9
        sdef(nm, img, 7)
    pal_arr('obj_pals', [[0] + p for p in OPAL])
    carr('spr_tiles', sp.data(), banked=True)
    define('N_SPR_TILES', len(sp.tiles))
    # metasprite tables: for each sprite: count, then (dx,dy,tile) triplets
    for name, pal, parts in spr_defs:
        define('SPR_' + name, parts[0][2])
        define('SPRPAL_' + name, pal)
        define('SPRW_' + name, max(p[0] for p in parts) + 8)
        if len(parts) > 1:
            define('SPRN_' + name, len(parts))
    print('sprite tiles', len(sp.tiles))

    # ---------------- images
    def image_scene(name, img, npals, palbase, base, bank=1, forced=(0,)):
        tl = [t for _, _, t in tiles_of([[0 if c == T else c for c in r] for r in img])]
        pals = derive_palettes(tl, npals, list(forced))
        pool = Pool(base, 256 - base)
        ids, attrs, w, h = convert_bg(img, pals, pool, palbase, bank)
        carr(name + '_tiles', pool.data(), banked=True)
        carr(name + '_map', ids)
        carr(name + '_attr', attrs)
        pal_arr(name + '_pals', pals)
        define('N_%s_TILES' % name.upper(), len(pool.tiles))
        define('%s_W' % name.upper(), w)
        define('%s_H' % name.upper(), h)
        wpng(name + '.png', render_map(pool.tiles, base, ids, attrs, w, h, pals, palbase))
        print(name, 'tiles', len(pool.tiles), 'pals', pals)

    def padimg(src, w, h, ox, oy):
        out = blank(w, h, 0)
        blit(out, src, ox, oy)
        return out

    IMG_BASE = len(cpool.tiles)
    define('IMG_BASE', IMG_BASE)
    image_scene('whisky', padimg(crop(100, 50, 28, 40), 32, 40, 2, 0), 3, 5, IMG_BASE)
    image_scene('glasses', padimg(crop(7, 69, 86, 39), 88, 40, 1, 0), 7, 1, 1, bank=0)
    image_scene('sunset', padimg(crop(16, 44, 84, 25), 88, 32, 2, 4), 7, 1, 1, bank=0)

    # ---------------- title logo (sprites, bank 1 sprite tiles)
    logo = crop(93, 90, 35, 38, transp=(11,))
    lw, lh = 40, 48
    lp = blank(lw, lh)
    blit(lp, logo, 2, 4)
    ltiles = []
    for sy in range(0, lh, 16):
        for sx in range(0, lw, 8):
            ltiles.append([r[sx:sx + 8] for r in lp[sy:sy + 16]])
    from collections import Counter
    # derive sprite palettes: treat T as forced "colour"
    lp_pals = derive_palettes([[[(c if c != T else 16) for c in r] for r in t] for t in ltiles], 3, [16]) \
        if False else None
    # logo uses 9,2,10,0 -> pick palettes per tile among combos
    combos = [[9, 2, 0], [9, 10, 0], [9, 2, 10]]
    lpool = SprPool(128)
    lparts = []
    for i, t in enumerate(ltiles):
        if all(c == T for r in t for c in r):
            continue
        best = min(range(3), key=lambda k: sum(min(dist(c, p) for p in combos[k]) for r in t for c in r if c != T))
        px = spr_convert(t, combos[best])
        idx = lpool.add_pair(px[:8], px[8:])
        lparts += [(i % 5) * 8, (i // 5) * 16, idx, best]
    carr('logo_tiles', lpool.data(), banked=True)
    carr('logo_parts', lparts)
    define('N_LOGO_PARTS', len(lparts) // 4)
    define('N_LOGO_TILES', len(lpool.tiles))
    pal_arr('logo_pals', [[0] + c for c in combos])

    # ---------------- title circles: 4-phase palette cycling rings
    # pixel phase class encodes (radius mod spacing); tile palette by zone
    cx, cy, sp_ = 80, 52, 10.0
    ring = blank(160, 104, 0)
    zone = []
    for y in range(104):
        for x in range(160):
            d = ((x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2) ** 0.5
            ring[y][x] = int(((sp_ - (d % sp_)) / sp_) * 4) % 4
    rpool = Pool(1, 200)
    rids, rattrs = [], []
    for tx, ty, t in tiles_of(ring):
        d = ((tx * 8 + 4 - cx) ** 2 + (ty * 8 + 4 - cy) ** 2) ** 0.5
        z = 0 if d < 15 else 1 if d < 26 else 2 if d < 41 else 3
        thick = 0 if d < 40 else 1 if d < 55 else 2 if d < 71 else 3
        idx, fl = rpool.add(t)
        rids.append(idx)
        rattrs.append(fl | z | (thick << 2) * 0)
        zone.append((z, thick))
    carr('ring_tiles', rpool.data(), banked=True)
    carr('ring_map', rids)
    carr('ring_attr', rattrs)
    carr('ring_thick', [t for _, t in zone])
    define('N_RING_TILES', len(rpool.tiles))
    print('ring tiles', len(rpool.tiles))

    # ---------------- vial bitmap 9x19 (pico colours 0,8,2)
    vial = crop(0, 109, 9, 19)
    carr('vial_px', [c for r in vial for c in r])

    # ---------------- audio
    aud_arr('p8_sfx', list(ROM[0x3200:0x3200 + 64 * 68]))
    aud_arr('p8_music', list(ROM[0x3100:0x3100 + 256]))
    ptab = []
    for i in range(108):
        p = i - 12
        f = 440 * 2 ** ((p - 33) / 12)
        x = round(2048 - 131072 / f)
        ptab.append(max(0, min(2047, x)))
    aud_arr('pitch_tab', ptab, 'uint16_t')
    ntab = []
    for p in range(64):
        f = 440 * 2 ** ((p - 33) / 12) * 24
        best = None
        for s in range(14):
            for r in range(8):
                rr = 0.5 if r == 0 else r
                fn = 524288 / rr / 2 ** (s + 1)
                e = abs(fn - f) / f
                if best is None or e < best[0]:
                    best = (e, (s << 4) | r)
        ntab.append(best[1])
    aud_arr('noise_tab', ntab)

    H.append('#endif')
    with open(os.path.join(ROOT, 'src', 'gen', 'assets.h'), 'w') as f:
        f.write('\n'.join(H) + '\n')
    with open(os.path.join(ROOT, 'src', 'gen', 'assets0.c'), 'w') as f:
        f.write('// generated by tools/gen_assets.py from the original PICO-8 cart\n#include "assets.h"\n\n' + '\n'.join(C0))
    with open(os.path.join(ROOT, 'src', 'gen', 'audio_data.h'), 'w') as f:
        f.write('// generated by tools/gen_assets.py: PICO-8 sfx/music data (included by sound.c)\n' + '\n'.join(AUD))
    with open(os.path.join(ROOT, 'src', 'gen', 'assetsb.c'), 'w') as f:
        f.write('// generated by tools/gen_assets.py\n#pragma bank 255\n#include "assets.h"\n\n' + '\n'.join(CB))


if __name__ == '__main__':
    main()
