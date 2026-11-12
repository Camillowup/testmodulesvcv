#!/usr/bin/env python3
"""GENRA CHOP FX's panel (36 HP): two effect buses after the SP-404 idea, every source in on its own with the bus it
goes to, every channel out on its own (for WING-LINK), the returns, the mix, DJ pads; in GENRA CHOP's look.

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
HP = 36
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
tw = text(4, 10.2, "GENRA", 8.0, INK, 900, "start", 0.5)
bx = 4 + tw + 2.2
rect(bx, 3.4, 1.2, 7.0, RED, 0.2)
text(bx + 2.6, 6.0, "CHOP FX", 1.8, INK_DIM, 700, "start", 0.35)
text(bx + 2.6, 9.5, "PERFORMER", 1.8, INK_DIM, 700, "start", 0.35)
for name, x, label in (("IN", W - 32.0, "IN"), ("FILT", W - 20.0, "DJ FILTER"), ("OUT", W - 8.0, "OUT")):
    comp(name, x, 6.4)
    text(x, 12.7, label, 1.35, INK, 700)
rect(3, 14.6, W * 0.62 - 4, 13.0, GRAPH, 1.0)
comp("LCD", 4.2, 15.8, W * 0.62 - 6.4, 10.6)
rect(W * 0.62 + 0.5, 14.6, W * 0.38 - 3.5, 6.0, GRAPH, 0.6)
comp("PRESET", W * 0.62 + 0.5, 14.6, W * 0.38 - 3.5, 6.0)
rect(W * 0.62 + 0.5, 21.6, W * 0.38 - 3.5, 6.0, GRAPH, 0.6)
comp("ROUTE", W * 0.62 + 0.5, 21.6, W * 0.38 - 3.5, 6.0)

DJW = 36.0
NB = 2
BW = (W - 6 - DJW - 2 * NB) / NB
BY, BH = 29.8, 54.0
J = 9.4
for b in range(NB):
    x0 = 3 + b * (BW + 2)
    rect(x0, BY, BW, BH, SECTION, 1.0, EDGE, 0.4)
    rect(x0, BY, BW, 5.0, GRAPH, 1.0)
    rect(x0, BY + 2.8, BW, 2.2, GRAPH, 0)
    text(x0 + BW / 2, BY + 3.6, f"BUS {b + 1}", 1.8, CREAM, 700, sp=0.35)
    comp(f"FX{b}", x0 + 1.6, BY + 6.6, BW - 3.2, 5.0)
    for i in range(3):
        kx = x0 + BW / 2 + (i - 1) * 17.0
        comp(f"OLED{b}_{i}", kx - 7.2, BY + 13.4, 14.4, 6.2)
        comp(f"C{b}_{i}", kx, BY + 24.4)
        comp(f"CV{b}_{i}_IN", kx, BY + 36.4)
        text(kx, BY + 31.2, f"CV {i + 1}", 1.2, INK, 700)
    comp(f"ON{b}", x0 + 1.6, BY + 43.4, BW - 3.2, 7.4)
dx = W - 3 - DJW
rect(dx, BY, DJW, BH, GRAPH, 1.0)
text(dx + DJW / 2, BY + 3.8, "DJ PADS", 1.6, CREAM, 700, sp=0.35)
PWD, PHD = (DJW - 4.2) / 2, 10.6
for i in range(8):
    comp(f"DJ{i}", dx + 1.6 + (i % 2) * (PWD + 1.0), BY + 6.0 + (i // 2) * (PHD + 1.0), PWD, PHD)

rect(3, 85.6, W - 6, 7.4, GRAPH, 1.0)
QW = (W - 6 - 4 - 7 * 0.8) / 8
for i in range(8):
    comp(f"Q{i}", 5 + i * (QW + 0.8), 86.6, QW, 5.4)

rect(3, 94.6, W - 6, 31.7, GRAPH, 1.4)
Y0, Y1 = 107.2, 121.0


def jack(name, x, y, label):
    comp(name, x, y)
    text(x, y - 5.4, label, 1.2, CREAM, 700)


def plate(x0, n, y, col=RED, top=8.0):
    w = n * J
    rect(x0 - J / 2 + 0.3, y - top, w - 0.6, top + 4.8, GRAPH_D, 0.8)
    rect(x0 - J / 2 + 1.0, y - top, w - 2.0, 0.5, col, 0.2)


SRC = (("IN", 2), ("KICK", 1), ("SNR", 1), ("CH", 1), ("OH", 1), ("BASS", 1), ("MEL", 2), ("PADS", 2))
x = 9.0
plate(x, 11, Y0, YELLOW, 11.4)
plate(x, 11, Y1, RED, 7.4)
for name, n in SRC:
    comp(f"SEL_{name}", x - J / 2 + 1.0, Y0 - 10.8, n * J - 2.0, 3.2)
    if n == 1:
        jack(f"{name}_IN", x, Y0, name)
        jack(f"{name}_OUT", x, Y1, name)
    else:
        jack(f"{name}L_IN", x, Y0, name + " L")
        jack(f"{name}R_IN", x + J, Y0, name + " R")
        jack(f"{name}L_OUT", x, Y1, name + " L")
        jack(f"{name}R_OUT", x + J, Y1, name + " R")
    x += n * J
comp("LINK_AUDIO", 9.0 - 4.0, Y0 - 12.6, J + 8.0, 0.9)
# row 1 right: clock and the buses' ON and FX
xc = 9.0 + 11 * J + 3.0
jack("CLK_IN", xc, Y0, "CLK")
jack("RST_IN", xc + J, Y0, "RST")
for b in range(NB):
    xb = xc + 2 * J + 3.0 + b * (2 * J + 2.0)
    plate(xb, 2, Y0, CREAM)
    jack(f"B{b}_ON_IN", xb, Y0, f"{b + 1}:ON")
    jack(f"B{b}_FX_IN", xb + J, Y0, f"{b + 1}:FX")
# row 2 right: the returns, the mix
xr = 9.0 + 11 * J + 3.0
plate(xr, 4, Y1, RED, 7.4)
for i in range(4):
    jack(f"RET{i}_OUT", xr + i * J, Y1, f"RET{i // 2 + 1} {'LR'[i % 2]}")
xo = W - 9.0 - J
plate(xo, 2, Y1)
jack("OUTL_OUT", xo, Y1, "MIX L")
jack("OUTR_OUT", xo + J, Y1, "MIX R")

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

# ---------------------------------------------------------------- preview (python3 tools/fx_panel.py --preview x.png)
if "--preview" in sys.argv:
    out.clear()
    QN = ["FILTER", "RESO", "DELAY", "ISOLATOR", "LOOPER", "STOPPER", "VINYL", "REVERB"]
    DJN = ["ECHO OUT", "STUTTER 1/4", "STUTTER 1/8", "STUTTER 1/16", "TAPE STOP", "BACKSPIN", "LOW CUT", "HIGH CUT"]
    SELV = {"IN": "1", "KICK": "DRY", "SNR": "2", "CH": "1", "OH": "1", "BASS": "DRY", "MEL": "2", "PADS": "2"}
    for k, v in comps.items():
        if k.startswith("LINK"):
            continue
        if len(v) == 2:
            if k.endswith(("_IN", "_OUT")):
                e(f'<circle cx="{v[0]}" cy="{v[1]}" r="3.9" fill="#1e1d1c" stroke="#c9c4b6" stroke-width="0.6"/><circle cx="{v[0]}" cy="{v[1]}" r="1.6" fill="#000"/>')
            else:
                r = 4.2 if k.startswith("C") else 3.3
                e(f'<circle cx="{v[0]}" cy="{v[1]}" r="{r}" fill="#2e2d2b"/><circle cx="{v[0]}" cy="{v[1]}" r="{r * 0.86}" fill="{GRAPH}"/><rect x="{v[0] - 0.3}" y="{v[1] - r * 0.84}" width="0.6" height="{r * 0.62}" rx="0.3" fill="{CREAM}"/>')
            continue
        x, y, w, h = v
        if k == "LCD":
            rect(x, y, w, h, LCD, 0.4)
            text(x + 1.6, y + 3.4, "1 VINYL SIM   ON      2 TAPE ECHO   1/8D", 1.9, "#24301a", 700, "start", check=False)
            text(x + 1.6, y + 6.6, "IN>1  SNR>2  CH>1  OH>1  MEL>2  PADS>2", 1.7, "#24301a", 700, "start", check=False)
            text(x + 1.6, y + 9.3, "TEMPO 92 BPM (CLK)", 1.6, "#3a4a2a", 700, "start", check=False)
        elif k in ("PRESET", "ROUTE") or k.startswith("FX"):
            lab = {"PRESET": "PRESET  Dusty Basement", "ROUTE": "ROUTE  1 > 2", "FX0": "VINYL SIM", "FX1": "TAPE ECHO", "FX2": "REVERB"}[k]
            rect(x, y, w, h, GRAPH, 0.6)
            text(x + 1.5, y + h / 2 + 0.6, lab + "  v", 1.55, CREAM, 700, "start", check=False)
        elif k.startswith("ON"):
            rect(x, y, w, h, RED, 0.8)
            text(x + w / 2, y + h / 2 + 0.6, "FX ON", 1.6, CREAM, 700, check=False)
        elif k.startswith("Q"):
            rect(x, y, w, h, "#55534f", 0.8)
            text(x + w / 2, y + h / 2 + 0.5, QN[int(k[1:])], 1.2, CREAM, 700, check=False)
        elif k.startswith("OLED"):
            bb, ii = int(k[4]), int(k[6])
            NM = [["NOISE", "WOW", "AGE"], ["TIME", "FEEDBK", "MIX"], ["SIZE", "TONE", "MIX"]][bb][ii]
            VL = [["35%", "20%", "60%"], ["1/8D", "45%", "35%"], ["50%", "50%", "30%"]][bb][ii]
            FR = [[0.35, 0.2, 0.6], [0.65, 0.45, 0.35], [0.5, 0.5, 0.3]][bb][ii]
            rect(x, y, w, h, "#141413", 0.5, "#55534f", 0.3)
            text(x + w / 2, y + 2.15, NM, 1.15, "#e8f4ff", 700, check=False)
            text(x + w / 2, y + 4.1, VL, 1.25, "#9fd3ff", 700, check=False)
            rect(x + 1.0, y + 4.8, (w - 2.0), 0.45, "#2c2c2a", 0.1)
            rect(x + 1.0, y + 4.8, (w - 2.0) * FR, 0.45, "#9fd3ff", 0.1)
        elif k.startswith("DJ"):
            i = int(k[2:])
            rect(x, y, w, h, PAD, 0.9, "#8f8a7c", 0.3)
            text(x + w / 2, y + h / 2 + 0.5, DJN[i], 1.2, INK, 700, check=False)
        elif k.startswith("SEL_"):
            val = SELV[k[4:]]
            rect(x, y, w, h, RED if val != "DRY" else "#55534f", 0.6)
            text(x + w / 2, y + h / 2 + 0.55, val, 1.3, CREAM, 700, check=False)
    prev = svg.replace("</svg>", "".join(out) + "</svg>")
    open("/tmp/fx_preview.svg", "w").write(prev)
    import subprocess
    subprocess.run(["rsvg-convert", "-w", "1100", "/tmp/fx_preview.svg", "-o", sys.argv[sys.argv.index("--preview") + 1]], check=True)
