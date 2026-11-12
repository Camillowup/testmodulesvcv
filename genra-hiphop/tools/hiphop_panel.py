#!/usr/bin/env python3
"""GENRA HIP-HOP's panel in the look of an old production center (cream case, grey pads, Akai red, green LCD).

Writes res/HipHop.svg (all text as paths, Inter), the two knobs res/HHKnob.svg, res/HHKnobSmall.svg and
src/HipHopLayout.hpp (positions in Rack pixels). Every printed text is checked against every other: an overlap stops
the script. Run from anywhere: python3 tools/hiphop_panel.py
"""
import os
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HP = 38
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


# ---------------------------------------------------------------- case
rect(0, 0, W, H, CASE, 0)
rect(0, 0, W, 1.6, CASE_TOP, 0)

# ---------------------------------------------------------------- header: GENRA | HIP-HOP PRODUCTION CENTER, transport
tw = text(5, 10.2, "GENRA", 8.6, INK, 900, "start", 0.5)
bx = 5 + tw + 2.6
rect(bx, 3.3, 1.3, 7.2, RED, 0.2)
text(bx + 2.8, 6.0, "HIP-HOP", 1.9, INK_DIM, 700, "start", 0.35)
text(bx + 2.8, 9.6, "PRODUCTION CENTER", 1.9, INK_DIM, 700, "start", 0.35)
# PLAY and the four header knobs
comp("STOP", 103.6, 3.3, 11.6, 6.6)
comp("PLAY", 116.2, 3.3, 11.6, 6.6)
for name, x, label in (("BPM", 141.5, "BPM"), ("SWING", 155.0, "SWING"), ("VOL", 168.5, "CHOP"), ("BRK", 182.0, "BREAK")):
    comp(name, x, 6.0)
    text(x, 12.6, label, 1.45, INK, 700)

# ---------------------------------------------------------------- left: the LCD, the fields, the pads, CHOP
rect(4, 15, 92, 16.6, GRAPH, 1.2)
comp("LCD", 5.4, 16.4, 89.2, 13.8)
for name, x0, x1 in (("ERA", 4, 25), ("STYLE", 27, 73), ("KEY", 75, 96)):
    rect(x0, 33.2, x1 - x0, 6.2, GRAPH, 0.8)
    comp(name, x0, 33.2, x1 - x0, 6.2)

rect(3, 41.2, 51.5, 50.6, SECTION, 1.2, EDGE)
PAD_SIZE, PAD_GAP = 11.5, 1.0
for r in range(4):
    for c in range(4):
        x = 4.0 + c * (PAD_SIZE + PAD_GAP)
        y = 42.2 + r * (PAD_SIZE + PAD_GAP)
        comp(f"PAD{r * 4 + c}", x, y, PAD_SIZE, PAD_SIZE)

rect(55.5, 41.2, 41.5, 50.6, SECTION, 1.2, EDGE)
text(57.0, 46.6, "CHOP", 2.0, INK, 900, "start", 0.3)
rect(67, 42.4, 29, 6.2, GRAPH, 0.8)
comp("MODE", 67, 42.4, 29, 6.2)
for name, x, label in (("MUTATE", 62.6, "MUTATE"), ("PITCH", 76.25, "PITCH"), ("DUST", 89.9, "DUST")):
    comp(name, x, 56.4)
    text(x, 64.1, label, 1.45, INK, 700)
BW, BG = 9.4, 0.8
for i, name in enumerate(("LOAD", "NEW", "REC", "LOCK")):
    comp(name, 56.2 + i * (BW + BG), 66.4, BW, 6.6)
for i, name in enumerate(("ROOT", "UNDO", "SAVE")):
    comp(name, 56.2 + i * (12.8 + 0.8), 74.8, 12.8, 6.6)
comp("NEWALL", 56.2, 83.6, 39.8, 6.6)

# ---------------------------------------------------------------- right: the waveform, the four tracks
rect(99, 15, 90, 24.4, GRAPH, 1.2)
comp("WAVE", 100.2, 16.2, 87.6, 22.0)
TRACKS = (("DRUMS", "DENS", "CHOP"), ("BASS", "DENS", "OCT"), ("MELODY", "DENS", "CHOP"), ("PADS", "DENS", "CHOP"))
CW, CG = 21.6, 1.2
for t, (name, k1, k2) in enumerate(TRACKS):
    x = 99 + t * (CW + CG)
    rect(x, 41.2, CW, 43.6, SECTION, 1.2, EDGE)
    rect(x, 41.2, CW, 5.6, GRAPH, 1.2)
    rect(x, 44.0, CW, 2.8, GRAPH, 0)
    text(x + CW / 2, 45.2, name, 1.75, CREAM, 900, "middle", 0.25)
    rect(x + 1.4, 48.6, CW - 2.8, 6.0, GRAPH, 0.8)
    comp(f"TMODE{t}", x + 1.4, 48.6, CW - 2.8, 6.0)
    comp(f"DENS{t}", x + 5.6, 62.6)
    comp(f"TK2_{t}", x + 16.0, 62.6)
    text(x + 5.6, 70.4, k1, 1.45, INK, 700)
    text(x + 16.0, 70.4, k2, 1.45, RED if k2 == "CHOP" else INK, 700)
    comp(f"TNEW{t}", x + 3.5, 75.6, CW - 7.0, 6.6)
comp("BARS", 99, 86.8, 90, 3.6)

# ---------------------------------------------------------------- the jacks
rect(3, 95.0, W - 6, 31.3, GRAPH, 1.4)
J = 9.4
Y1, Y2 = 104.4, 119.0


def jack(name, x, y, label, plate=False):
    comp(name, x, y)
    text(x, y - 5.7, label, 1.4, CREAM if not plate else CREAM, 700)


def plate(x0, n, y, title=None):
    w = n * J
    rect(x0 - J / 2 + 0.3, y - 8.2, w - 0.6, 13.0, GRAPH_D, 0.8)
    rect(x0 - J / 2 + 1.0, y - 8.2, w - 2.0, 0.5, RED, 0.2)


x = 9.6
for i, (n, lb) in enumerate((("CLK_IN", "CLK"), ("RST_IN", "RST"), ("RUN_IN", "RUN"), ("NEW_IN", "NEW"))):
    jack(n, x + i * J, Y1, lb)
x = 52.0
plate(x, 8, Y1)
for i, lb in enumerate(("KICK", "SNR", "CLAP", "RIM", "CH", "OH", "TOM", "PERC")):
    jack(f"DRUM{i}", x + i * J, Y1, lb, True)
x = 134.0
plate(x, 2, Y1)
jack("BRK_L", x, Y1, "BRK L", True)
jack("BRK_R", x + J, Y1, "BRK R", True)
x = 160.0
plate(x, 3, Y1)
jack("CLK_OUT", x, Y1, "CLK", True)
jack("RST_OUT", x + J, Y1, "RST", True)
jack("BAR_OUT", x + 2 * J, Y1, "BAR", True)

x = 9.6
plate(x, 5, Y2)
for i, lb in enumerate(("V/OCT", "GATE", "VEL", "ACC", "SLIDE")):
    jack(f"BASS{i}", x + i * J, Y2, lb, True)
x = 9.6 + 5 * J + 3.0
plate(x, 5, Y2)
for i, lb in enumerate(("V/OCT", "GATE", "VEL", "ACC", "SLIDE")):
    jack(f"MEL{i}", x + i * J, Y2, lb, True)
x = 9.6 + 10 * J + 6.0
plate(x, 4, Y2)
for i, lb in enumerate(("V/OCT", "GATE", "VEL", "MOD")):
    jack(f"PADO{i}", x + i * J, Y2, lb, True)
x = W - 9.6 - J
plate(x, 2, Y2)
jack("CHOP_L", x, Y2, "CHOP L", True)
jack("CHOP_R", x + J, Y2, "CHOP R", True)
# group names in the gap between the rows (small, dim)
for gx, gn in ((9.6 + 2 * J, "BASS"), (9.6 + 7 * J + 3.0, "MELODY"), (9.6 + 11.5 * J + 6.0, "PADS")):
    text(gx, 125.3, gn, 1.3, "#8f8a7c", 700)
text(52.0 + 3.5 * J, 110.3, "DRUMS", 1.3, "#8f8a7c", 700)

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
# jacks must not touch each other (8.4 mm wide)
jk = [(k, v) for k, v in comps.items() if len(v) == 2 and (k.endswith(("_IN", "_OUT", "_L", "_R")) or k[:-1] in
      ("DRUM", "BASS", "MEL", "PADO"))]
for i in range(len(jk)):
    for j in range(i + 1, len(jk)):
        (n1, (x1, y1)), (n2, (x2, y2)) = jk[i], jk[j]
        if abs(y1 - y2) < 1 and abs(x1 - x2) < 8.6:
            print("jacks too close:", n1, n2)
            sys.exit(1)
if max(v[0] for v in comps.values()) > W - 4:
    print("something past the right edge")
    sys.exit(1)

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.2f}mm" height="{H}mm" viewBox="0 0 {W:.2f} {H}">' +
       "".join(out) + "</svg>")
os.makedirs(os.path.join(ROOT, "res"), exist_ok=True)
open(os.path.join(ROOT, "res", "HipHop.svg"), "w").write(svg)


def knob(path, d):
    r = d / 2
    s = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{d}mm" height="{d}mm" viewBox="0 0 {d} {d}">'
         f'<circle cx="{r}" cy="{r}" r="{r}" fill="#2e2d2b"/>'
         f'<circle cx="{r}" cy="{r}" r="{r * 0.86:.3f}" fill="{GRAPH}"/>'
         f'<rect x="{r - 0.3:.3f}" y="{d * 0.08:.3f}" width="0.6" height="{r * 0.62:.3f}" rx="0.3" fill="{CREAM}"/></svg>')
    open(os.path.join(ROOT, "res", path), "w").write(s)


knob("HHKnob.svg", 8.4)
knob("HHKnobSmall.svg", 6.6)

# ---------------------------------------------------------------- layout header (Rack px)
lines = ["// generated by tools/hiphop_panel.py - do not edit", "#pragma once", "namespace hhl {",
         f"static const float W = {W * PX:.3f}f;"]
for k, v in comps.items():
    vals = ", ".join(f"{a * PX:.3f}f" for a in v)
    lines.append(f"static const float {k}[{len(v)}] = {{{vals}}};")
lines.append("} // namespace hhl")
open(os.path.join(ROOT, "src", "HipHopLayout.hpp"), "w").write("\n".join(lines) + "\n")
print(f"res/HipHop.svg: {HP} HP, {len(comps)} parts")

# ---------------------------------------------------------------- preview (python3 tools/hiphop_panel.py --preview x.png)
if "--preview" in sys.argv:
    out.clear()
    LABELS = {"LOAD": "LOAD", "NEW": "NEW", "REC": "REC", "LOCK": "LOCK", "ROOT": "ROOT", "UNDO": "UNDO", "SAVE": "SAVE",
              "NEWALL": "NEW ALL", "PLAY": "PLAY", "STOP": "STOP"}
    CHORDS = ["F#m7", "Bm9", "Dmaj", "C#7", "F#m", "Am7", "E", "Bm"]
    for k, v in comps.items():
        if len(v) == 2:
            big = k in ("MUTATE", "PITCH", "DUST") or k.startswith(("DENS", "TK2_"))
            hdr = k in ("BPM", "SWING", "VOL", "BRK")
            if big or hdr:
                r = 4.2 if big else 3.3
                e(f'<circle cx="{v[0]}" cy="{v[1]}" r="{r}" fill="#2e2d2b"/><circle cx="{v[0]}" cy="{v[1]}" r="{r*0.86}" fill="{GRAPH}"/>'
                  f'<rect x="{v[0]-0.3}" y="{v[1]-r*0.84}" width="0.6" height="{r*0.62}" fill="{CREAM}"/>')
            else:
                e(f'<circle cx="{v[0]}" cy="{v[1]}" r="3.9" fill="#1e1d1c" stroke="#c9c4b6" stroke-width="0.6"/><circle cx="{v[0]}" cy="{v[1]}" r="1.6" fill="#000"/>')
        else:
            x, y, w, h = v
            if k.startswith("PAD"):
                i = int(k[3:])
                e(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="1.2" fill="{PAD}" stroke="#8f8a7c" stroke-width="0.4"/>')
                text(x + w / 2, y + 3.0, str(i + 1), 1.5, INK_DIM, 700, check=False)
                if i < 8:
                    text(x + w / 2, y + h / 2 + 1.6, CHORDS[i], 2.1, INK, 700, check=False)
            elif k in LABELS:
                col = RED if k in ("NEW", "REC", "NEWALL", "PLAY") else GRAPH
                e(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="0.8" fill="{col}"/>')
                text(x + w / 2, y + h / 2 + 0.7, LABELS[k], 1.7, CREAM, 700, check=False)
            elif k.startswith("TNEW"):
                e(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="0.8" fill="{RED}"/>')
                text(x + w / 2, y + h / 2 + 0.7, "NEW", 1.7, CREAM, 700, check=False)
            elif k == "LCD":
                e(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="0.4" fill="{LCD}"/>')
                text(x + 2, y + 4.4, "90s > EAST COAST BOOM BAP", 2.4, "#24301a", 700, "start", check=False)
                text(x + 2, y + 8.4, "92 BPM   F#m   SWING 58%", 2.2, "#24301a", 700, "start", check=False)
                text(x + 2, y + 12.0, "SMPL soul_loop_03   KEY:AUTO", 1.9, "#3a4a2a", 700, "start", check=False)
            elif k in ("ERA", "STYLE", "KEY", "MODE") or k.startswith("TMODE"):
                lab = {"ERA": "ERA 90s", "STYLE": "STYLE  East Coast", "KEY": "KEY Auto F#m", "MODE": "Transient"}.get(k)
                if lab is None:
                    lab = ["KIT", "FOLLOW", "STAB", "SUSTAIN"][int(k[-1])]
                text(x + 1.5, y + h / 2 + 0.7, lab + " v", 1.7, CREAM, 700, "start", check=False)
            elif k == "WAVE":
                import math
                pts = " ".join(f"{x + i * w / 200:.2f},{y + h / 2 + math.sin(i * 0.7) * math.sin(i * 0.05) * h * 0.4:.2f}" for i in range(200))
                e(f'<polyline points="{pts}" fill="none" stroke="{LCD}" stroke-width="0.25"/>')
                for i in range(1, 16):
                    e(f'<rect x="{x + i * w / 16}" y="{y}" width="0.25" height="{h}" fill="{RED}"/>')
            elif k == "BARS":
                for i in range(4):
                    e(f'<rect x="{x + i * w / 4 + 0.4}" y="{y}" width="{w / 4 - 0.8}" height="{h}" rx="0.6" fill="{RED if i == 1 else GRAPH}"/>')
    prev = svg.replace("</svg>", "".join(out) + "</svg>")
    open("/tmp/hh_preview.svg", "w").write(prev)
    import subprocess
    subprocess.run(["rsvg-convert", "-w", "1500", "/tmp/hh_preview.svg", "-o", sys.argv[sys.argv.index("--preview") + 1]], check=True)
