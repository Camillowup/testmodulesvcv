#!/usr/bin/env python3
"""GENRA CHOP GRID's panel (the grid editor for GENRA CHOP) in the look of an old production center (cream case, grey pads, Akai red, green LCD).

Writes res/ChopGrid.svg (all text as paths, Inter) and src/ChopGridLayout.hpp (positions in Rack pixels). Every printed text is checked against every other: an overlap stops
the script. Run from anywhere: python3 tools/chop_panel.py
"""
import os
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HP = 44
W = HP * 5.08
H = 128.5
PX = 75 / 25.4

CASE, CASE_TOP, SECTION, EDGE = "#d8d1bf", "#cbc4b1", "#cdc6b3", "#a9a290"
GRAPH, GRAPH_D, CREAM, RED, PAD, LCD = "#3b3a38", "#2a2928", "#ece7da", "#c8312b", "#b9b4a6", "#a9b98c"
INK, INK_DIM = "#3b3a38", "#77736a"

FONT_DIR = "/usr/share/fonts/opentype/inter/"
FONTS = {700: "Inter-Bold.otf", 900: "Inter-Black.otf", 600: "Inter-SemiBold.otf"}
_f = {}


def font(w):
    if w not in _f:
        f = TTFont(FONT_DIR + FONTS[w])
        _f[w] = (f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm, f["hmtx"])
    return _f[w]


def twidth(s, size, w, sp):
    gs, cmap, upm, hmtx = font(w)
    return sum(hmtx[cmap.get(ord(c), "space")][0] * size / upm + sp for c in s) - sp


out = []
boxes = []
comps = {}


def e(s):
    out.append(s)


def text(x, y, s, size=1.6, color=INK, w=700, anchor="middle", sp=0.15, check=True):
    gs, cmap, upm, hmtx = font(w)
    sc = size / upm
    tw = twidth(s, size, w, sp)
    cx = x - (tw / 2 if anchor == "middle" else (tw if anchor == "end" else 0))
    if check:
        boxes.append((cx, y - size * 0.75, cx + tw, y + 0.05, s))
    paths = []
    for c in s:
        g = cmap.get(ord(c), "space")
        pen = SVGPathPen(gs)
        gs[g].draw(TransformPen(pen, (sc, 0, 0, -sc, cx, y)))
        d = pen.getCommands()
        if d:
            paths.append(d)
        cx += hmtx[g][0] * sc + sp
    if paths:
        e(f'<path fill="{color}" d="{" ".join(paths)}"/>')
    return tw


def rect(x, y, w, h, fill, rx=1.0, stroke=None, sw=0.3):
    st = f' stroke="{stroke}" stroke-width="{sw}"' if stroke else ""
    e(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" rx="{rx}" fill="{fill}"{st}/>')


def comp(name, x, y, *extra):
    comps[name] = (x, y) + extra



TRACK_COLORS = ("#8a8fb8", "#9fbf5a", "#5fb7b0", "#d58aa6", "#d9563f")  # drums, bass, melody, pads, chop

rect(0, 0, W, H, CASE, 0)
rect(0, 0, W, 1.6, CASE_TOP, 0)
tw = text(5, 10.2, "GENRA", 8.6, INK, 900, "start", 0.5)
bx = 5 + tw + 2.6
rect(bx, 3.3, 1.3, 7.2, RED, 0.2)
text(bx + 2.8, 6.0, "CHOP", 1.9, INK_DIM, 700, "start", 0.35)
text(bx + 2.8, 9.6, "GRID EDITOR", 1.9, INK_DIM, 700, "start", 0.35)
comp("LINK", W - 56.0, 7.0)
text(W - 53.4, 7.7, "LINK", 1.7, INK, 700, "start", 0.3)
for i, c in enumerate(TRACK_COLORS):
    rect(W - 41.5 + i * 7.6, 6.2, 6.0, 1.6, c, 0.8)

# the grid (one widget: header line, chord strip, keys, raster, VEL/CHANCE lane)
rect(4, 13.6, W - 8, 79.0, GRAPH, 1.4)
comp("GRID", 5.2, 14.8, W - 10.4, 76.6)

# the four sections
GAP = 3.0
BW = 9.0
secs = []
x = 4.0
w_track = 2 + 5 * BW + 4 * 0.8 + 2
w_view = 2 + 33 + 10 + 2
w_notes = 2 + 33 + 8 + 1.5 + 9 + 2
EW = 7.3
w_edit = 2 + 6 * EW + 5 * 0.8 + 2
total = w_track + w_view + w_notes + w_edit
GAP = (W - 8 - total) / 3
SY = 95.2
for name, w in (("TRACK", w_track), ("VIEW", w_view), ("NOTES", w_notes), ("EDIT", w_edit)):
    rect(x, SY, w, 30.0, SECTION, 1.2, EDGE)
    text(x + w / 2, SY + 4.4, name, 1.9, INK, 900, "middle", 0.4)
    secs.append(x)
    x += w + GAP
tx, vx, nx, ex = secs
for i, name in enumerate(("DRUMS", "BASS", "MEL", "PADS", "CHOP")):
    bxx = tx + 2 + i * (BW + 0.8)
    rect(bxx + 1, SY + 9.8, BW - 2, 1.0, TRACK_COLORS[i], 0.4)
    comp(f"TRK{i}", bxx, SY + 11.8, BW, 6.4)
for i, name in enumerate(("BAR", "ZOOM", "PITCH")):
    kx = vx + 7.5 + i * 11
    comp(name, kx, SY + 12.8)
    text(kx, SY + 20.0, name, 1.4, INK, 700)
ax = vx + 2 + 33
comp("AUTO", ax, SY + 9.8, 10.0, 6.4)
text(ax + 5.0, SY + 22.3, "SCROLL", 1.4, INK, 700)
for i, name in enumerate(("VEL", "LEN", "CHANCE")):
    kx = nx + 7.5 + i * 11
    comp(name, kx, SY + 12.8)
    text(kx, SY + 20.0, name, 1.4, INK, 700)
sx = nx + 2 + 33
comp("SNAP", sx, SY + 9.8, 8.0, 6.4)
comp("SNAPMODE", sx - 1.0, SY + 17.2, 10.0, 3.0)
fx = sx + 8.0 + 1.5
comp("FOLLOW", fx, SY + 9.8, 9.0, 6.4)
text(fx + 4.5, SY + 20.0, "CHORDS", 1.35, INK, 700)
for i, (name, sub) in enumerate((("UNDO", ""), ("REDO", ""), ("COPY", "BAR"), ("PASTE", "BAR"), ("CLEAR", "TRACK"), ("GEN", "TRACK"))):
    bxx = ex + 2 + i * (EW + 0.8)
    comp(name, bxx, SY + 9.3, EW, 6.4)
    if sub:
        text(bxx + EW / 2, SY + 19.2, sub, 1.3, INK, 700)
comp("REC", ex + 2, SY + 21.8, 2 * EW + 0.8, 6.4)

bad = []
for i in range(len(boxes)):
    for j in range(i + 1, len(boxes)):
        a, b = boxes[i], boxes[j]
        if a[0] < b[2] - 0.05 and b[0] < a[2] - 0.05 and a[1] < b[3] - 0.05 and b[1] < a[3] - 0.05:
            bad.append((a[4], b[4]))
if bad:
    print("texts overlap:", bad)
    sys.exit(1)
if x - GAP > W - 3.9:
    print("sections past the edge", x - GAP, W)
    sys.exit(1)

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.2f}mm" height="{H}mm" viewBox="0 0 {W:.2f} {H}">' +
       "".join(out) + "</svg>")
open(os.path.join(ROOT, "res", "ChopGrid.svg"), "w").write(svg)
lines = ["// generated by tools/grid_panel.py - do not edit", "#pragma once", "namespace ghl {",
         f"static const float W = {W * PX:.3f}f;"]
for k, v in comps.items():
    vals = ", ".join(f"{a * PX:.3f}f" for a in v)
    lines.append(f"static const float {k}[{len(v)}] = {{{vals}}};")
lines.append("} // namespace ghl")
open(os.path.join(ROOT, "src", "ChopGridLayout.hpp"), "w").write("\n".join(lines) + "\n")
print(f"res/ChopGrid.svg: {HP} HP, {len(comps)} parts")
