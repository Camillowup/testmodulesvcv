#!/usr/bin/env python3
"""GENRA CHOP FX's panel (two effect buses after the SP-404 idea), in GENRA CHOP's production-center look.

Writes res/ChopFx.svg (all text as paths, Inter) and src/ChopFxLayout.hpp (positions in Rack pixels). The LCD, the
fields and the buttons are drawn by the module. Every printed text is checked against every other.
Run from anywhere: python3 tools/fx_panel.py   (--preview x.png: a picture with the parts drawn in)
"""
import os
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HP = 22
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

# ---------------------------------------------------------------- header: GENRA | CHOP FX PERFORMER, IN, OUT
tw = text(4, 10.2, "GENRA", 8.0, INK, 900, "start", 0.5)
bx = 4 + tw + 2.2
rect(bx, 3.4, 1.2, 7.0, RED, 0.2)
text(bx + 2.6, 6.0, "CHOP FX", 1.8, INK_DIM, 700, "start", 0.35)
text(bx + 2.6, 9.5, "PERFORMER", 1.8, INK_DIM, 700, "start", 0.35)
for name, x, label in (("IN", W - 19.0, "IN"), ("OUT", W - 8.0, "OUT")):
    comp(name, x, 6.6)
    text(x, 13.0, label, 1.4, INK, 700)

# ---------------------------------------------------------------- the LCD, PRESET, ROUTE
rect(3, 15, W - 6, 15, GRAPH, 1.0)
comp("LCD", 4.4, 16.4, W - 8.8, 12.2)
PW = W * 0.58 - 4
rect(3, 32, PW, 5.2, GRAPH, 0.6)
comp("PRESET", 3, 32, PW, 5.2)
rect(W * 0.58 + 0.5, 32, W * 0.42 - 3.5, 5.2, GRAPH, 0.6)
comp("ROUTE", W * 0.58 + 0.5, 32, W * 0.42 - 3.5, 5.2)

# ---------------------------------------------------------------- the two buses
BW = (W - 6 - 2) / 2
for b in range(2):
    x0 = 3 + b * (BW + 2)
    rect(x0, 39.5, BW, 42.5, SECTION, 1.0, EDGE, 0.4)
    rect(x0, 39.5, BW, 5.2, GRAPH, 1.0)
    rect(x0, 42.5, BW, 2.2, GRAPH, 0)
    text(x0 + BW / 2, 43.2, f"BUS {b + 1}", 1.9, CREAM, 700, sp=0.35)
    comp(f"SEL{b}", x0, 39.5, BW, 5.2)  # click: the bus the quick keys and the LCD work on
    rect(x0 + 2, 46.8, BW - 4, 5.2, GRAPH, 0.6)
    comp(f"FX{b}", x0 + 2, 46.8, BW - 4, 5.2)
    for i in range(3):
        kx = x0 + BW / 2 + (i - 1) * 15.2
        comp(f"C{b}_{i}", kx, 59.5)
        text(kx, 67.4, f"CTRL {i + 1}", 1.4, INK, 700)
    comp(f"ON{b}", x0 + 2, 69.6, BW - 4, 6.6)
    text(x0 + BW / 2, 79.8, "TAP = LATCH    HOLD = MOMENTARY", 1.1, INK_DIM, 600, sp=0.12)

# ---------------------------------------------------------------- the quick keys (right-click: which effect)
rect(3, 84, W - 6, 9.4, GRAPH, 1.0)
QW = (W - 6 - 4 - 5 * 0.8) / 6
for i in range(6):
    comp(f"Q{i}", 5 + i * (QW + 0.8), 86.0, QW, 5.4)

# ---------------------------------------------------------------- the jacks
rect(3, 95.5, W - 6, 30.8, GRAPH, 1.4)
J = 9.4
Y1, Y2 = 104.6, 119.2


def jack(name, x, y, label):
    comp(name, x, y)
    text(x, y - 5.7, label, 1.35, CREAM, 700)


def plate(x0, n, y, col=RED):
    w = n * J
    rect(x0 - J / 2 + 0.3, y - 8.2, w - 0.6, 13.0, GRAPH_D, 0.8)
    rect(x0 - J / 2 + 1.0, y - 8.2, w - 2.0, 0.5, col, 0.2)


x = 9.0
for i, (n, lb) in enumerate((("INL_IN", "IN L"), ("INR_IN", "IN R"), ("CLK_IN", "CLK"), ("RST_IN", "RST"))):
    jack(n, x + i * J, Y1, lb)
# kept free for later: the light segments of a connection without cables (GENRA CHOP's sound and clock)
comp("LINK_AUDIO", x - 4.0, 97.0, J + 8.0, 0.9)
comp("LINK_CLK", x + 2 * J - 4.0, 97.0, J + 8.0, 0.9)
xo = W - 9.0 - J
plate(xo, 2, Y1)
jack("OUTL_OUT", xo, Y1, "OUT L")
jack("OUTR_OUT", xo + J, Y1, "OUT R")
for b in range(2):
    xb = 9.0 + b * (5 * J + 2.2)
    plate(xb, 5, Y2, CREAM)
    for i, lb in enumerate(("CV 1", "CV 2", "CV 3", "ON", "FX")):
        jack(f"B{b}_{i}_IN", xb + i * J, Y2, lb)
    text(xb + 2 * J, 125.4, f"BUS {b + 1}", 1.25, "#8f8a7c", 700)

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
jk = [(k, v) for k, v in comps.items() if len(v) == 2 and k.endswith(("_IN", "_OUT"))]
for i in range(len(jk)):
    for j in range(i + 1, len(jk)):
        (n1, (x1, y1)), (n2, (x2, y2)) = jk[i], jk[j]
        if abs(y1 - y2) < 1 and abs(x1 - x2) < 8.6:
            print("jacks too close:", n1, n2)
            sys.exit(1)
if max(v[0] + (v[2] if len(v) == 4 else 4.2) for v in comps.values()) > W - 2.5:
    print("something past the right edge")
    sys.exit(1)

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.2f}mm" height="{H}mm" viewBox="0 0 {W:.2f} {H}">' +
       "".join(out) + "</svg>")
open(os.path.join(ROOT, "res", "ChopFx.svg"), "w").write(svg)
lines = ["// generated by tools/fx_panel.py - do not edit", "#pragma once", "namespace cfx {",
         f"static const float W = {W * PX:.3f}f;"]
for k, v in comps.items():
    vals = ", ".join(f"{a * PX:.3f}f" for a in v)
    lines.append(f"static const float {k}[{len(v)}] = {{{vals}}};")
lines.append("} // namespace cfx")
open(os.path.join(ROOT, "src", "ChopFxLayout.hpp"), "w").write("\n".join(lines) + "\n")
print(f"res/ChopFx.svg: {HP} HP, {len(comps)} parts")

# ---------------------------------------------------------------- preview
if "--preview" in sys.argv:
    out.clear()
    QN = ["FILTER", "RESO", "DELAY", "ISOLATOR", "LOOPER", "STOPPER"]
    for k, v in comps.items():
        if k.startswith("LINK"):
            continue
        if len(v) == 2:
            if k.endswith(("_IN", "_OUT")):
                e(f'<circle cx="{v[0]}" cy="{v[1]}" r="3.9" fill="#1e1d1c" stroke="#c9c4b6" stroke-width="0.6"/>'
                  f'<circle cx="{v[0]}" cy="{v[1]}" r="1.6" fill="#000"/>')
            else:
                r = 4.2 if k.startswith("C") else 3.3
                e(f'<circle cx="{v[0]}" cy="{v[1]}" r="{r}" fill="#2e2d2b"/><circle cx="{v[0]}" cy="{v[1]}" r="{r * 0.86}" fill="{GRAPH}"/>'
                  f'<rect x="{v[0] - 0.3}" y="{v[1] - r * 0.84}" width="0.6" height="{r * 0.62}" rx="0.3" fill="{CREAM}"/>')
            continue
        x, y, w, h = v
        if k == "LCD":
            rect(x, y, w, h, LCD, 0.4)
            text(x + 1.6, y + 4.0, "1 VINYL SIM   ON", 2.1, "#24301a", 700, "start", check=False)
            text(x + 1.6, y + 7.6, "2 TAPE ECHO   1/8D", 2.1, "#24301a", 700, "start", check=False)
            text(x + 1.6, y + 11.0, "NOISE 35%  WOW 20%  AGE 60%", 1.7, "#3a4a2a", 700, "start", check=False)
        elif k in ("PRESET", "ROUTE") or k.startswith("FX"):
            lab = {"PRESET": "PRESET  Dusty Basement", "ROUTE": "ROUTE  1 > 2", "FX0": "VINYL SIM", "FX1": "TAPE ECHO"}[k]
            text(x + 1.5, y + h / 2 + 0.6, lab + "  v", 1.6, CREAM, 700, "start", check=False)
        elif k.startswith("ON"):
            rect(x, y, w, h, RED, 0.8)
            text(x + w / 2, y + h / 2 + 0.6, "FX ON", 1.6, CREAM, 700, check=False)
        elif k.startswith("Q"):
            rect(x, y, w, h, "#55534f", 0.8)
            text(x + w / 2, y + h / 2 + 0.5, QN[int(k[1:])], 1.3, CREAM, 700, check=False)
    prev = svg.replace("</svg>", "".join(out) + "</svg>")
    open("/tmp/fx_preview.svg", "w").write(prev)
    import subprocess
    subprocess.run(["rsvg-convert", "-w", "900", "/tmp/fx_preview.svg", "-o", sys.argv[sys.argv.index("--preview") + 1]], check=True)
