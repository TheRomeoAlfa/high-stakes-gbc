#!/usr/bin/env python3
"""Convert the original PICO-8 High Stakes cart into GBC assets.

Reads assets/highstakes.p8.png and writes:
  src/gen/assets.h      declarations + constants
  src/gen/assets0.c     small tables kept in ROM bank 0 (maps, palettes, fonts, audio)
  src/gen/assetsb.c     bulk tile data (autobanked)
  build/preview/*.png   previews of converted graphics
"""
import itertools
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
# the cart's fadetable (line 14 of the source): per screen colour, its colours as it fades
FADETABLE = ("0,0,0,0,0,0,0,0,0,0,0,0,0,0,0|1,1,129,129,129,129,129,129,129,129,0,0,0,0,0|"
             "2,2,2,130,130,130,130,130,128,128,128,128,128,0,0|128,128,128,128,128,128,128,128,0,0,0,0,0,0,0|"
             "129,129,129,129,129,129,129,129,129,0,0,0,0,0,0|5,5,133,133,133,133,130,130,128,128,128,128,128,0,0|"
             "6,6,134,13,13,13,141,5,5,5,133,130,128,128,0|7,6,6,6,134,134,134,134,5,5,5,133,130,128,0|"
             "8,8,136,136,136,136,132,132,132,130,128,128,128,128,0|136,136,136,136,136,132,132,132,132,130,128,128,128,128,0|"
             "130,130,130,130,130,130,130,128,128,128,128,128,128,0,0|11,139,139,139,139,3,3,3,3,129,129,129,0,0,0|"
             "12,12,12,140,140,140,140,131,131,131,1,129,129,129,0|13,13,141,141,5,5,5,133,133,130,129,129,128,128,0|"
             "14,14,14,134,134,141,141,2,2,133,130,130,128,128,0|15,143,143,134,134,134,134,5,5,5,133,133,128,128,0")
T = -1  # transparent marker
VOL_EXP = float(os.environ.get('VOL_EXP', '0.65'))  # loudness compression exponent


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


def card_tile_err(tile, pal):
    """tile_err for card art: the border (colour 10) counts 8x, so a tile where it meets the
    face picks a palette that keeps it (a broken frame shows more than a shaded pixel)"""
    e = 0
    for row in tile:
        for c in row:
            d = min(dist(c, p) for p in pal)
            e += d * 8 if c == 10 else d
    return e


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


def convert_bg(img, pals, pool, palbase, bank, err=None):
    """img (pico colours, T->0). Returns (map ids, attrs) lists row-major."""
    ids, attrs = [], []
    h, w = len(img) // 8, len(img[0]) // 8
    err = err or tile_err
    for tx, ty, t in tiles_of([[0 if c == T else c for c in row] for row in img]):
        pi = min(range(len(pals)), key=lambda i: err(t, pals[i]))
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
    '\x02': ".#####. ###..## ##...## ###..## .#####.",   # PICO-8's ⬅️ and ➡️
    '\x03': ".#####. ##..### ##...## ##..### .#####.",
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
BACK_PAL = [0, 10, 2, 9]   # indices 0-2 match FRONT_PAL (lifted cards blend over both)

# --------------------------------------------------------------------------- card hop / flip
# The original flips a card by redrawing it column by column at an angle (rot 0 -> 1 on the back,
# -1 -> 0 on the face, 0.1 per frame) while it hops up (zd=1.4, gravity 0.15) over a drop shadow.
# Each of the 21 frames is pre-rendered into a 3x5 tile region: the row of tiles above the card's
# cell (where the lifted card overlaps its neighbour) and the 3x4 cell itself.

# Records are padded to 16-byte blocks: the tiles are copied to VRAM by CGB DMA straight from
# ROM, which needs 16-byte aligned sources (the lift files sit alone at the start of their banks).
LIFT_REC = 48 + 3 * 16 + 12 * 16   # header (palettes 0-14, overlap counts 15-17, masks 18-41),
#                                    overlap tiles, cell tiles
LIFT_SHREC = 16 + 12 * 16          # used-sprite mask, shadow sprite tiles


def lift_motion():
    z, zd, out = 0.0, 1.4, []
    for k in range(1, 22):
        z += zd
        zd -= 0.15
        if z <= 0:
            z, zd = 0.0, 0.0
        if k <= 10:
            rot = min(0.1 * k, 0.9995)        # k=10: edge-on, one column wide
        elif k == 11:
            rot = None                        # face side at rot=-1: nothing drawn
        else:
            rot = -1 + 0.1 * (k - 11)
        out.append((z, rot))
    return out


def lift_region(img, z, rot):
    """24x40 image: rows 0-7 = tile row above the cell, rows 8-39 = the cell."""
    out = blank(24, 40)
    if rot is None:
        return out
    angx = math.cos(math.pi * rot / 2)
    angy = -math.sin(math.pi * rot / 2)
    for x in range(21):
        col = math.floor((x - 10) / angx + 10)
        if not 0 <= col <= 20:
            continue
        off = math.floor(0.5 - (x - 10) * 0.5 * angy - z)
        for y in range(29):
            c = img[y][col]
            if c != T:
                yy = 8 + OY + y + off
                assert yy >= 0, 'lifted card leaves its region'
                out[yy][OX + x] = c
    return out


def lift_record(reg, card_pals, pref):
    """pref: palette tried first, so a tile that fits several keeps one palette across frames"""
    pals, tiles, masks, cnts = [], [], [], []
    order = [pref] + [i for i in range(len(card_pals)) if i != pref]
    for ty in range(5):
        for tx in range(3):
            t = [[0 if c == T else c for c in r[tx * 8:tx * 8 + 8]] for r in reg[ty * 8:ty * 8 + 8]]
            pi = min(order, key=lambda i: card_tile_err(t, card_pals[i]))
            pals.append(1 + pi)
            tiles += enc_tile(map_tile(t, card_pals[pi]))
            if ty == 0:
                m = []
                for r in reg[0:8]:
                    v = 0
                    for x in range(8):
                        if r[tx * 8 + x] != T:
                            v |= 0x80 >> x
                    m.append(v)
                masks += m
                cnts.append(sum(bin(v).count('1') for v in m))
    rec = pals + cnts + masks + [0] * 6 + tiles
    assert len(rec) == LIFT_REC
    return rec


def lift_shadow(reg, rot):
    """Shadow sprites (3x2 of 8x16) for the cell: the card silhouette at rest, 1px right/down,
    narrowed like the card, minus wherever the lifted card covers it."""
    w = 0 if rot is None else math.floor(21 * math.cos(math.pi * rot / 2) + 1e-9)
    sh = blank(24, 32, 0)
    if w > 0:
        x0 = OX + 1 + (21 - w) // 2
        for y in range(OY + 1, OY + 30):
            for x in range(x0, x0 + w):
                corner = w > 2 and x in (x0, x0 + w - 1) and y in (OY + 1, OY + 29)
                if not corner and reg[8 + y][x] == T:
                    sh[y][x] = 3
    data, used = [], 0
    for r in range(2):
        for c in range(3):
            part = [row[c * 8:c * 8 + 8] for row in sh[r * 16:r * 16 + 16]]
            if any(v for row in part for v in row):
                used |= 1 << (r * 3 + c)
            data += enc_tile(part[:8]) + enc_tile(part[8:])
    return [used] + [0] * 15 + data


def lift_base_ent(pal, px):
    """palette number, non-zero pixel count (unused at run time), 16 bytes of tile"""
    return [pal, sum(1 for r in px for v in r if v)] + list(enc_tile(px))


def board_lut(src, dst):
    """nearest colour of each src palette entry in dst, as lift.c's lift_luts computes it"""
    def c5(c):
        r, g, b = PICO[c]
        return r >> 3, g >> 3, b >> 3
    out = []
    for a in src:
        ra, ga, ba = c5(a)
        best, bd = 0, None
        for j, b in enumerate(dst):
            rb, gb, bb = c5(b)
            d = 2 * (ra - rb) ** 2 + 4 * (ga - gb) ** 2 + 3 * (ba - bb) ** 2
            if bd is None or d < bd:
                best, bd = j, d
        out.append(best)
    return out


def lift_frames(faces, back, card_pals, slot_bot, slot_pals):
    motion = lift_motion()
    # back side: frames k=1..10; face side: k=12..21 per card value (k=11 is blank)
    # (the back group ends with the blank k=11 frame)
    groups = [('lift_b', back, range(0, 11))] + [('lift_f%d' % v, faces[v], range(11, 21)) for v in range(2, 14)]
    names = []
    for gi, (name, img, ks) in enumerate(groups):
        data = []
        for k in ks:
            z, rot = motion[k]
            k1 = 11 if k == 10 and name == 'lift_b' else k + 1
            data += lift_record(lift_region(img, z, rot) if k1 != 11 else blank(24, 40), card_pals,
                                1 if name == 'lift_b' else 0)
        carr(name, data, banked=True, tgt=LIFTF[gi * 4 // len(groups)])
        names.append(name)
    # cards raised flat (rot 0): a face-up card bounced by A (z 1-3 px) and a face-down card
    # under the cursor (zbase 1). Frames 21-23 / 24 in lift.c; no pre-blended overlaps.
    zdata = lift_record(lift_region(back, 1, 0), card_pals, 1)
    for v in range(2, 14):
        for z in (1, 2, 3):
            zdata += lift_record(lift_region(faces[v], z, 0), card_pals, 0)
    carr('lift_z', zdata, banked=True, tgt=LIFTP[6])
    shadow = []
    for k in range(21):
        z, rot = motion[k]
        shadow += lift_shadow(lift_region(back, z, rot), rot)
    for z in (1, 2, 3):   # shadows 21-23: raised flat
        shadow += lift_shadow(lift_region(back, z, 0), 0)
    carr('lift_shadow', shadow, banked=True, tgt=LIFTF[3])
    define('LIFT_REC', LIFT_REC)
    define('LIFT_SHREC', LIFT_SHREC)
    C0.append('const uint8_t *const lift_ptr[%d] = {%s};\n' % (len(names), ', '.join(names)))
    H.append('extern const uint8_t *const lift_ptr[%d];' % len(names))
    C0.append('uint8_t lift_bank(uint8_t g) {\n  switch (g) {\n' +
              ''.join('  case %d: return BANK(%s);\n' % (i, n) for i, n in enumerate(names)) +
              '  default: return BANK(lift_shadow);\n  }\n}\n')
    H.append('uint8_t lift_bank(uint8_t g);   // g = 0 back, 1 + value - 2 faces, 255 shadow')
    # what the lifted card overlaps: the bottom tile row of a card frame at rest (back, faces 2..13)
    # or of a top slot (7 kinds), as palette number + plain 2bpp tile (no flips)
    bases = []
    frames = [cell(back)] + [cell(faces[v]) for v in range(2, 14)]
    for fr in frames:
        for tx in range(3):
            t = [[0 if c == T else c for c in r[tx * 8:tx * 8 + 8]] for r in fr[24:32]]
            pi = min(range(len(card_pals)), key=lambda i: card_tile_err(t, card_pals[i]))
            bases.append((1 + pi, map_tile(t, card_pals[pi])))
    bases += slot_bot
    bot = []
    for pal, px in bases:
        bot += lift_base_ent(pal, px)
    carr('lift_base_data', bot, banked=True, tgt=LIFTF[3])
    # The common overlaps, pre-blended: the 7 top slot kinds (variants 0-6) and a card back
    # (variant 7). Each record: 8 x (3 tiles, 3 palettes, padding) = 512 bytes.
    board = {1: card_pals[0], 2: card_pals[1], 3: card_pals[2], 4: card_pals[3]}
    board.update({5: slot_pals[1], 6: slot_pals[2], 7: slot_pals[3]})
    variants = [bases[(13 + k) * 3:(14 + k) * 3] for k in range(7)] + [bases[0:3]]
    pnames = []
    for gi, (name, img, ks) in enumerate(groups):
        data = []
        for k in ks:
            z, rot = motion[k]
            k1 = 11 if k == 10 and name == 'lift_b' else k + 1
            reg = lift_region(img, z, rot) if k1 != 11 else blank(24, 40)
            pref = 1 if name == 'lift_b' else 0
            order = [pref] + [i for i in range(len(card_pals)) if i != pref]
            cards3 = []
            for tx in range(3):
                rows = [r[tx * 8:tx * 8 + 8] for r in reg[0:8]]
                t = [[0 if c == T else c for c in r] for r in rows]
                pi = min(order, key=lambda i: card_tile_err(t, card_pals[i]))
                cards3.append((1 + pi, map_tile(t, card_pals[pi]), [[c != T for c in r] for r in rows]))
            for var in variants:
                tiles, pals = [], []
                for (bp, bpx), (cp, cpx, msk) in zip(var, cards3):
                    cnt = sum(1 for r in msk for m in r if m)
                    bcnt = sum(1 for r in bpx for v in r if v)
                    if cnt and bcnt <= cnt:     # mostly card: recolour the base
                        lut = board_lut(board[bp], board[cp])
                        bpx = [[lut[v] for v in r] for r in bpx]
                        bp = cp
                    elif cnt:                   # mostly base: recolour the card's pixels
                        lut = board_lut(board[cp], board[bp])
                        cpx = [[lut[v] for v in r] for r in cpx]
                    out = [[cpx[y][x] if msk[y][x] else bpx[y][x] for x in range(8)] for y in range(8)]
                    tiles += enc_tile(out)
                    pals.append(bp)
                data += tiles + pals + [0] * 13
        pn = name.replace('lift_', 'liftp_')
        carr(pn, data, banked=True, tgt=LIFTP[gi // 2])
        pnames.append(pn)
    define('LIFT_PREREC', 512)
    C0.append('const uint8_t *const liftp_ptr[%d] = {%s};\n' % (len(pnames), ', '.join(pnames)))
    H.append('extern const uint8_t *const liftp_ptr[%d];' % len(pnames))
    C0.append('uint8_t liftp_bank(uint8_t g) {\n  switch (g) {\n' +
              ''.join('  case %d: return BANK(%s);\n' % (i, n) for i, n in enumerate(pnames[:-1])) +
              '  default: return BANK(%s);\n  }\n}\n' % pnames[-1])
    H.append('uint8_t liftp_bank(uint8_t g);')
    print('lift data', sum(len(x) for part in LIFTF for x in part) // 6, 'bytes (approx)')
    # preview: all 21 frames of a 5 flip
    prev = blank(24 * 21, 40, 4)
    for k in range(21):
        z, rot = motion[k]
        reg = lift_region(back if k < 10 else faces[5], z, rot)
        sh = lift_shadow(reg, rot)
        for y in range(40):
            for x in range(24):
                if reg[y][x] != T:
                    prev[y][k * 24 + x] = reg[y][x]
                else:
                    prev[y][k * 24 + x] = 0
        # shadow overlay (cell rows)
        for r in range(2):
            for c in range(3):
                base = 16 + (r * 3 + c) * 32
                for half in range(2):
                    tb = sh[base + half * 16: base + half * 16 + 16]
                    for yy in range(8):
                        for xx in range(8):
                            if (tb[yy * 2] >> (7 - xx)) & 1:
                                prev[8 + r * 16 + half * 8 + yy][k * 24 + c * 8 + xx] = 4
    wpng('lift.png', prev)


# --------------------------------------------------------------------------- outputs

H, C0, CB, AUD = [], [], [], []
PREVIEW = os.path.join(ROOT, 'build', 'preview')
os.makedirs(PREVIEW, exist_ok=True)


LIFTF = [[], [], [], []]   # src/gen/lift0..3.c: card hop/flip animation, each alone in a fixed bank
LIFT_BANK0 = 12
LIFTP = [[] for _ in range(7)]   # src/gen/liftp0..6.c: pre-blended overlap rows, banks 5-11
LIFTP_BANK0 = 5
RINGF = [[] for _ in range(15)]   # src/gen/rings0..14.c: title / menu / winnings ring frames, a full bank each
RING_BANK0 = 17


def carr(name, data, typ='uint8_t', banked=False, per=16, tgt=None):
    if tgt is None:
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
    # rows as MSB-first bytes (bit7 = leftmost): left 8 columns and columns 8-9
    def rev8(v):
        return int('{:08b}'.format(v & 255)[::-1], 2)
    carr('big_l', [rev8(r) for r in brow], per=10)
    carr('big_r', [rev8(r >> 8) for r in brow], per=10)

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
    # vampire faces: skull grays + white, and red (the V) + the border plum + lavender, so
    # the tiles where the border meets the V or the skull keep the border (chosen by search)
    vpals = [[0, 5, 6, 7], [0, 8, 10, 13]]
    card_pals = [FRONT_PAL, BACK_PAL] + vpals
    print('vampire palettes', vpals)
    cpool = Pool(0, 256)
    fmap, fattr = [], []
    for fr in frames:
        ids, attrs, _, _ = convert_bg(fr, card_pals, cpool, 1, 1, card_tile_err)
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

    # ---------------- opponent menu cards as sprites (they drift one by one, over the rings):
    # the four vampire faces as 8x8 sprites, transparent only outside the card, so black
    # (opaque) takes a colour of each palette: five OBJ palettes (0-3 and 7; the title's logo
    # has 4-6 while it moves to the menu) of black and two colours, chosen to fit the tiles
    mt_src = []
    for v in range(10, 14):
        for tx, ty, t in tiles_of(cell(faces[v])):
            mt_src.append(t)
    used = sorted({c for t in mt_src for r in t for c in r if c != T and c != 0})
    cands = [[0, a, b] for i, a in enumerate(used) for b in used[i + 1:]]
    def mt_err(t, pal):
        return sum((min(dist(c, p) for p in pal) * (8 if c in (8, 10) else 1)) for r in t for c in r if c != T)   # the border and the V first
    chosen = []
    for _ in range(5):
        best = min(cands, key=lambda pal: sum(min(mt_err(t, q) for q in chosen + [pal]) for t in mt_src))
        chosen.append(best)
    obj_no = [0, 1, 2, 3, 7]
    mtiles, mpal = [], []
    for t in mt_src:
        pi = min(range(5), key=lambda i: mt_err(t, chosen[i]))
        pal = chosen[pi]
        mtiles += enc_tile([[0 if c == T else 1 + min(range(3), key=lambda i: dist(c, pal[i])) for c in r] for r in t])
        mpal.append(obj_no[pi])
    print('menu card palettes', chosen, 'error', sum(min(mt_err(t, q) for q in chosen) for t in mt_src))
    carr('menu_spr_tiles', mtiles, banked=True)
    carr('menu_spr_pal', mpal)
    pal_arr('menu_obj_pals', [[0] + p for p in chosen])   # OBJ 0-3, then 7

    # ---------------- game static BG tiles (bank 0, base 1): chip slots + sepline
    gpool = Pool(1, 120)
    slot_bg = crop(54, 21, 13, 10, transp=(11,))
    chip_spr = {1: (28, 21, 13, 10), 2: (41, 21, 13, 10), 3: (67, 21, 13, 10), 4: (80, 21, 13, 10),
                5: (93, 21, 7, 10), 6: (100, 21, 7, 10), 7: (19, 15, 9, 8), 8: (19, 23, 9, 8),
                9: (28, 31, 13, 10), 10: (41, 31, 13, 10)}

    def chip_img(s, cap=None, dx=0):
        x, y, w, h = chip_spr[s]
        img = crop(x, y, w, h, transp=(11,))
        if cap:
            cx = 7 - len(cap) * 2 + dx
            for i, ch in enumerate(cap):
                for yy, r in enumerate(FONT[ch].split()):
                    for xx, c in enumerate(r):
                        if c == '#':
                            img[2 + yy][cx + i * 4 + xx] = 8
        return img

    slot_pals = [FRONT_PAL, [0, 1, 7, 8], [0, 1, 8, 15], [0, 1, 13, 6]]
    smaps, sattrs = [], []
    slot_bot = []   # per slot kind: bottom-row tiles as (palette number, 8x8 indices)
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
            if i >= 3:
                slot_bot.append((a & 7, px))
            for y in range(8):
                for x in range(8):
                    im[(i // 3) * 8 + y][(i % 3) * 8 + x] = rp[a & 7][px[y][x]]
        blit(prev, im, k * 24, 0)
    wpng('slots.png', prev)
    carr('slot_map', smaps)
    lift_frames(faces, back, card_pals, slot_bot, slot_pals)
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
    OPAL = [[1, 7, 0], [1, 7, 8], [1, 8, 15], [1, 13, 6], [9, 2, 10], [14, 2, 10], [14, 15, 7], [9, 8, 4]]   # pal 7 slot 3: card shadow
    spr_defs = []

    def sdef(name, img, pal, keep_empty=False):
        parts = add_sprite(sp, img, OPAL[pal], keep_empty)
        spr_defs.append((name, pal, parts))

    hand = crop(10, 15, 9, 10, transp=(11,))   # full 9x10 hand, fingertip at (3, 0)
    sdef('HAND', hand, 0, keep_empty=True)
    for v in range(2, 10):
        sdef('TOK%d' % v, chip_img(1, '%d+' % v, 1), 1)   # 1 px right: cprint centres 2 chars 1 px left
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
    # a card back in flight during the deal: 3x2 parts of 8x16, row by row (stake palette)
    sdef('CARDBACK', cell(card_back()), 4, keep_empty=True)
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

    # ---------------- title / menu / winnings circles (docircs): rings 10 px apart grow 1 px a
    # step, each born where the logo (or the chosen card) is and drifting to the screen centre
    # as it grows: radius r is centred (80, 48 + D * (1 - r / 100)) (the original's myage =
    # r / 90, at 10/9 scale). D is fixed per frame set: the title's logo bob (-3..3), the
    # menu's chosen card (-14), between them for the title-to-menu move (-7, -11), and the
    # winnings' (-10). The title, the move and the menu share one map, so the title can hand
    # its rings to the menu. D being vertical, the ring area (20x13 tiles) is mirrored
    # left-right from its right half with tile flips: 130 tiles. A scene streams each frame by
    # DMA into the hidden one of two copies, 0x8000 or 0x9000 (LCDC.4 picks the one shown):
    # 128 in VRAM bank 1 (tile 0 blank), the rest in bank 0's 124-127.
    # Banks: 8 frames of 128 tiles each, then one bank of every frame's extra tiles (4 each).
    # A ring's colour goes by its radius (zones 0-5: the original's circcols, the outer band
    # split so the title-to-menu recolouring can sweep out); a tile's palette z holds zones
    # z..z+2 as colours 1-3, z being the nearest zone it ever shows.
    cy, sp_, RR = 48, 10, 100
    def rzone(r):
        return 0 if r < 15 else 1 if r < 26 else 2 if r < 41 else 3 if r < 60 else 4 if r < 80 else 5
    def rthick(r):   # the original's 1/2/3/4 past radius 40/55/71, scaled by 10/9
        return 1 if r <= 44 else 2 if r <= 61 else 3 if r <= 79 else 4
    ring_scenes = [
        ('TITLE', list(range(-3, 4)) + [-7, -11, -14], lambda ty, tx: False),   # sets 7-9: the move, the menu
        ('WIN', [-10], lambda ty, tx: 4 <= ty <= 6 and 2 <= tx <= 17),
    ]
    ring_frames = []
    for rname, dset, excl in ring_scenes:
        cells = [(ty, hc) for ty in range(13) for hc in range(10) if not excl(ty, 10 + hc)]
        assert all(excl(ty, 10 + hc) == excl(ty, 9 - hc) for ty in range(13) for hc in range(10))
        assert len(cells) <= 131
        zmin = [9] * len(cells)
        zmax = [-1] * len(cells)
        raw = []
        for D in dset:
            for ph in range(sp_):
                rings = [(r, cy + D * max(0.0, 1 - r / RR), rthick(r), rzone(r))
                         for r in (k * sp_ + ph for k in range(12)) if r > 0]
                fr = []
                for ci, (ty, hc) in enumerate(cells):
                    px = []
                    for y in range(8):
                        yy = ty * 8 + y + 0.5
                        for x in range(8):
                            xx = hc * 8 + x + 0.5
                            z = -1
                            for r, ccy, t, rz in rings:
                                d = math.hypot(xx, yy - ccy)
                                if r - t < d <= r + 0.5:
                                    z = rz
                                    break
                            if z >= 0:
                                zmin[ci] = min(zmin[ci], z)
                                zmax[ci] = max(zmax[ci], z)
                            px.append(z)
                    fr.append(px)
                raw.append(fr)
        tz = [z if z < 9 else 0 for z in zmin]
        over = sum(1 for i in range(len(cells)) if zmax[i] - tz[i] > 2)
        base = len(ring_frames)
        for fr in raw:
            data = [0] * 16
            for ci, px in enumerate(fr):
                data += enc_tile([[0 if px[y * 8 + x] < 0 else 1 + min(2, px[y * 8 + x] - tz[ci])
                                   for x in range(8)] for y in range(8)])
            extra = data[2048:]
            data = data[:2048]
            ring_frames.append((data + [0] * (2048 - len(data)), extra + [0] * (64 - len(extra))))
        idx = {c: i + 1 for i, c in enumerate(cells)}   # 128-131: bank 0's 124-127
        rids, rattrs = [], []
        for ty in range(13):
            for tx in range(20):
                hc = tx - 10 if tx >= 10 else 9 - tx
                if excl(ty, tx):
                    rids.append(0)
                    rattrs.append(0x08)
                else:
                    k = idx[(ty, hc)]
                    rids.append(k if k < 128 else k - 4)
                    rattrs.append(tz[k - 1] | (0x20 if tx < 10 else 0) | (0x08 if k < 128 else 0))
        if rname == 'TITLE':   # the menu's text over the rings (scenes.c menu_comp): its cells' ring tiles
            mt = []
            for ty, tx in [(8 + r, 4 + c) for r in range(4) for c in range(12)] + \
                          [(3 + r, x) for r in range(2) for x in (6, 7, 12, 13)]:
                k, a = rids[ty * 20 + tx], rattrs[ty * 20 + tx]
                assert 0 < k < 128 and a & 0x08
                mt += [k, min(a & 7, 2) | (0x80 if a & 0x20 else 0)]
            carr('ring_mt', mt, banked=True)
        carr('ring_map_' + rname.lower(), rids, banked=True)
        carr('ring_attr_' + rname.lower(), rattrs, banked=True)
        define('RING_' + rname, base)
        if rname == 'TITLE':
            define('RING_MENU', base + 9 * sp_)
        define('RING_NT_' + rname, min(128, len(cells) + 1))
        define('RING_NX_' + rname, max(0, len(cells) + 1 - 128))
        print('rings', rname.lower(), len(raw), 'frames of', len(cells), 'tiles,', over, 'tiles over 3 zones')
    assert len(ring_frames) <= 8 * (len(RINGF) - 1) and len(ring_frames) * 64 <= 16384
    for i in range(len(RINGF) - 1):
        blob = [v for f in ring_frames[i * 8:i * 8 + 8] for v in f[0]]
        carr('ring_b%d' % i, blob + [0] * (16384 - len(blob)), banked=True, tgt=RINGF[i])
    blob = [v for f in ring_frames for v in f[1]]
    carr('ring_b%d' % (len(RINGF) - 1), blob + [0] * (16384 - len(blob)), banked=True, tgt=RINGF[-1])
    define('RING_XBANK', RING_BANK0 + len(RINGF) - 1)
    define('RING_BANK0', RING_BANK0)
    define('RING_STEPS', sp_)

    # ---------------- PICO-8 fades: each screen colour walks its row of the cart's fadetable
    # (fadepal: index flr(perc * 16 + 1), black past the row's end); fade_ramp[c][lvl] for the
    # GBC's fade levels 0..25 (perc = lvl / 25). Included by fade.c, so it sits in its bank.
    std = [(0, 0, 0), (29, 43, 83), (126, 37, 83), (0, 135, 81), (171, 82, 54), (95, 87, 79),
           (194, 195, 199), (255, 241, 232), (255, 0, 77), (255, 163, 0), (255, 236, 39),
           (0, 228, 54), (41, 173, 255), (131, 118, 156), (255, 119, 168), (255, 204, 170)]
    ext = [(0x29, 0x18, 0x14), (0x11, 0x1D, 0x35), (0x42, 0x21, 0x36), (0x12, 0x53, 0x59),
           (0x74, 0x2F, 0x29), (0x49, 0x33, 0x3B), (0xA2, 0x88, 0x79), (0xF3, 0xEF, 0x7D),
           (0xBE, 0x12, 0x50), (0xFF, 0x6C, 0x24), (0xA8, 0xE7, 0x2E), (0x00, 0xB5, 0x43),
           (0x06, 0x5A, 0xB5), (0x75, 0x46, 0x65), (0xFF, 0x6E, 0x59), (0xFF, 0x9D, 0x81)]
    ft = [[int(v) for v in row.split(',')] for row in FADETABLE.split('|')]
    def c555(i):
        r, g, b = std[i] if i < 16 else ext[i - 128]
        return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
    rows = []
    for c in range(16):
        assert c555(ft[c][0]) == rgb555(c), c   # the screen palette is each row's first entry
        rows.append([c555(ft[c][s]) if s < 15 else 0 for s in (lvl * 16 // 25 for lvl in range(26))])
    with open(os.path.join(ROOT, 'src', 'gen', 'fade_ramp.h'), 'w') as f:
        f.write('// generated by tools/gen_assets.py: PICO-8 fade colours per screen colour and level\n'
                'static const uint16_t fade_ramp[26][16] = {   // [level][colour]\n' +
                ',\n'.join('  {' + ','.join('0x%04X' % rows[c][l] for c in range(16)) + '}' for l in range(26)) +
                '\n};\n')
        # screen colour by (lo ^ hi) of its RGB555 value, + 1 (0: not a screen colour)
        hsh = [0] * 256
        for c in range(16):
            v = rgb555(c)
            k = (v ^ (v >> 8)) & 0xFF
            assert hsh[k] == 0
            hsh[k] = c + 1
        f.write('static const uint8_t fade_hash[256] = {\n' +
                ',\n'.join('  ' + ','.join(str(x) for x in hsh[i:i + 32]) for i in range(0, 256, 32)) + '\n};\n')

    # ---------------- palette fade table: fade_tab[k][c] = c * k^2 / 625, k = 0..25
    carr('fade_tab', [c * k * k // 625 for k in range(26) for c in range(32)])

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
    # loudness compression: PICO-8 mixes in float, the GB has 15 volume steps per
    # channel, so quiet parts would round to silence. Map linear amplitude
    # (vol*16, 0..240, indexed by /4) through a square-root curve.
    aud_arr('vol_comp', [round(240 * (i / 60) ** VOL_EXP) for i in range(61)])
    # reciprocal of note length in frames: 65536 / nd (nd >= 1)
    aud_arr('recip_tab', [min(65535, 65536 // n) for n in range(1, 129)], 'uint16_t')

    H.append('#endif')
    with open(os.path.join(ROOT, 'src', 'gen', 'assets.h'), 'w') as f:
        f.write('\n'.join(H) + '\n')
    with open(os.path.join(ROOT, 'src', 'gen', 'assets0.c'), 'w') as f:
        f.write('// generated by tools/gen_assets.py from the original PICO-8 cart\n#include "assets.h"\n\n' + '\n'.join(C0))
    with open(os.path.join(ROOT, 'src', 'gen', 'audio_data.h'), 'w') as f:
        f.write('// generated by tools/gen_assets.py: PICO-8 sfx/music data (included by sound.c)\n' + '\n'.join(AUD))
    with open(os.path.join(ROOT, 'src', 'gen', 'assetsb.c'), 'w') as f:
        f.write('// generated by tools/gen_assets.py\n#pragma bank 255\n#include "assets.h"\n\n' + '\n'.join(CB))
    for i, part in enumerate(LIFTP):
        with open(os.path.join(ROOT, 'src', 'gen', 'liftp%d.c' % i), 'w') as f:
            f.write('// generated by tools/gen_assets.py: pre-blended card hop overlap rows\n#pragma bank %d\n'
                    '#include "assets.h"\n\n' % (LIFTP_BANK0 + i) + '\n'.join(part))
    for i, part in enumerate(RINGF):
        with open(os.path.join(ROOT, 'src', 'gen', 'rings%d.c' % i), 'w') as f:
            f.write('// generated by tools/gen_assets.py: ring frames\n#pragma bank %d\n'
                    '#include "assets.h"\n\n' % (RING_BANK0 + i) + '\n'.join(part))
    for i, part in enumerate(LIFTF):
        with open(os.path.join(ROOT, 'src', 'gen', 'lift%d.c' % i), 'w') as f:
            f.write('// generated by tools/gen_assets.py: card hop/flip frames\n#pragma bank %d\n'
                    '#include "assets.h"\n\n' % (LIFT_BANK0 + i) + '\n'.join(part))


if __name__ == '__main__':
    main()
