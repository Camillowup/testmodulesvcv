#!/usr/bin/env python3
"""GENRA TECHNO: the panel library (a copy of tools/tpanel.py in Camillowup/Technogen, for KRATZ in GENRA Test).

A panel script prints the static artwork (plates, lines, rings, labels, all text as paths in Inter) into res/<Name>.svg
and writes the positions of everything the module draws itself (knobs, buttons, fields, displays, jacks) into
src/<Name>Layout.hpp, in millimetres. preview() also draws those parts into docs/panels/<name>.png, so the picture
shows the module as Rack will.

Every printed text is checked against every other: an overlap stops the script. Look after docs/DESIGN.md of the
GENRA repo (dark GENRA look, colours by track).
"""
import os
import subprocess
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H = 128.5

BG, PLATE, LINE, TXT, DIM, DISP, STRIP = "#17181d", "#212329", "#34363f", "#d9dbe2", "#8a8d99", "#07080a", "#141519"
RED = "#ff4a4a"  # GENRA TECHNO's accent
DR, BA, ME, LE, PA = "#7b6cff", "#b8f23a", "#3dc9ff", "#5fe0c8", "#e0779e"  # drums, bass, stab, lead, atmo
YEL = "#e3e36a"
PART_COLS = ["#8a93a6", "#7b6cff", "#e3e36a", "#ff4a4a", "#3dc9ff", "#8a93a6"]
J = 9.4  # jack raster

FONT_DIR = "/usr/share/fonts/opentype/inter/"
FONTS = {600: "Inter-SemiBold.otf", 700: "Inter-Bold.otf", 800: "Inter-ExtraBold.otf", 900: "Inter-Black.otf"}
_f = {}

KNOB_D = {"large": 12.19, "medium": 9.60, "small": 7.68, "trim": 6.05}


def font(w):
    if w not in _f:
        f = TTFont(FONT_DIR + FONTS[w])
        _f[w] = (f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm, f["hmtx"])
    return _f[w]


def twidth(s, size, w=700, sp=0.15):
    gs, cmap, upm, hmtx = font(w)
    return sum(hmtx[cmap.get(ord(c), "space")][0] * size / upm + sp for c in s) - sp


def mix(c, a=0.28, base="#212329"):
    c = c.lstrip("#")
    b = base.lstrip("#")
    rgb = [int(c[i:i + 2], 16) for i in (0, 2, 4)]
    bb = [int(b[i:i + 2], 16) for i in (0, 2, 4)]
    return "#" + "".join(f"{int(bb[i] + (rgb[i] - bb[i]) * a):02x}" for i in range(3))


class Panel:
    def __init__(self, name, hp, title, subtitle, accent=RED):
        self.name, self.hp = name, hp
        self.W = hp * 5.08
        self.out, self.prev, self.boxes = [], [], []
        self.comps = []  # (kind, name, values)
        self.rect(0, 0, self.W, H, BG, 0)
        # the head: title, red bar, GENRA TECHNO and the subtitle
        tw = self.text(6, 10.0, title, 6.6, TXT, 900, "start", 0.8)
        self.rect(6 + tw + 1.6, 3.8, 0.8, 6.6, accent, 0)
        self.text(6 + tw + 4.0, 6.7, "GENRA TECHNO", 1.7, TXT, 700, "start", 0.4)
        self.text(6 + tw + 4.0, 10.0, subtitle, 1.3, DIM, 600, "start", 0.35)
        self.titleEnd = 6 + tw + 4.0 + max(twidth("GENRA TECHNO", 1.7, 700, 0.4), twidth(subtitle, 1.3, 600, 0.35))

    # ---- printed artwork
    def e(self, s):
        self.out.append(s)

    def p(self, s):  # preview only
        self.prev.append(s)

    def text(self, x, y, s, size=1.6, color=TXT, w=700, anchor="middle", sp=0.15, check=True, preview=False):
        gs, cmap, upm, hmtx = font(w)
        sc = size / upm
        tw = twidth(s, size, w, sp)
        cx = x - (tw / 2 if anchor == "middle" else (tw if anchor == "end" else 0))
        if check:
            self.boxes.append((cx, y - size * 0.74, cx + tw, y + 0.05, s))
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
            (self.p if preview else self.e)(f'<path fill="{color}" d="{" ".join(paths)}"/>')
        return tw

    def rect(self, x, y, w, h, fill, rx=1.0, stroke=None, sw=0.3, preview=False):
        st = f' stroke="{stroke}" stroke-width="{sw}"' if stroke else ""
        (self.p if preview else self.e)(
            f'<rect x="{x:.3f}" y="{y:.3f}" width="{w:.3f}" height="{h:.3f}" rx="{rx}" fill="{fill}"{st}/>')

    def section(self, x, y, w, h, title, col=RED, left=False):
        self.rect(x, y, w, h, PLATE, 1.2)
        self.rect(x, y, w, 0.6, col, 0.2)
        if left:
            self.text(x + 3, y + 4.4, title, 1.5, col, 800, "start", sp=0.6)
        else:
            self.text(x + w / 2, y + 4.2, title, 1.5, col, 800, sp=0.6)

    def ring(self, x, y, d, col):
        self.e(f'<circle cx="{x:.3f}" cy="{y:.3f}" r="{d / 2 + 1.1:.3f}" fill="none" stroke="{col}" '
               f'stroke-width="0.45" stroke-dasharray="0.25 0.75"/>')

    # ---- parts the module draws (recorded for the layout header, drawn in the preview)
    def comp(self, kind, name, *v):
        self.comps.append((kind, name, v))

    def knob(self, name, x, y, size="medium", ring=None, label=None, label_dy=None):
        d = KNOB_D[size]
        self.ring(x, y, d, ring or "#555a66")
        if label:
            self.text(x, y + d / 2 + (label_dy if label_dy is not None else 3.2), label, 1.35, DIM, 700)
        self.comp("knob", name, x, y)
        self.p(f'<circle cx="{x}" cy="{y}" r="{d / 2}" fill="#0e0f12"/><circle cx="{x}" cy="{y}" r="{d / 2 * 0.86}" '
               f'fill="#26282e"/><rect x="{x - 0.3}" y="{y - d / 2 + 0.7}" width="0.6" height="{d * 0.3}" rx="0.3" fill="#f2f3f7"/>')

    def button(self, name, x, y, w, h, label, fill="#2c2e36", tcol=TXT, size=1.45):
        self.comp("button", name, x, y, w, h)
        self.rect(x, y, w, h, fill, 1.0, preview=True)
        self.text(x + w / 2, y + h / 2 + size * 0.36, label, size, tcol, 700, check=False, preview=True)

    def field(self, name, x, y, w, h, value, col=TXT):
        self.comp("field", name, x, y, w, h)
        self.rect(x, y, w, h, DISP, 0.8, col, 0.35)  # the frame is printed
        self.text(x + 1.6, y + h / 2 + 0.55, value, 1.45, col, 700, "start", check=False, preview=True)
        self.p(f'<path d="M{x + w - 3.6},{y + h / 2 - 0.5} l2,0 l-1,1.3z" fill="{col}"/>')

    def display(self, name, x, y, w, h):
        self.comp("display", name, x, y, w, h)
        self.rect(x, y, w, h, DISP, 1.2, LINE)

    def area(self, name, x, y, w, h):
        """A region the module draws into (no frame printed)."""
        self.comp("display", name, x, y, w, h)

    def jack(self, name, x, y, label, plate=None, out=False):
        if plate:
            self.rect(x - 4.1, y - 4.3, 8.2, 11.3, plate, 1.0)
        self.text(x, y + 5.6, label, 1.3, TXT if plate else DIM, 700)
        self.comp("output" if out else "input", name, x, y)
        self.p(f'<circle cx="{x}" cy="{y}" r="3.15" fill="#c9ccd3"/><circle cx="{x}" cy="{y}" r="2.35" fill="#9a9da5"/>'
               f'<circle cx="{x}" cy="{y}" r="1.6" fill="#050506"/>')

    def strip(self, y=101.6):
        self.rect(3, y, self.W - 6, H - y - 1.3, STRIP, 1.4)

    # ---- writing
    def finish(self, preview_px=1800, preview_name=None):
        bad = []
        for i in range(len(self.boxes)):
            for j in range(i + 1, len(self.boxes)):
                a, b = self.boxes[i], self.boxes[j]
                if a[0] < b[2] - 0.05 and b[0] < a[2] - 0.05 and a[1] < b[3] - 0.05 and b[1] < a[3] - 0.05:
                    bad.append((a[4], b[4]))
        if bad:
            print(self.name, "texts overlap:", bad)
            sys.exit(1)
        for kind, name, v in self.comps:
            if v[0] > self.W - 2 or v[0] < 1:
                print(self.name, name, "past the edge")
                sys.exit(1)
        head = f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.W:.2f}mm" height="{H}mm" viewBox="0 0 {self.W:.3f} {H}">'
        os.makedirs(os.path.join(ROOT, "res"), exist_ok=True)
        open(os.path.join(ROOT, "res", self.name + ".svg"), "w").write(head + "".join(self.out) + "</svg>")
        # layout header
        lines = ["// generated by tools/" + os.path.basename(sys.argv[0]) + " — do not edit (positions in mm)",
                 "#pragma once", f"namespace {self.name.lower()}_layout {{", f"static const float WIDTH_HP = {self.hp};"]
        for kind, name, v in self.comps:
            vals = ", ".join(f"{x:.3f}f" for x in v)
            lines.append(f"static const float {name}[{len(v)}] = {{{vals}}}; // {kind}")
        lines.append("}")
        open(os.path.join(ROOT, "src", self.name + "Layout.hpp"), "w").write("\n".join(lines) + "\n")
        # preview
        os.makedirs(os.path.join(ROOT, "docs", "panels"), exist_ok=True)
        pv = os.path.join(ROOT, "docs", "panels", (preview_name or self.name.lower()))
        open(pv + ".svg", "w").write(head + "".join(self.out) + "".join(self.prev) + "</svg>")
        subprocess.run(["rsvg-convert", "-w", str(preview_px), pv + ".svg", "-o", pv + ".png"], check=True)
        os.remove(pv + ".svg")
        print(self.name, f"{self.hp} HP ok")
