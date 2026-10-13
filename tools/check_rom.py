#!/usr/bin/env python3
"""Post-link checks the linker doesn't make:
- bank 0 must end below 0x4000 (an overflow links silently into bank 1's address range);
- card hop data read by CGB DMA must be 16-byte aligned (lift.c), ring frames start their banks (scenes.c);
- RAM variables must leave the stack (growing down from 0xE000) at least 768 bytes.
Usage: check_rom.py build/highstakes.map build/highstakes.noi"""
import re
import sys

errs = []
mp = open(sys.argv[1]).read()
start = int(re.search(r'([0-9A-F]{8})  s__GSFINA', mp).group(1), 16)
length = int(re.search(r'([0-9A-F]{8})  l__GSFINA', mp).group(1), 16)
if start + length > 0x4000:
    errs.append('bank 0 overflows: ends at 0x%04X' % (start + length))
heap = int(re.search(r'([0-9A-F]{8})  s__HEAP ', mp).group(1), 16)
if 0xE000 - heap < 768:
    errs.append('RAM variables end at 0x%04X: only %d bytes left for the stack' % (heap, 0xE000 - heap))
bad = []
for line in open(sys.argv[2]):
    m = re.match(r'DEF (_liftp?_(?:b|f\d+|shadow|z)) 0x([0-9A-Fa-f]+)', line)
    if m and int(m.group(2), 16) % 16:
        bad.append(m.group(1))
    m = re.match(r'DEF (_ring_b\d+) 0x([0-9A-Fa-f]+)', line)   # scenes.c addresses frames from 0x4000
    if m and int(m.group(2), 16) & 0xFFFF != 0x4000:
        bad.append(m.group(1))
if bad:
    errs.append('not 16-byte aligned for DMA: ' + ', '.join(bad))
if errs:
    sys.exit('\n'.join(errs))
print('bank 0 ends at 0x%04X (%d bytes free), stack room %d bytes' % (start + length, 0x4000 - start - length, 0xE000 - heap))
