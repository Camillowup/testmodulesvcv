#!/usr/bin/env python3
"""Panels of KESSEL (the LXR firmware's drum engine, 26 HP) and its expander KESSEL FX (12 HP), in the look of FUNKE
and SCHMELZ (the GENRA instruments' pattern): preset field in the header, a display, sections with an accent line, the
main knob large with a dotted ring in the accent colour, the jacks in two rows at the bottom (the GENRA strip first).
Accent: the drum track's violet (as DRUM-X).

Usage:  kessel_panel.py real            -> res/Kessel.svg, res/KesselFx.svg (text as paths) + src/kessel/KesselLayout.hpp
        kessel_panel.py preview OUTDIR  -> OUTDIR/Kessel.png, OUTDIR/KesselFx.png with stand-in knobs and jacks and the
                                           text the module draws (the knob labels that change with the lane / effect)

Coordinates in mm (height 128.5 mm); the C++ side converts with mm2px(). The panel holds what never changes; the
module draws the display, the field texts, the section title VOICE · <lane> and the knob labels that depend on the lane
or the effect. Every text box is checked against every other: an overlap stops the script.
"""
import math
import os
import subprocess
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

MODE = sys.argv[1] if len(sys.argv) > 1 else "real"
PREVIEW = MODE == "preview"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

H = 128.5
BG, PLATE, LINE, TXT, DIM, DARK = "#17181d", "#212329", "#34363f", "#d9dbe2", "#8a8d99", "#07080a"
TITLE, RING, TRAY, SYNC_LINE = "#e6e7ec", "#4a4c57", "#141519", "#5a5c66"
# keep in step with src/kessel/Kessel.cpp (ACCENT): the drum track's colour, as DRUM-X
ACC = "#7b6cff"
R = {"small": 3.84, "knob": 4.8, "large": 6.1, "jack": 4.0, "button": 4.6}
SLOT = 9.4

FONT_DIR = "/usr/share/fonts/opentype/inter/"
FONTS = {600: "Inter-SemiBold.otf", 700: "Inter-Bold.otf", 800: "Inter-ExtraBold.otf"}
_fonts = {}


def font(w):
    if w not in _fonts:
        f = TTFont(FONT_DIR + FONTS[w])
        _fonts[w] = (f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm, f["hmtx"])
    return _fonts[w]


def width(s, size, weight, sp):
    gs, cmap, upm, hmtx = font(weight)
    return sum(hmtx[cmap.get(ord(c), "space")][0] * size / upm + sp for c in s) - sp


class Panel:
    def __init__(self, name, hp):
        self.name, self.W = name, hp * 5.08
        self.out, self.comps, self.fields, self.areas, self.boxes, self.mock = [], [], {}, {}, [], []
        self.e(f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.W:.2f}mm" height="{H}mm" viewBox="0 0 {self.W:.2f} {H}">')
        self.e(f'<rect width="{self.W:.2f}" height="{H}" fill="{BG}"/>')

    def e(self, s):
        self.out.append(s)

    def text(self, x, y, s, size=1.8, color=DIM, weight=600, anchor="middle", sp=0.25, mock=False):
        gs, cmap, upm, hmtx = font(weight)
        sc = size / upm
        w = width(s, size, weight, sp)
        cx = x - (w / 2 if anchor == "middle" else (w if anchor == "end" else 0))
        self.boxes.append((cx, y - size * 0.75, cx + w, y + 0.1, s))
        paths = []
        for c in s:
            g = cmap.get(ord(c), "space")
            pen = SVGPathPen(gs)
            gs[g].draw(TransformPen(pen, (sc, 0, 0, -sc, cx, y)))
            if pen.getCommands():
                paths.append(pen.getCommands())
            cx += hmtx[g][0] * sc + sp
        if paths:
            svg = f'<path fill="{color}" d="{" ".join(paths)}"/>'
            (self.mock if mock else self.out).append(svg)

    def dyn(self, name, x, y, s, size=1.7, anchor="middle"):
        """A text the module draws (it changes): kept in the layout, drawn here only in the preview."""
        self.areas[name] = (x, y)
        self.text(x, y, s, size, TXT, 600, anchor, 0.3, mock=True)

    def tint(self, f, base=PLATE):
        c = [int(ACC[i:i + 2], 16) for i in (1, 3, 5)]
        p = [int(base[i:i + 2], 16) for i in (1, 3, 5)]
        return "#" + "".join(f"{round(p[i] + (c[i] - p[i]) * f):02x}" for i in range(3))

    def section(self, x, y, w, h, title, dyn_name=None):
        self.e(f'<rect x="{x:.2f}" y="{y}" width="{w:.2f}" height="{h}" rx="1.2" fill="{PLATE}"/>')
        self.e(f'<rect x="{x:.2f}" y="{y}" width="{w:.2f}" height="0.88" rx="0.44" fill="{ACC}"/>')
        if dyn_name:
            self.areas[dyn_name] = (x + w / 2, y + 5.0)
            self.text(x + w / 2, y + 5.0, title, 2.03, ACC, 800, "middle", 0.75, mock=True)
        else:
            self.text(x + w / 2, y + 5.0, title, 2.03, ACC, 800, "middle", 0.75)

    def knob(self, name, x, y, label, kind="knob", hot=False, dyn=False):
        self.comps.append((kind, name, x, y))
        r = R[kind]
        self.e(f'<circle cx="{x}" cy="{y}" r="{r + 0.88:.2f}" fill="none" stroke="{ACC if hot else RING}" '
               f'stroke-width="0.29" stroke-dasharray="0.29 0.73" stroke-linecap="round"/>')
        if dyn:
            self.dyn("LBL_" + name, x, y + r + 3.4, label)
        else:
            self.text(x, y + r + 3.4, label, 1.7, TXT, 600, "middle", 0.3)

    def button(self, name, x, y, label):
        self.comps.append(("button", name, x, y))
        self.text(x, y + R["button"] + 3.0, label, 1.6, TXT, 600, "middle", 0.3)

    def jack(self, name, x, y, label, out_=False):
        self.comps.append(("output" if out_ else "input", name, x, y))
        if out_:
            self.e(f'<rect x="{x - 4.6:.2f}" y="{y - 3.9:.2f}" width="9.2" height="10.6" rx="0.9" fill="{self.tint(0.32)}"/>')
        self.text(x, y + 5.7, label, 1.5, TXT if out_ else DIM, 600, "middle", 0.25)

    def field(self, name, x, y, w, mock_a="", mock_b=""):
        self.fields[name] = (x, y, w, 5.4)
        self.e(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="5.4" rx="0.9" fill="{DARK}" stroke="{LINE}" stroke-width="0.4"/>')
        self.e(f'<polyline points="{x + w - 4.2:.2f},{y + 2.1:.2f} {x + w - 3:.2f},{y + 3.4:.2f} {x + w - 1.8:.2f},{y + 2.1:.2f}" '
               f'fill="none" stroke="{ACC}" stroke-width="0.4" stroke-linecap="round" stroke-linejoin="round"/>')
        if mock_a:
            self.text(x + 2, y + 3.75, mock_a, 1.9, TXT, 700, "start", 0.35, mock=True)
        if mock_b:
            self.text(x + w - 6, y + 3.6, mock_b, 1.4, DIM, 600, "end", 0.25, mock=True)

    def tray(self, x, y, n, line=None, slot=SLOT):
        w = n * slot
        self.e(f'<rect x="{x + 0.3:.2f}" y="{y - 4.6:.2f}" width="{w - 0.6:.2f}" height="11.2" rx="1" fill="{TRAY}"/>')
        if line:
            self.e(f'<rect x="{x + 1.2:.2f}" y="{y - 4.6:.2f}" width="{w - 2.4:.2f}" height="0.55" rx="0.27" fill="{line}"/>')
        return x + w

    def header(self, name, sub, preset_w, mock_preset):
        self.text(12.9, 7.6, name, 5.1, TITLE, 800, "start", 1.08)
        if sub:
            self.text(12.9 + width(name, 5.1, 800, 1.08) + 3.4, 7.45, sub, 1.76, DIM, 600, "start", 0.47)
        if preset_w:
            x = self.W - 23.5 - preset_w
            self.field("PRESETF", x, 3.4, preset_w, mock_preset)
            self.boxes.append((x, 3.4, x + preset_w, 8.8, "[preset field]"))
        self.e(f'<rect x="{self.W - 21.5:.2f}" y="5.55" width="8" height="1.08" rx="0.54" fill="{ACC}"/>')

    def display(self, y, h):
        self.fields["DISPLAY"] = (3, y, self.W - 6, h)
        self.e(f'<rect x="3" y="{y}" width="{self.W - 6:.2f}" height="{h}" rx="1.6" fill="{DARK}" stroke="{LINE}" stroke-width="0.4"/>')

    def check(self):
        bad = []
        bx = self.boxes
        for i, a in enumerate(bx):
            for b in bx[i + 1:]:
                if "[preset field]" in (a[4], b[4]) and (a[4] in MOCK_IN_FIELD or b[4] in MOCK_IN_FIELD):
                    continue
                if a[0] < b[2] - 0.05 and b[0] < a[2] - 0.05 and a[1] < b[3] - 0.05 and b[1] < a[3] - 0.05:
                    bad.append((a[4], b[4]))
        if bad:
            sys.exit(f"{self.name}: texts overlap: {bad}")


MOCK_IN_FIELD = {"01  TECHNO KIT"}

# =============================================================================================== KESSEL
LANES = ["KICK", "SNARE", "", "RIM", "HAT CL", "HAT OP", "TOM", "CYM"]  # GENRA's order; the clap shares the snare
k = Panel("Kessel", 26)
W = k.W
k.header("KESSEL", "GENRA · DRUMS", 38, "01  TECHNO KIT")
k.display(10.5, 14)

# the lane buttons: big, in GENRA's order (the module draws them: name, lit by its hit, the edited one filled)
bw, pitch, by = 14.6, (W - 6) / 8, 26.6
lanes = []
for i, n in enumerate(LANES):
    x = 3 + i * pitch + (pitch - bw) / 2
    lanes.append((x, by, bw, 6.6))
    if n:
        k.e(f'<rect x="{x:.2f}" y="{by}" width="{bw}" height="6.6" rx="1" fill="#2b2d35" stroke="{LINE}" stroke-width="0.3"/>')
        k.text(x + bw / 2, by + 4.0, n, 1.75, TXT, 700, "middle", 0.3, mock=True)
        k.boxes.pop()
    else:  # the clap's place: GENRA's clap plays the snare (the LXR's snare is its clap, by REPEAT)
        k.e(f'<rect x="{x:.2f}" y="{by}" width="{bw}" height="6.6" rx="1" fill="none" stroke="{LINE}" stroke-width="0.3" '
            f'stroke-dasharray="0.8 0.6"/>')
        k.text(x + bw / 2, by + 4.0, "FREI", 1.5, "#55575f", 700, "middle", 0.3)

Y = 35.2
k.section(3, Y, 62.5, 44, "VOICE · KICK", "VOICE_TITLE")
k.field("WAVEF", 6, Y + 6.5, 56.5, "SINE", "OSCILLATOR")
k.knob("TUNE", 15, Y + 21, "TUNE", "large", hot=True)
k.knob("FM", 31, Y + 18.5, "FM", dyn=True)
k.knob("FMFREQ", 44, Y + 18.5, "FM FREQ", dyn=True)
k.knob("CLICK", 57, Y + 18.5, "CLICK", dyn=True)
k.knob("PAMT", 31, Y + 35.5, "P.AMT", "small", dyn=True)
k.knob("PDEC", 44, Y + 35.5, "P.DEC", "small", dyn=True)
k.knob("PAN", 57, Y + 35.5, "PAN", "small")
fx0 = 67.5
k.section(fx0, Y, W - fx0 - 3, 44, "FILTER · AMP")
k.field("FTYPEF", fx0 + 3, Y + 6.5, W - fx0 - 9, "LOWPASS", "SVF 12 DB")
k.knob("CUT", fx0 + 12, Y + 21, "CUTOFF", "large", hot=True)
k.knob("RES", fx0 + 28, Y + 18.5, "RES")
k.knob("DRIVE", fx0 + 41, Y + 18.5, "DRIVE")
k.knob("LEVEL", fx0 + 54, Y + 18.5, "LEVEL")
k.knob("ATK", fx0 + 28, Y + 35.5, "ATK", "small")
k.knob("DEC", fx0 + 41, Y + 35.5, "DEC", "small")
k.knob("SEND", fx0 + 54, Y + 35.5, "FX SEND", "small")

Y2 = 81.2
k.section(3, Y2, W - 6, 21.2, "KIT · MASTER")
for i, (n, l) in enumerate([("KTUNE", "TUNE"), ("KDECAY", "DECAY"), ("KTONE", "TONE"), ("KDRIVE", "DRIVE"),
                            ("KACC", "ACCENT"), ("KFX", "FX"), ("KVOL", "VOLUME")]):
    k.knob(n, 3 + (W - 6) / 7 * (i + 0.5), Y2 + 12.2, l)

k.e(f'<rect x="3" y="103.6" width="{W - 6:.2f}" height="24.2" rx="1.2" fill="{PLATE}"/>')
y1 = 108.4
x = 6.0
# the GENRA strip: DRUMS (GENRA's 8 drum triggers, polyphonic) · VEL · ACC, then CLOCK · RESET
x = k.tray(x, y1, 3, k.tint(0.35, TRAY))
for i, (n, l) in enumerate([("IN_DRUMS", "DRUMS"), ("IN_VEL", "VEL"), ("IN_ACC", "ACC")]):
    k.jack(n, 6 + SLOT / 2 + i * SLOT, y1, l)
x += 2.2
xs = x
x = k.tray(x, y1, 2, SYNC_LINE)
for i, (n, l) in enumerate([("IN_CLOCK", "CLOCK"), ("IN_RESET", "RESET")]):
    k.jack(n, xs + SLOT / 2 + i * SLOT, y1, l)
x += 2.4
xs = x
cvs = [("IN_KTUNE", "TUNE"), ("IN_KDECAY", "DECAY"), ("IN_KTONE", "TONE"), ("IN_KDRIVE", "DRIVE"), ("IN_KFX", "FX"),
       ("IN_KVOL", "VOL")]
k.tray(xs, y1, len(cvs))
for i, (n, l) in enumerate(cvs):
    k.jack(n, xs + SLOT / 2 + i * SLOT, y1, l)
y2 = 120.3
sl = 11.0
gx = (W - (9 * sl + 2.4)) / 2
outs = [(f"OUT_LANE{i}", n) for i, n in enumerate(LANES) if n] + [("OUT_L", "L"), ("OUT_R", "R")]
for i, (n, l) in enumerate(outs):
    k.jack(n, gx + sl / 2 + i * sl + (2.4 if i >= 7 else 0), y2, l, True)
k.check()

# =============================================================================================== KESSEL FX
f = Panel("KesselFx", 12)
FW = f.W
f.text(10.2, 7.8, "KESSEL FX", 4.2, TITLE, 800, "start", 0.8)
f.text(FW - 14.5, 7.0, "LINK", 1.5, TXT, 700, "end", 0.35)
f.e(f'<rect x="{FW - 13.2:.2f}" y="5.55" width="6.5" height="1.08" rx="0.54" fill="{ACC}"/>')
f.areas["LINK"] = (FW - 9.95, 6.1)
f.display(10.5, 14)
Y = 26.5
f.section(3, Y, FW - 6, 46.5, "EFFECT")
f.field("FXF", 6, Y + 6.5, FW - 12, "DELAY", "TEMPO")
f.knob("MIX", 15, Y + 21, "MIX", "large", hot=True)
f.knob("P1", 32, Y + 18.5, "TIME", dyn=True)
f.knob("P2", 47, Y + 18.5, "FDBK", dyn=True)
f.button("ON", 15, Y + 37.0, "ON")
f.knob("P3", 32, Y + 35.5, "TONE", "small", dyn=True)
f.knob("P4", 47, Y + 35.5, "RANGE", "small", dyn=True)
Y2 = 75
f.section(3, Y2, FW - 6, 27.4, "MOD · OUT")
f.field("CVF", 6, Y2 + 6.5, FW - 12, "TIME", "CV TARGET")
f.knob("DEPTH", 17, Y2 + 19.2, "DEPTH", "small")
f.knob("LEVEL", FW - 17, Y2 + 19.2, "LEVEL", "small")
f.e(f'<rect x="3" y="103.6" width="{FW - 6:.2f}" height="24.2" rx="1.2" fill="{PLATE}"/>')
y1 = 108.4
x = (FW - (4 * SLOT + 4.4)) / 2
f.tray(x, y1, 1, SYNC_LINE)
f.jack("IN_CLOCK", x + SLOT / 2, y1, "CLOCK")
x += SLOT + 2.2
f.tray(x, y1, 1)
f.jack("IN_CV", x + SLOT / 2, y1, "CV")
x += SLOT + 2.2
f.tray(x, y1, 2)
f.jack("IN_L", x + SLOT / 2, y1, "IN L")
f.jack("IN_R", x + SLOT * 1.5, y1, "IN R")
for i, (n, l) in enumerate([("OUT_L", "L"), ("OUT_R", "R")]):
    f.jack(n, FW / 2 - 5.7 + i * 11.4, 120.3, l, True)
f.check()


# ---------------------------------------------------------------- the texts the module switches, as small SVGs
# (in the panel's font, so a label that changes looks like the printed ones). res/kessel/<kind>_<slug>.svg, the
# baseline LBL_BASE mm below the top; the module draws it centred on the anchor the layout gives.
LABEL_KINDS = {
    # kind: (size, weight, spacing, colour)
    "knob": (1.7, 600, 0.3, TXT),
    "title": (2.03, 800, 0.75, ACC),
    "lane": (1.75, 700, 0.3, TXT),
    "lanesel": (1.75, 700, 0.3, "#0b0b10"),
}


def slug(t):
    return "".join(c if c.isalnum() else "_" for c in t.lower())


def label_svg(kind, t):
    size, weight, sp, color = LABEL_KINDS[kind]
    w = width(t, size, weight, sp) + 0.6
    h = size * 1.25
    gs, cmap, upm, hmtx = font(weight)
    sc = size / upm
    cx = 0.3
    paths = []
    for c in t:
        g = cmap.get(ord(c), "space")
        pen = SVGPathPen(gs)
        gs[g].draw(TransformPen(pen, (sc, 0, 0, -sc, cx, size)))
        if pen.getCommands():
            paths.append(pen.getCommands())
        cx += hmtx[g][0] * sc + sp
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.3f}mm" height="{h:.3f}mm" viewBox="0 0 {w:.3f} {h:.3f}">'
            f'<path fill="{color}" d="{" ".join(paths)}"/></svg>')


VOICE_LABELS = ["FM", "FM FREQ", "CLICK", "P.AMT", "P.DEC", "NOISE", "N.FREQ", "REPEAT", "FM 2", "FREQ 2"]
FX_LABELS = ["TIME", "FDBK", "TONE", "RANGE", "SIZE", "DECAY", "EARLY", "DRIVE", "COLOR", "LEVEL", "OFFSET", "FREQ",
             "WAVE", "BITS", "RATE", "RES", "-"]
LABELS = [("knob", t) for t in VOICE_LABELS + FX_LABELS]
LABELS += [("title", "VOICE \u00b7 " + n) for n in LANES if n]
LABELS += [(k, n) for n in LANES if n for k in ("lane", "lanesel")]


# =============================================================================================== output
def stand_ins(p):
    """Knobs, buttons, jacks and screws for the preview (Rack draws its own)."""
    s = ['<defs><radialGradient id="kn" cx="0.4" cy="0.35" r="0.7"><stop offset="0" stop-color="#3a3c45"/>'
         '<stop offset="1" stop-color="#0c0d10"/></radialGradient><radialGradient id="jk" cx="0.4" cy="0.35" r="0.75">'
         '<stop offset="0" stop-color="#ffffff"/><stop offset="0.6" stop-color="#c9cbd1"/><stop offset="1" stop-color="#8d9098"/>'
         '</radialGradient></defs>']
    for kind, n, x, y in p.comps:
        if kind in ("small", "knob", "large"):
            r = R[kind]
            s.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#050507"/><circle cx="{x}" cy="{y}" r="{r - 0.35:.2f}" fill="url(#kn)"/>')
            s.append(f'<line x1="{x}" y1="{y - r * 0.35:.2f}" x2="{x}" y2="{y - r * 0.8:.2f}" stroke="#f2f2f5" '
                     f'stroke-width="{r * 0.16:.2f}" stroke-linecap="round"/>')
        elif kind == "button":
            s.append(f'<circle cx="{x}" cy="{y}" r="{R["button"]}" fill="#050507"/><circle cx="{x}" cy="{y}" '
                     f'r="{R["button"] - 0.7:.2f}" fill="{ACC}"/>')
        else:
            s.append(f'<circle cx="{x}" cy="{y}" r="3.15" fill="url(#jk)"/><circle cx="{x}" cy="{y}" r="2.35" fill="#e8e9ec"/>'
                     f'<circle cx="{x}" cy="{y}" r="1.55" fill="#050505"/>')
    for sx in (7.62, p.W - 7.62):
        for sy in (2.54, H - 2.54):
            s.append(f'<circle cx="{sx}" cy="{sy}" r="1.6" fill="#2a2b31" stroke="#9a9ca4" stroke-width="0.25"/>')
    return s


if not PREVIEW:
    for p in (k, f):
        open(os.path.join(ROOT, "res", p.name + ".svg"), "w").write("\n".join(p.out + ["</svg>"]))
    lines = ["// Generated by tools/kessel_panel.py - do not edit", "// mm; the module converts with mm2px().",
             "#pragma once"]
    for ns, p in (("kessel", k), ("kesselfx", f)):
        lines.append(f"namespace {ns} {{")
        lines.append(f"static const int HP = {int(round(p.W / 5.08))};")
        for kind, n, x, y in p.comps:
            lines.append(f"static const float {n}[2] = {{{x:.3f}f, {y:.3f}f}};  // {kind}")
        for n, (x, y, w, h) in p.fields.items():
            lines.append(f"static const float {n}[4] = {{{x:.3f}f, {y:.3f}f, {w:.3f}f, {h:.3f}f}};  // x, y, w, h")
        for n, (x, y) in p.areas.items():
            lines.append(f"static const float {n}[2] = {{{x:.3f}f, {y:.3f}f}};  // a text the module draws (centre, baseline)")
        if p is k:
            lines.append("static const float LANE[8][4] = {" + ", ".join(
                f"{{{x:.3f}f, {y:.3f}f, {w:.3f}f, {h:.3f}f}}" for (x, y, w, h) in lanes) + "};  // x, y, w, h")
        lines.append("}")
    lines.append("namespace kessellabels {")
    lines.append("// the switched texts: res/kessel/<kind>_<slug>.svg, baseline this far below the top (mm, per kind)")
    for kind, (size, *_rest) in LABEL_KINDS.items():
        lines.append(f"static const float BASE_{kind.upper()} = {size:.3f}f;")
    lines.append("}")
    os.makedirs(os.path.join(ROOT, "res", "kessel"), exist_ok=True)
    for kind, t in LABELS:
        open(os.path.join(ROOT, "res", "kessel", f"{kind}_{slug(t)}.svg"), "w").write(label_svg(kind, t))
    os.makedirs(os.path.join(ROOT, "src", "kessel"), exist_ok=True)
    open(os.path.join(ROOT, "src", "kessel", "KesselLayout.hpp"), "w").write("\n".join(lines) + "\n")
    print("wrote res/Kessel.svg, res/KesselFx.svg, res/kessel/*.svg and src/kessel/KesselLayout.hpp")
else:
    outdir = sys.argv[2]
    for p in (k, f):
        svg = os.path.join(outdir, p.name + ".svg")
        open(svg, "w").write("\n".join(p.out + p.mock + stand_ins(p) + ["</svg>"]))
        subprocess.run(["rsvg-convert", "-w", str(int(p.W * 15.5)), svg, "-o", os.path.join(outdir, p.name + ".png")],
                       check=True)
    print("wrote", outdir)
