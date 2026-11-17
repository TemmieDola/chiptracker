#!/usr/bin/env python3
"""Generate the 32x32 OPK icon for CHIPTRACKER.

Pure standard library (zlib + struct).  The icon is a tiny teal handheld
with a black tracker screen: three coloured pattern rows and a play mark.
"""

import os
import struct
import zlib

W = H = 32

CASE = (14, 124, 138, 255)
CASE_HI = (47, 168, 182, 255)
CASE_LO = (8, 64, 74, 255)
SCREEN = (5, 16, 18, 255)
GRID = (16, 52, 58, 255)
GREEN = (114, 208, 118, 255)
YELLOW = (233, 194, 75, 255)
PURPLE = (169, 139, 232, 255)
BLUE = (76, 125, 255, 255)
RED = (228, 96, 84, 255)
WHITE = (240, 253, 253, 255)


def rounded_mask(radius):
    mask = [[True] * W for _ in range(H)]
    r2 = radius * radius
    for y in range(H):
        for x in range(W):
            cx = cy = None
            if x < radius and y < radius:
                cx, cy = radius, radius
            elif x >= W - radius and y < radius:
                cx, cy = W - radius - 1, radius
            elif x < radius and y >= H - radius:
                cx, cy = radius, H - radius - 1
            elif x >= W - radius and y >= H - radius:
                cx, cy = W - radius - 1, H - radius - 1
            if cx is not None:
                if (x - cx) ** 2 + (y - cy) ** 2 > r2:
                    mask[y][x] = False
    return mask


def main():
    px = [[(0, 0, 0, 0) for _ in range(W)] for _ in range(H)]
    mask = rounded_mask(6)

    for y in range(H):
        for x in range(W):
            if mask[y][x]:
                px[y][x] = CASE

    # top highlight and bottom shade
    for x in range(8, W - 8):
        px[4][x] = CASE_HI
        px[5][x] = CASE_HI
        px[29][x] = CASE_LO

    # screen area
    for y in range(8, 23):
        for x in range(6, 26):
            px[y][x] = SCREEN
    for x in range(6, 26):
        px[8][x] = GRID
        px[22][x] = GRID
    for y in range(8, 23):
        px[y][6] = GRID
        px[y][25] = GRID

    # pattern rows: note / instrument / effect
    def bar(x, y, w, col):
        for i in range(w):
            px[y][x + i] = col

    bar(9, 10, 4, GREEN)
    bar(14, 10, 2, YELLOW)
    bar(17, 10, 3, PURPLE)

    for x in range(8, 20):
        px[12][x] = GRID

    bar(9, 13, 3, GREEN)
    bar(13, 13, 3, YELLOW)
    bar(17, 13, 3, RED)

    bar(9, 16, 4, PURPLE)
    bar(14, 16, 2, GREEN)

    # play triangle
    for col in range(3):
        for y in range(11 + col, 16 - col):
            px[y][21 + col] = WHITE

    # four face-button dots under the screen
    for i, col in enumerate((RED, YELLOW, BLUE, GREEN)):
        x = 9 + i * 4
        for dy in range(2):
            for dx in range(2):
                px[25 + dy][x + dx] = col

    raw = bytearray()
    for y in range(H):
        raw.append(0)
        for x in range(W):
            raw.extend(px[y][x])

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")

    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "chiptune", "opk", "chiptune.png")
    out = os.path.normpath(out)
    with open(out, "wb") as f:
        f.write(png)
    print("wrote %s (%d bytes)" % (out, len(png)))


if __name__ == "__main__":
    main()
