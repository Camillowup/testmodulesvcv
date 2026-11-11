#!/usr/bin/env python3
"""Panel of LIED, a text singer on eSpeak NG, in the GENRA look (accent: coral).

Usage:  lied_panel.py real                   -> res/Lied.svg (text as paths) + src/lied/LiedLayout.hpp
        lied_panel.py preview OUT.png [RACK]   -> preview with Rack components and mock display / lyrics
                                                  (RACK: a Rack source tree, for res/ComponentLibrary)

Coordinates in mm (25 HP, height 128.5 mm), the frame of GENRA's FUNKE. The C++ side converts with mm2px().
The panel holds what never changes; the module draws the display, the lyrics and the text in the fields.
"""
import math
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from panellib import genra_strip  # noqa: E402

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

MODE = sys.argv[1] if len(sys.argv) > 1 else "real"
PREVIEW = MODE == "preview"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RACK = sys.argv[3] if len(sys.argv) > 3 else os.path.join(ROOT, "..", "ext", "Rack")
CL = os.path.join(RACK, "res", "ComponentLibrary")
KNOBS = os.path.join(ROOT, "res", "knob")

HP = 25
W, H = HP * 5.08, 128.5
BG, PLATE, LINE, TXT, DIM, DARK = "#17181d", "#212329", "#34363f", "#d9dbe2", "#8a8d99", "#07080a"
TITLE, RING = "#e6e7ec", "#4a4c57"
ACC = "#ff7f6e"   # coral; keep in step with src/lied/Lied.cpp (LIED_RGB)

R = {"small": 3.84, "knob": 4.8, "large": 6.1, "jack": 4.0, "button": 3.6}
KIND = {"small": "small", "knob": "medium", "large": "large"}

FONT_DIR = "/usr/share/fonts/opentype/inter/"
FONTS = {400: "Inter-Regular.otf", 500: "Inter-Medium.otf", 600: "Inter-SemiBold.otf", 700: "Inter-Bold.otf",
         800: "Inter-ExtraBold.otf"}
_fonts = {}
out, comps, extra, fields = [], [], {}, {}


def font(weight):
    if weight not in _fonts:
        f = TTFont(FONT_DIR + FONTS[weight])
        _fonts[weight] = (f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm, f["hmtx"])
    return _fonts[weight]


def e(s):
    out.append(s)


def text(x, y, s, size=1.8, color=DIM, weight=600, anchor="middle", sp=0.25):
    gs, cmap, upm, hmtx = font(weight)
    scale = size / upm
    glyphs = [cmap.get(ord(c), "space") for c in s]
    w = sum(hmtx[g][0] * scale + sp for g in glyphs) - sp
    cx = x - (w / 2 if anchor == "middle" else (w if anchor == "end" else 0))
    paths = []
    for g in glyphs:
        pen = SVGPathPen(gs)
        gs[g].draw(TransformPen(pen, (scale, 0, 0, -scale, cx, y)))
        if pen.getCommands():
            paths.append(pen.getCommands())
        cx += hmtx[g][0] * scale + sp
    if paths:
        e(f'<path fill="{color}" d="{" ".join(paths)}"/>')
    return w


def width(s, size, weight, sp):
    gs, cmap, upm, hmtx = font(weight)
    return sum(hmtx[cmap.get(ord(c), "space")][0] * size / upm + sp for c in s) - sp


def tint(f, base=PLATE):
    c = [int(ACC[i:i + 2], 16) for i in (1, 3, 5)]
    p = [int(base[i:i + 2], 16) for i in (1, 3, 5)]
    return "#" + "".join(f"{round(p[i] + (c[i] - p[i]) * f):02x}" for i in range(3))


def section(x, y, w, h, title):
    e(f'<rect x="{x}" y="{y}" width="{w:.2f}" height="{h}" rx="1.2" fill="{PLATE}"/>')
    e(f'<rect x="{x}" y="{y}" width="{w:.2f}" height="0.88" rx="0.44" fill="{ACC}"/>')
    text(x + w / 2, y + 5.0, title, 2.03, ACC, 800, "middle", 0.75)


def knob(name, x, y, label, kind="knob", hot=False):
    comps.append((kind, name, x, y))
    r = R[kind] + 0.88
    e(f'<circle cx="{x}" cy="{y}" r="{r:.2f}" fill="none" stroke="{ACC if hot else RING}" stroke-width="0.29" '
      f'stroke-dasharray="0.29 0.73" stroke-linecap="round"/>')
    text(x, y + R[kind] + 3.4, label, 1.7, TXT, 600, "middle", 0.3)


def button(name, x, y, label):
    comps.append(("button", name, x, y))
    text(x, y + R["button"] + 3.0, label, 1.7, TXT, 600, "middle", 0.3)


def lamps(name, x0, dx, y, names):
    extra[name] = (x0, y, dx)
    for i, n in enumerate(names):
        e(f'<circle cx="{x0 + i * dx:.2f}" cy="{y}" r="1.25" fill="#0d0e11"/>')
        text(x0 + i * dx, y + 4.0, n, 1.45, TXT, 600, "middle", 0.25)


def jack(name, x, y, label, out_=False):
    comps.append(("output" if out_ else "input", name, x, y))
    if out_:
        e(f'<rect x="{x - 4.6:.2f}" y="{y - 3.9:.2f}" width="9.2" height="10.6" rx="0.9" fill="{tint(0.32)}"/>')
    text(x, y + 5.7, label, 1.5, TXT if out_ else DIM, 600, "middle", 0.25)


def field(name, x, y, w, h=5.4, chevron=True):
    fields[name] = (x, y, w, h)
    e(f'<rect x="{x}" y="{y}" width="{w:.2f}" height="{h}" rx="0.9" fill="{DARK}" stroke="{LINE}" stroke-width="0.4"/>')
    if chevron:
        e(f'<polyline points="{x + w - 4.2:.2f},{y + 2.1:.2f} {x + w - 3:.2f},{y + 3.4:.2f} {x + w - 1.8:.2f},{y + 2.1:.2f}" '
          f'fill="none" stroke="{ACC}" stroke-width="0.4" stroke-linecap="round" stroke-linejoin="round"/>')


# ---------------------------------------------------------------- frame, header, display
e(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.2f}mm" height="{H}mm" viewBox="0 0 {W:.2f} {H}">')
e(f'<rect width="{W:.2f}" height="{H}" fill="{BG}"/>')
text(12.9, 7.6, "LIED", 5.1, TITLE, 800, "start", 1.08)
text(12.9 + width("LIED", 5.1, 800, 1.08) + 3.4, 7.45, "GENRA · TEXT SINGER", 1.76, DIM, 600, "start", 0.47)
PF = (W - 60, 3.4, 36, 5.4)
field("PRESETF", *PF)
e(f'<rect x="{W - 21.5:.2f}" y="5.55" width="8" height="1.08" rx="0.54" fill="{ACC}"/>')

DISPLAY = (3, 10.5, W - 6, 15)
e(f'<rect x="{DISPLAY[0]}" y="{DISPLAY[1]}" width="{DISPLAY[2]:.2f}" height="{DISPLAY[3]}" rx="1.6" fill="{DARK}" '
  f'stroke="{LINE}" stroke-width="0.4"/>')

# ---------------------------------------------------------------- lyrics: four editable lines on the panel
section(3, 27, 66.6, 47, "LYRICS")
LY = (6, 32.5, 60.6, 24.4)
field("LYRICSF", *LY, chevron=False)
for i in range(4):
    y = LY[1] + i * LY[3] / 4
    if i:
        e(f'<line x1="{LY[0] + 1}" y1="{y:.2f}" x2="{LY[0] + LY[2] - 1}" y2="{y:.2f}" stroke="{LINE}" stroke-width="0.25"/>')
    text(LY[0] + 2.6, y + 4.0, str(i + 1), 1.7, DIM, 700, "middle", 0.2)
text(21, 62.3, "STEP", 1.5, DIM, 700, "middle", 0.4)
lamps("STEP", 11, 10, 65, ["SYLL", "WORD", "FREE"])
button("RESTART", 47, 64.5, "RESTART")
button("NEXT", 61, 64.5, "NEXT")

# ---------------------------------------------------------------- voice
section(71.6, 27, W - 74.6, 47, "VOICE")
field("LANGF", 74.6, 32.5, 22.2)
field("VOICEF", 98.8, 32.5, 22.2)
knob("FORMANT", 85.1, 51, "FORMANT", "large", hot=True)
knob("BREATH", 102.2, 47.5, "BREATH")
knob("BRIGHT", 116, 47.5, "BRIGHT")
knob("ROUGH", 102.2, 65.2, "ROUGH", "small")
knob("CHOIR", 116, 65.2, "CHOIR", "small")

# ---------------------------------------------------------------- articulation, vibrato, fx
section(3, 76, 44, 26, "SING")
for i, (n, l) in enumerate([("SPEED", "SPEED"), ("VOWEL", "VOWEL"), ("GLIDE", "GLIDE"), ("ACCENT", "ACCENT")]):
    knob(n, 9 + i * 10.5, 89.5, l, "small")
section(49, 76, 32, 26, "VIBRATO")
for i, (n, l) in enumerate([("VDEPTH", "DEPTH"), ("VRATE", "RATE"), ("VDELAY", "DELAY")]):
    knob(n, 55 + i * 10, 89.5, l, "small")
section(83, 76, W - 86, 26, "FX")
for i, (n, l) in enumerate([("ECHO", "ECHO"), ("FDBK", "FDBK"), ("VERB", "VERB"), ("LOFI", "LO-FI")]):
    knob(n, 88.6 + i * 10, 89.5, l, "small")

# ---------------------------------------------------------------- in / out
e(f'<rect x="3" y="103.6" width="{W - 6:.2f}" height="24.2" rx="1.2" fill="{PLATE}"/>')


def row(groups, y, ytop, outs_last, x0):
    sl, gap = 10.8, 2.2
    gx = x0
    for gi, g in enumerate(groups):
        w = len(g) * sl
        e(f'<rect x="{gx + 0.3:.2f}" y="{ytop}" width="{w - 0.6:.2f}" height="11.4" rx="1" fill="#141519"/>')
        for i, (nm, lb) in enumerate(g):
            jack(nm, gx + sl / 2 + i * sl, y, lb, outs_last and gi == len(groups) - 1)
        gx += w + gap


x = genra_strip(jack, e, 6, 108.4, {"V/OCT": "VOCT", "GATE": "GATE", "VEL": "VEL", "ACC": "IN_ACC", "SLIDE": "SLIDE",
                                     "MOD": "IN_MOD"}, ACC, {"CLOCK": "CLOCK", "RESET": "IN_RESET"})
row([[("IN_LINE", "LINE"), ("IN_NEXT", "NEXT")], [("IN_FORM", "FORMANT")]],
    108.4, 103.5, False, x + 3.2)
G2 = [[("IN_BREATH", "BREATH"), ("IN_SPEED", "SPEED"), ("IN_VOWEL", "VOWEL")], [("IN_CHOIR", "CHOIR"), ("IN_VERB", "VERB")],
      [("OUT_SYL", "SYLL"), ("OUT_END", "END"), ("OUT_L", "L"), ("OUT_R", "R")]]
row(G2, 120.3, 116.3, True, W - 13 - (9 * 10.8 + 2 * 2.2))
e("</svg>")

if not PREVIEW:
    open(os.path.join(ROOT, "res", "Lied.svg"), "w").write("\n".join(out))
    lines = ["// Generated by tools/lied_panel.py - do not edit (mm)", "#pragma once", "namespace llayout {"]
    for kind, n, x, y in comps:
        lines.append(f"static const float {n}[2] = {{{x:.3f}f, {y:.3f}f}};")
    for n, (x, y, dx) in extra.items():
        lines.append(f"static const float {n}_LAMPS[3] = {{{x:.3f}f, {y:.3f}f, {dx:.3f}f}};  // first lamp x, y, distance")
    for n, (x, y, w, h) in fields.items():
        lines.append(f"static const float {n}[4] = {{{x:.3f}f, {y:.3f}f, {w:.3f}f, {h:.3f}f}};  // x, y, w, h")
    lines.append(f"static const float DISPLAY[4] = {{{DISPLAY[0]:.3f}f, {DISPLAY[1]:.3f}f, {DISPLAY[2]:.3f}f, {DISPLAY[3]:.3f}f}};")
    lines.append(f"static const int HP = {HP};")
    lines.append("}")
    os.makedirs(os.path.join(ROOT, "src", "lied"), exist_ok=True)
    open(os.path.join(ROOT, "src", "lied", "LiedLayout.hpp"), "w").write("\n".join(lines) + "\n")
    print("wrote res/Lied.svg and src/lied/LiedLayout.hpp")
    sys.exit(0)

# ---------------------------------------------------------------- preview: components + mock content
_cache = {}


def img(fn, x, y):
    if fn not in _cache:
        src = open(fn).read()
        src = re.sub(r"<\?xml[^>]*\?>|<!DOCTYPE[^>]*>|<!--.*?-->", "", src, flags=re.S)
        m = re.search(r"<svg[^>]*>", src, re.S)
        tag = m.group(0)
        w = float(re.search(r'\swidth="([0-9.]+)', tag).group(1))
        h = float(re.search(r'\sheight="([0-9.]+)', tag).group(1))
        vb = re.search(r'viewBox="([^"]+)"', tag)
        body = src[m.end():src.rindex("</svg>")]
        body = re.sub(r"<sodipodi:namedview.*?(/>|</sodipodi:namedview>)|<metadata.*?</metadata>", "", body, flags=re.S)
        body = re.sub(r'\s(inkscape|sodipodi):[a-zA-Z-]+="[^"]*"', "", body)
        _cache[fn] = (vb.group(1) if vb else f"0 0 {w} {h}", w / 2.9528, h / 2.9528, body)
    vbs, sw, sh, body = _cache[fn]
    out.insert(-1, f'<svg x="{x - sw / 2:.3f}" y="{y - sh / 2:.3f}" width="{sw:.3f}" height="{sh:.3f}" '
                   f'viewBox="{vbs}" overflow="visible">{body}</svg>')


def ptext(*a, **k):
    n = len(out)
    w = text(*a, **k)
    out.insert(-1, out.pop(n))
    return w


def prect(s):
    out.insert(-1, s)


for kind, n, x, y in comps:
    if kind in KIND:
        img(os.path.join(KNOBS, KIND[kind] + "_bg.svg"), x, y)
        img(os.path.join(KNOBS, KIND[kind] + ".svg"), x, y)
    elif kind == "button":
        img(os.path.join(CL, "VCVBezel.svg"), x, y)
    else:
        img(os.path.join(CL, "PJ301M.svg"), x, y)
for sx_ in (7.62, W - 7.62):
    for sy in (2.54, H - 2.54):
        img(os.path.join(CL, "ScrewBlack.svg"), sx_, sy)
x0, y0, ddx = extra["STEP"]
prect(f'<circle cx="{x0}" cy="{y0}" r="1.25" fill="{ACC}"/>')

# preset and selection fields
ptext(PF[0] + 2, PF[1] + 3.75, "04  POP CHORUS", 1.9, TXT, 700, "start", 0.35)
for n, a in (("LANGF", "DEUTSCH"), ("VOICEF", "KLATT F2")):
    fx, fy, fw, fh = fields[n]
    ptext(fx + 2, fy + 3.75, a, 1.8, TXT, 700, "start", 0.3)

# lyrics: line 2 is playing, its current syllable lit
lines = [["Hal", "lo ", "Welt, ", "ich ", "bin ", "da"],
         ["wir ", "sin", "gen ", "durch ", "die ", "Nacht"],
         ["la ", "la ", "la ", "la ", "la"],
         []]
lx, ly, lw, lh = LY
for i, syl in enumerate(lines):
    y = ly + i * lh / 4 + 4.0
    if i == 1:
        prect(f'<rect x="{lx + 0.6}" y="{ly + i * lh / 4 + 0.6:.2f}" width="0.7" height="{lh / 4 - 1.2:.2f}" rx="0.3" fill="{ACC}"/>')
    if not syl:
        ptext(lx + 5.2, y, "click to type…", 1.9, "#4a4c57", 600, "start", 0.2)
        continue
    cx = lx + 5.2
    for j, s in enumerate(syl):
        sep = "" if s.endswith(" ") or j == len(syl) - 1 else "-"
        col = TXT if i == 1 else DIM
        if i == 1 and j == 3:
            col = ACC
            sw = width(s.strip(), 2.1, 700, 0.2)
            prect(f'<rect x="{cx - 0.6:.2f}" y="{y - 2.9:.2f}" width="{sw + 1.2:.2f}" height="3.9" rx="0.6" fill="{tint(0.22, DARK)}"/>')
        cx += ptext(cx, y, s.strip() + sep, 2.1, col, 700, "start", 0.2)
        if s.endswith(" "):
            cx += 1.3

# display
dx, dy, dw, dh = DISPLAY
ptext(dx + 4, dy + 4.5, "LINE 2 / 4 · SYLLABLE 4 / 6", 1.5, DIM, 700, "start", 0.35)
ptext(dx + 4, dy + 10.5, "durch", 4.4, "#ffffff", 800, "start", 0.3)
ptext(dx + 4, dy + 13.8, "[ d U r C ]", 1.5, ACC, 700, "start", 0.4)
# formant envelope: three peaks
sx, sw, base = 47, 36, dy + 12.6
pts = []
for i in range(121):
    f = i / 120
    v = 0.9 * math.exp(-((f - 0.12) / 0.05) ** 2) + 0.65 * math.exp(-((f - 0.36) / 0.06) ** 2) \
        + 0.4 * math.exp(-((f - 0.62) / 0.07) ** 2) + 0.06
    pts.append(f"{sx + f * sw:.2f},{base - 8.5 * v:.2f}")
prect(f'<polyline points="{" ".join(pts)}" fill="none" stroke="{ACC}" stroke-width="0.45"/>')
for f, lb in ((0.12, "F1"), (0.36, "F2"), (0.62, "F3")):
    ptext(sx + f * sw, base + 1.6, lb, 1.2, DIM, 700, "middle", 0.2)
ptext(dx + dw - 4, dy + 4.5, "DE · KLATT F2", 1.5, DIM, 700, "end", 0.35)
ptext(dx + dw - 4, dy + 10.5, "A4", 4.4, "#ffffff", 800, "end", 0.3)
ptext(dx + dw - 4, dy + 13.8, "STEP SYLL · GLIDE", 1.5, ACC, 700, "end", 0.3)

svg = sys.argv[2].rsplit(".", 1)[0] + ".svg"
open(svg, "w").write("\n".join(out))
subprocess.run(["rsvg-convert", "-w", "2200", svg, "-o", sys.argv[2]], check=True)
print("wrote", sys.argv[2])
