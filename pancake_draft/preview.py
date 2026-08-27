#!/usr/bin/env python3
"""Render one mood from pancake.grid to a PNG, so you can hand-edit the grid
and see the result immediately - no dependency on anything outside this folder.

Usage:
    python3 preview.py [mood] [out.png]
    python3 preview.py            # renders all four moods to preview_<mood>.png
"""
import sys
from PIL import Image, ImageDraw

WIDTH = 34
HEIGHT = 30
SCALE = 14
MOODS = ["happy", "satisfied", "cranky", "neglected"]
GRID_PATH = "pancake.grid"


def dither_ink(cell, x, y):
    if cell == "#":
        return True
    if cell == "o":
        return (x + y) % 2 == 0
    if cell == "d":
        return x % 2 == 0 and y % 2 == 0
    return False  # '.' and 'w' are both unlit - see the README note below


def parse_grid(path):
    moods = {}
    current = None
    with open(path) as f:
        for raw in f:
            line = raw.rstrip("\n")
            stripped = line.strip()
            if stripped.startswith("[") and stripped.endswith("]"):
                current = stripped[1:-1]
                moods[current] = []
                continue
            if current is None or not stripped:
                continue
            if len(line) != WIDTH:
                print(f"warning: a row under [{current}] is {len(line)} chars, expected {WIDTH}: {line!r}")
                continue
            moods[current].append(line)
    for mood, rows in moods.items():
        if len(rows) != HEIGHT:
            print(f"warning: [{mood}] has {len(rows)} rows, expected {HEIGHT}")
    return moods


def render(rows, out_path):
    img = Image.new("RGB", (WIDTH * SCALE, HEIGHT * SCALE), (235, 235, 235))
    px = img.load()
    for y, row in enumerate(rows):
        for x, cell in enumerate(row):
            if cell == ".":
                continue
            color = (20, 20, 20) if dither_ink(cell, x, y) else (255, 255, 255)
            for dy in range(SCALE):
                for dx in range(SCALE):
                    px[x * SCALE + dx, y * SCALE + dy] = color
    d = ImageDraw.Draw(img)
    for x in range(WIDTH + 1):
        d.line([(x * SCALE, 0), (x * SCALE, HEIGHT * SCALE)], fill=(210, 210, 210))
    for y in range(HEIGHT + 1):
        d.line([(0, y * SCALE), (WIDTH * SCALE, y * SCALE)], fill=(210, 210, 210))
    img.save(out_path)
    print(f"wrote {out_path}")


if __name__ == "__main__":
    moods = parse_grid(GRID_PATH)
    args = sys.argv[1:]
    if not args:
        for name in MOODS:
            if name in moods:
                render(moods[name], f"preview_{name}.png")
    else:
        mood = args[0]
        out = args[1] if len(args) > 1 else f"preview_{mood}.png"
        render(moods[mood], out)
