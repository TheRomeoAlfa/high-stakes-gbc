#!/usr/bin/env python3
"""Fails the build if card hop data read by CGB DMA is not 16-byte aligned (see lift.c)."""
import re
import sys

bad = []
for line in open(sys.argv[1]):
    m = re.match(r'DEF (_liftp?_(?:b|f\d+|shadow)) 0x([0-9A-Fa-f]+)', line)
    if m and int(m.group(2), 16) % 16:
        bad.append(m.group(1))
if bad:
    sys.exit('not 16-byte aligned for DMA: ' + ', '.join(bad))
