#!/usr/bin/env python3
"""Reference PICO-8 sound synthesizer (approximate) for checking the GB player.

Renders the cart's music patterns / sfx to mono float samples at 22050 Hz using
PICO-8's tracker semantics: notes of speed*183 samples, effects (slide,
vibrato, drop, fade in/out, fast/slow arpeggio), custom instruments (sfx 0-7
played as sub-sfx, transposed relative to C2) and the 8 base waveforms
(formulas after the zepto8 emulator).
"""
import math
import os
import random
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from p8cart import read_cart  # noqa: E402

SR = 22050
TICK = 183
ROM = read_cart(os.path.join(ROOT, 'assets', 'highstakes.p8.png'))
SFX = [ROM[0x3200 + i * 68:0x3200 + i * 68 + 68] for i in range(64)]
MUSIC = [ROM[0x3100 + i * 4:0x3104 + i * 4] for i in range(64)]


def note(s, i):
    lo, hi = s[i * 2], s[i * 2 + 1]
    return dict(pitch=lo & 63, wave=((lo >> 6) | (hi << 2)) & 7, vol=(hi >> 1) & 7,
                fx=(hi >> 4) & 7, cust=hi >> 7)


def freq(p):
    return 440.0 * 2 ** ((p - 33) / 12.0)


_rng = random.Random(1)


def wave(w, t, st):
    t -= math.floor(t)
    if w == 0:
        return 0.5 * (abs(4 * t - 2) - 1)
    if w == 1:
        a = 0.9
        r = 2 * t / a - 1 if t < a else 2 * (1 - t) / (1 - a) - 1
        return r * 0.5
    if w == 2:
        return 0.653 * (t if t < 0.5 else t - 1)
    if w == 3:
        return 0.25 if t < 0.5 else -0.25
    if w == 4:
        return 0.25 if t < 1 / 3 else -0.25
    if w == 5:
        r = 3 - abs(24 * t - 6) if t < 0.5 else 1 - abs(16 * t - 12)
        return r / 9
    if w == 6:
        # brown-ish noise; brightness follows pitch
        st['n'] = st.get('n', 0) * 0.8 + _rng.uniform(-1, 1) * 0.2
        return st['n'] * 0.9
    if w == 7:
        k = abs(2 * ((t * 127 / 128) % 1) - 1)
        u = abs(2 * ((t * 1.007) % 1) - 1)
        return 0.25 * (k + u - 1)
    return 0


class Seq:
    """Plays one sfx (or a custom instrument); yields per-sample (pitch, vol, wave)."""

    def __init__(self, n):
        self.s = SFX[n]
        self.speed = max(1, self.s[65])
        self.ls, self.le = self.s[66], self.s[67]
        self.len = self.speed * TICK
        self.pos = 0          # samples into current note
        self.idx = 0
        self.prev = dict(pitch=0, vol=0)
        self.cur = note(self.s, 0)
        self.done = False

    def step(self):
        self.pos += 1
        if self.pos >= self.len:
            self.pos = 0
            self.prev = self.cur
            self.idx += 1
            if self.ls < self.le and self.idx >= self.le:
                self.idx = self.ls
            if self.idx >= 32:
                self.done = True
                self.idx = 31
                self.cur = dict(pitch=0, wave=0, vol=0, fx=0, cust=0)
                return
            self.cur = note(self.s, self.idx)

    def value(self):
        """(pitch float, vol 0..1, wave, cust, newnote) at the current sample"""
        n = self.cur
        f = self.pos / self.len
        p, v = float(n['pitch']), n['vol'] / 7
        fx = n['fx']
        ticks = self.pos / TICK
        if fx == 1 and self.prev['vol']:
            p = self.prev['pitch'] + (p - self.prev['pitch']) * f
            v = self.prev['vol'] / 7 + (v - self.prev['vol'] / 7) * f
        elif fx == 2:
            p += 0.25 * math.sin(ticks / 4 * 2 * math.pi * 0.5)
        elif fx == 3:
            p = math.log2(max(1e-3, (1 - f))) * 12 + p
        elif fx == 4:
            v *= f
        elif fx == 5:
            v *= 1 - f
        elif fx >= 6:
            sp = (4 if fx == 6 else 8) // (2 if self.speed <= 8 else 1)
            g = self.idx & ~3
            k = int(ticks // sp + (self.idx - g) * self.len / TICK / sp) & 3
            p = float(self.s[(g + k) * 2] & 63)
        return p, v, n['wave'], n['cust'], self.pos == 0


def render_voice(n, nsamp, leader_len=None):
    seq = Seq(n)
    out = []
    phase = 0.0
    st = {}
    inst = None
    for i in range(nsamp):
        p, v, w, c, new = seq.value()
        if c and v > 0:
            if new or inst is None or inst[1] != w:
                inst = (Seq(w), w)
            ip, iv, iw, _, _ = inst[0].value()
            p = p + ip - 24
            v = v * iv
            w = iw
            inst[0].step()
        else:
            inst = None
        f = freq(p)
        phase += f / SR
        out.append(wave(w, phase, st) * v if v > 0 else 0.0)
        seq.step()
        if seq.done and n >= 0 and leader_len is None:
            pass
    return out


def render_pattern(pat, seconds, lvl=3):
    """renders music starting at pattern pat, following loops"""
    total = int(seconds * SR)
    mix = [0.0] * total
    pos = 0
    while pos < total and pat >= 0:
        m = MUSIC[pat]
        chans = []
        for c in range(4):
            b = m[c]
            if b & 0x40:
                continue
            if 1 <= pat <= 8 and c > lvl:
                continue
            chans.append((c, b & 63))
        if not chans:
            break
        # pattern length: first non-looping channel, else the first channel
        lead = None
        for c, n in chans:
            s = SFX[n]
            if not s[66] < s[67]:
                lead = n
                break
        if lead is None:
            lead = chans[0][1]
        plen = 32 * max(1, SFX[lead][65]) * TICK
        plen = min(plen, total - pos)
        for c, n in chans:
            v = render_voice(n, plen)
            for i in range(plen):
                mix[pos + i] += v[i]
        pos += plen
        if m[1] & 0x80:
            while pat > 0 and not (MUSIC[pat][0] & 0x80):
                pat -= 1
        elif m[2] & 0x80:
            pat = -1
        else:
            pat += 1
    return mix


def render_sfx(n, seconds):
    return render_voice(n, int(seconds * SR))


def write_wav(path, samples, sr=SR):
    import struct
    import wave as wv
    peak = max(1e-6, max(abs(x) for x in samples))
    w = wv.open(path, 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(sr)
    w.writeframes(b''.join(struct.pack('<h', int(x / peak * 30000)) for x in samples))
    w.close()


if __name__ == '__main__':
    kind, n, secs, out = sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
    smp = render_pattern(n, secs) if kind == 'music' else render_sfx(n, secs)
    write_wav(out, smp)
