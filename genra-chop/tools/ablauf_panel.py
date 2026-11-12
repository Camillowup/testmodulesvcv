#!/usr/bin/env python3
"""GENRA CHOP ABLAUF's panel (the song arranger for GENRA CHOP), in GENRA CHOP's production-center look.

Writes res/ChopAblauf.svg (all text as paths, Inter) and src/ChopAblaufLayout.hpp (positions in Rack pixels). The pads,
buttons, the song list and the fields are drawn by the module. Run from anywhere: python3 tools/ablauf_panel.py
"""
import os
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HP = 26
W = HP * 5.08
H = 128.5
PX = 75 / 25.4

CASE, CASE_TOP, SECTION, EDGE = "#d8d1bf", "#cbc4b1", "#cdc6b3", "#a9a290"
GRAPH, GRAPH_D, CREAM, RED, PAD, LCD = "#3b3a38", "#2a2928", "#ece7da", "#c8312b", "#b9b4a6", "#a9b98c"
YELLOW = "#d6a640"  # LOAD SAMPLE
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


# ---------------------------------------------------------------- case
rect(0, 0, W, H, CASE, 0)
rect(0, 0, W, 1.6, CASE_TOP, 0)

# ---------------------------------------------------------------- header: GENRA | CHOP SONG ARRANGER, transport
tw = text(5, 10.2, "GENRA", 8.6, INK, 900, "start", 0.5)
bx = 5 + tw + 2.6
rect(bx, 3.3, 1.3, 7.2, RED, 0.2)
text(bx + 2.8, 6.0, "CHOP", 1.9, INK_DIM, 700, "start", 0.35)
text(bx + 2.8, 9.6, "SONG ARRANGER", 1.9, INK_DIM, 700, "start", 0.35)
comp("STOP", W - 30.0, 3.0, 12.0, 5.6)
comp("PLAY", W - 17.0, 3.0, 12.0, 5.6)
comp("LINK", W - 30.0, 11.2)
text(W - 28.4, 11.8, "LINK", 1.4, INK, 700, "start", 0.3)

# ---------------------------------------------------------------- the patterns: 4 x 4 pads, banks A-D
PX0, PW = 4.0, 62.0
rect(PX0, 14, PW, 82, GRAPH, 1.2)
text(PX0 + 3, 19.4, "PATTERNS", 1.7, CREAM, 700, "start", 0.3)
for i in range(4):
    comp(f"BANK{i}", PX0 + PW - 2 - 4 * 7 + i * 7, 15.8, 6.2, 5.0)
PAD_SIZE, PAD_GAP = 13.6, 1.1
for r in range(4):
    for c in range(4):
        comp(f"PAD{r * 4 + c}", PX0 + 2.3 + c * (PAD_SIZE + PAD_GAP), 23.0 + r * (PAD_SIZE + PAD_GAP), PAD_SIZE, PAD_SIZE)
comp("PADINFO", PX0 + 2.3, 81.2, 4 * PAD_SIZE + 3 * PAD_GAP, 3.4)
# CLEAR (then a pad: that pad empty) | CLEAR ALL (twice: all 64 pads empty); VARIATION RENEWS is gone
CW2 = (4 * PAD_SIZE + 3 * PAD_GAP - 0.8) / 2
comp("CLEAR", PX0 + 2.3, 86.4, CW2, 6.6)
comp("CLEARALL", PX0 + 2.3 + CW2 + 0.8, 86.4, CW2, 6.6)

# ---------------------------------------------------------------- the song
SX = 69.0
SW = W - SX - 4
rect(SX, 14, SW, 50, GRAPH, 1.2)
text(SX + 3, 19.4, "SONG", 1.7, CREAM, 700, "start", 0.3)
comp("MODE", SX + SW - 22.5, 15.8, 20.5, 5.0)
comp("LIST", SX + 2, 23.0, SW - 4, 22.0)
B4 = (SW - 4 - 3 * 0.8) / 4
for i, n in enumerate(("INS", "DEL", "UNDO", "MIDI")):
    comp(n, SX + 2 + i * (B4 + 0.8), 47.0, B4, 5.4)
comp("FORM", SX + 2, 54.6, (SW - 4) * 0.48, 6.4)
comp("MAKE", SX + 2 + (SW - 4) * 0.48 + 0.8, 54.6, (SW - 4) * 0.52 - 0.8, 6.4)

# ---------------------------------------------------------------- the parts
rect(SX, 67, SW, 29, GRAPH, 1.2)
text(SX + 3, 72.0, "VARIATION", 1.5, CREAM, 700, "start", 0.3)
S2 = (SW - 4 - 0.8) / 2
for i in range(6):
    comp(f"PART{i}", SX + 2 + (i % 2) * (S2 + 0.8), 74.0 + (i // 2) * 7.2, S2, 6.2)

# ---------------------------------------------------------------- the jacks
rect(4, 99, W - 8, 26, GRAPH, 1.4)
X0, SP = 11.5, (W - 23) / 8
LABS = (("ROWCV_IN", "ROW CV"), ("RST_IN", "RESET"), ("RUN_IN", "RUN"), ("NEXT_IN", "NEXT"), ("PREV_IN", "PREV"),
        ("PADCV_IN", "PAD CV"), ("END_OUT", "END"), ("BAR_OUT", "BAR"), ("ROW_OUT", "ROW"))
for i, (n, lb) in enumerate(LABS):
    comp(n, X0 + i * SP, 113.0)
    text(X0 + i * SP, 107.4, lb, 1.4, CREAM, 700)
for k, (lab, col) in enumerate((("IN", CREAM), ("IN", "#9a958a"), ("OUT", RED))):
    xa = X0 + 3 * k * SP - 4.5
    xb = X0 + (3 * k + 2) * SP + 4.5
    rect(xa, 119.6, xb - xa, 0.5, col, 0.2)
    text((xa + xb) / 2, 123.2, lab, 1.2, "#9a958a", 700)

# ---------------------------------------------------------------- checks
bad = []
for i in range(len(boxes)):
    for j in range(i + 1, len(boxes)):
        a, b = boxes[i], boxes[j]
        if a[0] < b[2] - 0.05 and b[0] < a[2] - 0.05 and a[1] < b[3] - 0.05 and b[1] < a[3] - 0.05:
            bad.append((a[4], b[4]))
if bad:
    print("texts overlap:", bad)
    sys.exit(1)

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.2f}mm" height="{H}mm" viewBox="0 0 {W:.2f} {H}">' +
       "".join(out) + "</svg>")
open(os.path.join(ROOT, "res", "ChopAblauf.svg"), "w").write(svg)
lines = ["// generated by tools/ablauf_panel.py - do not edit", "#pragma once", "namespace cal {",
         f"static const float W = {W * PX:.3f}f;"]
for k, v in comps.items():
    vals = ", ".join(f"{a * PX:.3f}f" for a in v)
    lines.append(f"static const float {k}[{len(v)}] = {{{vals}}};")
lines.append("} // namespace cal")
open(os.path.join(ROOT, "src", "ChopAblaufLayout.hpp"), "w").write("\n".join(lines) + "\n")
print(f"res/ChopAblauf.svg: {HP} HP, {len(comps)} parts")
