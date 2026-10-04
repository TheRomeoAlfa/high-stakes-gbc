"""Minimal PICO-8 .p8.png cartridge reader (no third-party deps).

Returns the 32K cart ROM image: gfx at 0x0000, map 0x2000, flags 0x3000,
music 0x3100, sfx 0x3200, compressed code 0x4300.
"""
import struct
import zlib


def _png_rgba(path):
    d = open(path, 'rb').read()
    p, idat = 8, b''
    w = h = 0
    while p < len(d):
        ln, = struct.unpack('>I', d[p:p + 4])
        t, c = d[p + 4:p + 8], d[p + 8:p + 8 + ln]
        p += 12 + ln
        if t == b'IHDR':
            w, h = struct.unpack('>II', c[:8])
        elif t == b'IDAT':
            idat += c
    raw = zlib.decompress(idat)
    bpp, stride = 4, w * 4
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[i]
        i += 1
        line = bytearray(raw[i:i + stride])
        i += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        rows.append(line)
        prev = line
    return w, h, rows


def read_cart(path):
    w, h, rows = _png_rgba(path)
    mem = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = rows[y][x * 4:x * 4 + 4]
            mem.append(((a & 3) << 6) | ((r & 3) << 4) | ((g & 3) << 2) | (b & 3))
    return bytes(mem[:0x8000])
