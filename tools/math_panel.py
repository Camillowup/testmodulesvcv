#!/usr/bin/env python3
"""The panel of MATH (bytebeat / floatbeat from JIANT FM) in the GENRA look. After GENRA's tools/jiant_panel.py.

Usage:  math_panel.py real                    -> res/Math.svg (text as paths) + src/math/MathLayout.hpp
        math_panel.py preview OUTDIR RACK     -> OUTDIR/Math.png with Rack components and mock display content
                                                 (RACK: a Rack source tree, for res/ComponentLibrary)

Coordinates are Rack pixels (1 HP = 15 px, height 380 px), so the C++ side uses them as they are.
The panel holds what never changes; the module widgets draw the display, the knob names that follow MODE / LANG,
the values, the pads and the lit selectors.
"""
import base64
import os
import re
import subprocess
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from panellib import dim  # noqa: E402

MODE = sys.argv[1] if len(sys.argv) > 1 else "real"
PREVIEW = MODE == "preview"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H = 380

BG, PLATE, LINE, TXT, DIM = "#17181d", "#212329", "#34363f", "#d9dbe2", "#8a8d99"
# keep in step with src/math/Math.cpp (MATH_RGB)
COLORS = {"Math": "#c0549a"}

# component radii (px)
R_SMALL, R_TRIM, R_JACK, R_BEZEL = 11.3, 8.9, 11.8, 10.6

FONT_DIR = "/usr/share/fonts/opentype/inter/"
FONTS = {600: "Inter-SemiBold.otf", 700: "Inter-Bold.otf", 800: "Inter-ExtraBold.otf"}
_fonts = {}


def font(weight):
    if weight not in _fonts:
        f = TTFont(FONT_DIR + FONTS.get(weight, FONTS[600]))
        _fonts[weight] = (f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm, f["hmtx"])
    return _fonts[weight]


def tint(hex_, f):
    """The module colour mixed onto the plate (f: the colour's share)."""
    a = [int(hex_[i:i + 2], 16) for i in (1, 3, 5)]
    b = [int(PLATE[i:i + 2], 16) for i in (1, 3, 5)]
    return "#" + "".join(f"{round(b[i] + (a[i] - b[i]) * f):02x}" for i in range(3))


class Panel:
    def __init__(self, slug, title, hp, sub):
        self.slug, self.W, self.C = slug, hp * 15, COLORS[slug]
        self.o, self.comp, self.mock = [], [], []
        W = self.W
        self.e(f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
               f'width="{W * 25.4 / 75:.3f}mm" height="{H * 25.4 / 75:.3f}mm" viewBox="0 0 {W} {H}">')
        self.e(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
        self.text(38, 22.5, title, 15, "#e6e7ec", 800, "start", 3.2)
        self.text(38 + self.width(title, 15, 800, 3.2) + 10, 22, sub, 5.2, DIM, 600, "start", 1.4)
        self.e(f'<rect x="{W - 80}" y="16.4" width="34" height="3.2" rx="1.6" fill="{self.C}"/>')

    # ------------------------------------------------------------------ drawing
    def e(self, s):
        self.o.append(s)

    def width(self, s, size, weight, sp):
        gs, cmap, upm, hmtx = font(weight)
        return sum(hmtx[cmap.get(ord(c), "space")][0] * size / upm + sp for c in s) - sp

    def text(self, x, y, s, size=5.0, color=TXT, weight=600, anchor="middle", sp=0.6):
        gs, cmap, upm, hmtx = font(weight)
        scale = size / upm
        w = self.width(s, size, weight, sp)
        cx = x - (w / 2 if anchor == "middle" else (w if anchor == "end" else 0))
        paths = []
        for c in s:
            g = cmap.get(ord(c), "space")
            pen = SVGPathPen(gs)
            gs[g].draw(TransformPen(pen, (scale, 0, 0, -scale, cx, y)))
            if pen.getCommands():
                paths.append(pen.getCommands())
            cx += hmtx[g][0] * scale + sp
        if paths:
            self.e(f'<path fill="{color}" d="{" ".join(paths)}"/>')

    def plate(self, x, y, w, h, bar=False):
        self.e(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3.5" fill="{PLATE}"/>')
        if bar:
            self.e(f'<rect x="{x}" y="{y}" width="{w}" height="2.6" rx="1.3" fill="{self.C}"/>')

    def title(self, x, y, s, size=6.0, sp=2.2):
        self.text(x, y, s, size, self.C, 800, "middle", sp)

    def sep(self, x, y0, y1):
        self.e(f'<line x1="{x}" y1="{y0}" x2="{x}" y2="{y1}" stroke="{LINE}" stroke-width="0.8"/>')

    def block(self, x, y, w, h):
        self.e(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3" fill="{tint(self.C, 0.13)}"/>')

    def add(self, kind, name, x, y, **kw):
        self.comp.append((kind, name, x, y, kw))

    # ------------------------------------------------------------------ components
    def knob(self, name, x, y, r=R_SMALL, label=None, ly=None, ring=True, v=0.5):
        self.add("trim" if r == R_TRIM else "knob", name, x, y, v=v)
        if ring:
            self.e(f'<circle cx="{x}" cy="{y}" r="{r + 2.6}" fill="none" stroke="#4a4c57" stroke-width="0.85" '
                   f'stroke-dasharray="0.85 2.15" stroke-linecap="round"/>')
        if label:
            self.text(x, ly, label, 5.0 if r > R_TRIM else 4.7)

    def jack(self, name, x, y, label=None, ly=None, out=False):
        self.add("output" if out else "input", name, x, y)
        if out:
            self.e(f'<rect x="{x - 12.5}" y="{y - 14}" width="25" height="{(ly or y + 21) - y + 19}" rx="2.6" fill="{tint(self.C, 0.32)}"/>')
        if label:
            self.text(x, ly or y + 21, label, 4.8, TXT)

    def button(self, name, x, y, label=None, ly=None, light=False):
        self.add("lightbutton" if light else "button", name, x, y)
        if label:
            self.text(x, ly or y + 19.5, label, 4.8)

    def area(self, name, x, y, w, h):
        """A rectangle the module widget draws into."""
        self.add("area", name, x, y, w=w, h=h)

    def leds(self, name, x0, y, dx, labels, ly):
        self.add("leds", name, x0, y, dx=dx, n=len(labels))
        for i, n in enumerate(labels):
            self.e(f'<circle cx="{x0 + i * dx}" cy="{y}" r="4.6" fill="#0d0e11"/>')
            self.text(x0 + i * dx, ly, n, 4.6, DIM)

    def display(self, x, y, w, h):
        self.e(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="4" fill="#07080a" stroke="{LINE}" stroke-width="1.2"/>')
        sh = h - 8
        sw = round(sh * 232 / 150, 1)
        self.area("SCREEN", x + 5, y + 4, sw, sh)
        self.sep(x + sw + 10, y + 8, y + h - 8)
        self.area("INFO", x + sw + 19, y + 4, x + w - (x + sw + 19) - 5, h - 8)

    # ------------------------------------------------------------------ output
    def svg(self):
        return "\n".join(self.o + ["</svg>"])

    def layout(self):
        """Names ending in a number become arrays (E0, E1 .. -> E[n][2])."""
        L = [f"namespace {self.slug.lower()} {{", f"static const float W = {self.W}.f;"]
        groups, order = {}, []
        for kind, name, x, y, kw in self.comp:
            if kind == "area":
                v = [x, y, kw["w"], kw["h"]]
            elif kind == "leds":
                v = [x, y, kw["dx"]]
            else:
                v = [x, y]
            m = re.match(r"(.*?[A-Z_])(\d+)$", name)
            key = m.group(1) if m else name
            if key not in groups:
                groups[key] = []
                order.append(key)
            groups[key].append(v)
        for key in order:
            vs = groups[key]
            fmt = lambda v: "{" + ", ".join(f"{a:.1f}f" for a in v) + "}"
            if len(vs) == 1 and not re.search(r"\d$", next(n for k, n, *_ in self.comp if n.startswith(key))):
                L.append(f"static const float {key}[{len(vs[0])}] = {fmt(vs[0])};")
            else:
                L.append(f"static const float {key.rstrip('_')}[{len(vs)}][{len(vs[0])}] = {{{', '.join(fmt(v) for v in vs)}}};")
        L.append(f"}}  // namespace {self.slug.lower()}")
        return "\n".join(L)


def io_row(p, ins, outs=("L", "R")):
    """Inputs on the left (plain jacks), the mix out on the right (on tiles), labels below."""
    p.plate(14, 316, 236, 44)
    # the GENRA strip (tools/panellib.py look, in px: 1 mm = 2.9528): V/OCT, GATE, VEL on a tray in the module colour
    SL = 27.76
    notes = [x for x in ins if x[1] in ("V/OCT", "GATE", "VEL")]
    rest = [x for x in ins if x not in notes]
    x0 = 22
    p.e(f'<rect x="{x0 + 0.9}" y="{332 - 13.6}" width="{len(notes) * SL - 1.8:.2f}" height="33" rx="3" fill="#141519"/>')
    p.e(f'<rect x="{x0 + 3.5}" y="{332 - 13.6}" width="{len(notes) * SL - 7:.2f}" height="1.6" rx="0.8" fill="{dim(p.C)}"/>')
    for i, (name, lab) in enumerate(notes):
        p.jack(name, x0 + SL * (i + 0.5), 332, lab, 353.5)
    sync = [x for x in rest if x[1] in ("CLOCK", "RESET")]
    rest = [x for x in rest if x not in sync]
    xs = x0 + len(notes) * SL + 6.5          # the grey CLOCK / RESET tray, 2.2 mm (6.5 px) after the note tray
    if sync:
        p.e(f'<rect x="{xs + 0.9}" y="{332 - 13.6}" width="{len(sync) * SL - 1.8:.2f}" height="33" rx="3" fill="#141519"/>')
        p.e(f'<rect x="{xs + 3.5}" y="{332 - 13.6}" width="{len(sync) * SL - 7:.2f}" height="1.6" rx="0.8" fill="#5a5c66"/>')
        for i, (name, lab) in enumerate(sync):
            p.jack(name, xs + SL * (i + 0.5), 332, lab, 353.5)
        xs += len(sync) * SL
    for i, (name, lab) in enumerate(rest):
        p.jack(name, xs + 12 + 34 * (i + 0.5), 332, lab, 353.5)
    p.plate(256, 316, 120, 44)
    for i, lab in enumerate(outs):
        p.jack(f"OUT_{lab}", 292 + i * 48, 332, lab, 353.5, out=True)


def env_fx(p):
    p.plate(14, 270, 178, 42, True)
    p.title(103, 280, "ENVELOPE", 5.0, 2.0)
    p.plate(198, 270, 178, 42, True)
    p.title(287, 280, "FX", 5.0, 2.0)
    for i, (n, lab) in enumerate([("ATK", "ATK"), ("DEC", "DEC"), ("SUS", "SUS"), ("REL", "REL")]):
        p.knob(n, 14 + 178 / 4 * (i + 0.5), 294, R_TRIM, lab, 309.5, ring=False)
    for i, (n, lab) in enumerate([("DIST", "DIST"), ("CHOR", "CHORUS"), ("DLY", "DELAY"), ("REV", "REVERB")]):
        p.knob(n, 198 + 178 / 4 * (i + 0.5), 294, R_TRIM, lab, 309.5, ring=False)


def engine(p, n):
    """n knobs; their names and values follow MODE / LANG and are drawn by the widget."""
    p.plate(14, 184, 362, 82, True)
    p.title(195, 194.5, "ENGINE", 6.0)
    for k in range(n):
        x = 14 + 362 / n * (k + 0.5)
        p.knob(f"E{k}", x, 211)
        p.area(f"E_TEXT{k}", x - 362 / n / 2, 224.5, 362 / n, 15)
        p.jack(f"E_IN{k}", x, 253.5)


# ============================================================================ MATH (26 HP)
def math_panel():
    p = Panel("Math", "MATH", 26, "JIANT FM · BYTEBEAT / FLOATBEAT")
    p.display(14, 30, 362, 106)
    p.plate(14, 140, 362, 40)
    p.button("PREV", 32, 154, "◀ PRE")
    p.button("NEXT", 62, 154, "PRE ▶")
    p.button("CALL", 94, 154, "CALL", light=True)
    p.sep(112, 146, 174)
    p.block(118, 143, 84, 34)
    p.text(160, 151, "PLAY", 4.6, p.C, 800, "middle", 1.6)
    p.leds("PLAY", 141, 160.5, 38, ["KEYS", "THRU"], 172.5)
    p.block(208, 143, 116, 34)
    p.text(266, 151, "VOICE", 4.6, p.C, 800, "middle", 1.6)
    p.leds("VOICE", 228, 160.5, 38, ["POLY", "MONO", "LEGATO"], 172.5)
    p.knob("LEVEL", 352, 155, label="LEVEL", ly=175.5, v=0.82)
    engine(p, 6)
    env_fx(p)
    io_row(p, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL"), ("CLOCK", "CLOCK"), ("RESET", "RESET")])
    return p


PANELS = [math_panel()]

# ============================================================================ real
if not PREVIEW:
    for p in PANELS:
        open(os.path.join(ROOT, "res", f"{p.slug}.svg"), "w").write(p.svg())
    hdr = ["// Generated by tools/math_panel.py - do not edit", "// Rack pixels (1 HP = 15 px).", "#pragma once",
           "namespace mlayout {"] + [p.layout() for p in PANELS] + ["}  // namespace mlayout", ""]
    open(os.path.join(ROOT, "src", "math", "MathLayout.hpp"), "w").write("\n".join(hdr))
    print("written: res/Math.svg src/math/MathLayout.hpp")
    sys.exit(0)

# ============================================================================ preview
OUT = sys.argv[2]
CL = os.path.join(sys.argv[3], "res", "ComponentLibrary")
_cache = {}


def comp_svg(name, x, y, rot=0.0):
    fn = f"{CL}/{name}.svg"
    if fn not in _cache:
        src = open(fn).read()
        src = re.sub(r"<\?xml[^>]*\?>", "", src)
        src = re.sub(r"<!DOCTYPE[^>]*>", "", src)
        m = re.search(r"<svg[^>]*>", src)
        tag = m.group(0)
        w = float(re.search(r'\swidth="([0-9.]+)', tag).group(1))
        h = float(re.search(r'\sheight="([0-9.]+)', tag).group(1))
        if "mm" in re.search(r'\swidth="([^"]+)"', tag).group(1):
            w, h = w * 75 / 25.4, h * 75 / 25.4
        vb = re.search(r'viewBox="([^"]+)"', tag)
        body = src[m.end():src.rindex("</svg>")]
        body = re.sub(r"<sodipodi:namedview.*?(/>|</sodipodi:namedview>)", "", body, flags=re.S)
        body = re.sub(r"<metadata.*?</metadata>", "", body, flags=re.S)
        body = re.sub(r'\s(inkscape|sodipodi):[a-zA-Z-]+="[^"]*"', "", body)
        _cache[fn] = (vb.group(1) if vb else f"0 0 {w} {h}", w, h, body)
    vbs, w, h, body = _cache[fn]
    g = f'<svg x="{x - w / 2:.2f}" y="{y - h / 2:.2f}" width="{w:.2f}" height="{h:.2f}" viewBox="{vbs}">{body}</svg>'
    if rot:
        g = f'<g transform="rotate({rot:.1f} {x} {y})">{g}</g>'
    return g


def mono(p, x, y, s, size=5.6, color=DIM, anchor="start"):
    return (f'<text x="{x:.1f}" y="{y:.1f}" font-family="DejaVu Sans Mono" font-size="{size}" fill="{color}" '
            f'text-anchor="{anchor}">{s}</text>')


def bold(x, y, s, size, color, anchor="start", sp=0.5):
    return (f'<text x="{x:.1f}" y="{y:.1f}" font-family="Nunito, Inter" font-weight="800" font-size="{size}" fill="{color}" '
            f'text-anchor="{anchor}" letter-spacing="{sp}">{s}</text>')


def chips(x, y, names, active, w, h, color, gap=3, size=5.2):
    s = []
    for i, n in enumerate(names):
        act = i in active
        s.append(f'<rect x="{x:.1f}" y="{y}" width="{w}" height="{h}" rx="2.2" fill="{color if act else "#1a1b21"}"/>')
        s.append(bold(x + w / 2, y + h / 2 + size * 0.36, n, size, "#0a0a0d" if act else DIM, "middle", 0.3))
        x += w + gap
    return "".join(s)


MOCK = {
    "Math": dict(name="BYTE VAR", sub="· 2/12", info="BYTE · 8BIT · KEYS · POLY",
                 labels=["ALGO", "VAR", "MOTION", "GRIT", "BEND", "LOOP"],
                 values=["B17", "31%", "SYNC 1", "0%", "0%", "0%"]),
}

for p in PANELS:
    m = MOCK[p.slug]
    s = []
    for kind, name, x, y, kw in p.comp:
        rot = (-135 + kw.get("v", 0.5) * 270) if kind in ("knob", "trim") else 0
        if kind in ("input", "output"):
            s.append(comp_svg("PJ301M", x, y))
        elif kind == "knob":
            s.append(comp_svg("RoundSmallBlackKnob_bg", x, y) + comp_svg("RoundSmallBlackKnob", x, y, rot))
        elif kind == "trim":
            s.append(comp_svg("Trimpot_bg", x, y) + comp_svg("Trimpot", x, y, rot))
        elif kind in ("button", "lightbutton"):
            s.append(comp_svg("VCVBezel", x, y))
            if kind == "lightbutton":
                s.append(f'<circle cx="{x}" cy="{y}" r="6.6" fill="{p.C}"/>')
        elif kind == "leds":
            s.append(f'<circle cx="{x}" cy="{y}" r="3.6" fill="{p.C}"/>')
            for i in range(1, kw["n"]):
                s.append(f'<circle cx="{x + i * kw["dx"]}" cy="{y}" r="3.6" fill="#3d3f48"/>')
        elif kind == "area" and name.startswith("E_TEXT"):
            k = int(name[6:])
            cx = x + kw["w"] / 2
            s.append(bold(cx, y + 6.5, m["labels"][k], 5.3, TXT, "middle", 0.4))
            s.append(mono(p, cx, y + 13.5, m["values"][k], 5.0, p.C, "middle"))
        elif kind == "area" and name == "SCREEN":
            for i in range(30):
                v = ((i * 5 & i >> 1) | (i * 3 & i >> 2)) % 9
                s.append(f'<rect x="{x + 6 + i * 4.6:.1f}" y="{y + kw["h"] - 10 - v * 9}" width="3.6" height="{v * 9 + 4}" fill="{p.C}" opacity="{0.35 + v / 14:.2f}"/>')
        elif kind == "area" and name == "INFO":
            ix, iy, iw = x, y, kw["w"]
            s.append(bold(ix, iy + 20, m["name"], 13, "#f2f3f7", "start", 0.6))
            s.append(bold(ix + iw, iy + 20, m["sub"], 6, DIM, "end", 0.8))
            s.append(mono(p, ix, iy + 33, m["info"]))
            s.append(chips(ix, iy + 41, ["BYTE", "FLOAT"], {0}, 36, 14, p.C))
            s.append(chips(ix + 82, iy + 41, ["4BIT", "4B/2", "8BIT", "1BIT"], {2}, (iw - 82 - 9) / 4, 14, p.C))
            s.append(mono(p, ix, iy + 71, "TIME  free · 120 BPM · Q 17", 5.6, DIM))
            s.append(mono(p, ix, iy + 92, "ALGO B17  VAR 31%  MOTION SYNC 1", 5.2, p.C))
    for sx in (15, p.W - 30):
        for sy in (0, H - 15):
            s.append(comp_svg("ScrewBlack", sx + 7.5, sy + 7.5))
    svg = p.svg().replace("</svg>", "".join(s) + "</svg>")
    fn = os.path.join(OUT, p.slug)
    open(fn + ".svg", "w").write(svg)
    subprocess.run(["rsvg-convert", "-z", "3", fn + ".svg", "-o", fn + ".png"], check=True)
    print(fn + ".png")
