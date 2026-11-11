#!/usr/bin/env python3
"""The GENRA instruments' look (after GENRA MONO, GRUND, FUNKE and SCHMELZ) for the GENRA Test panels.

A panel is described as data (the header, the display, sections in rows of columns of knobs, lamps and fields, the
jacks) and laid out here: sections sized by what is in them, the panel as narrow as the content allows, the jacks at
the bottom in two rows (the GENRA strip and the CV first, the outputs on tiles last). Every printed text is checked
against every other: an overlap stops the script.

real  -> res/<Slug>.svg (text as paths) + src/<dir>/<Slug>Layout.hpp in Rack pixels (1 HP = 15 px), names as before
         (names ending in a number become arrays: E0, E1 .. -> E[n][2]); areas [x, y, w, h], lamps [x0, y, dx].
preview OUT.png -> the same with stand-in knobs, jacks and the module's texts, for checking by eye.
"""
import math
import os
import re
import subprocess
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H = 128.5
PX = 75 / 25.4  # Rack px per mm
BG, PLATE, LINE, TXT, DIM, DARK = "#17181d", "#212329", "#34363f", "#d9dbe2", "#8a8d99", "#07080a"
TITLE, RING, TRAY, SYNC_LINE = "#e6e7ec", "#4a4c57", "#141519", "#5a5c66"
R = {"large": 6.1, "knob": 4.8, "small": 3.84, "trim": 3.0, "mini": 3.0, "button": 4.6, "step": 2.6, "jack": 4.0}
ITEM_H = {"large": 18.6, "knob": 14.9, "small": 13.8, "button": 13.0, "step": 10.2, "mini": 12.0}
COL_W = {"large": 16.2, "knob": 12.8, "small": 11.4, "button": 12.8, "step": 7.4, "mini": 9.8}
CEN = {"large": 7.4, "knob": 5.8, "small": 4.8, "button": 4.8, "step": 3.0, "mini": 3.9}
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


def mix(a, b, f):
    A = [int(a[i:i + 2], 16) for i in (1, 3, 5)]
    B = [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return "#" + "".join(f"{round(B[i] + (A[i] - B[i]) * f):02x}" for i in range(3))


class Panel:
    def __init__(self, spec):
        self.s = spec
        self.acc = spec["acc"]
        self.out, self.mock, self.boxes = [], [], []
        self.comps = []  # (kind, name, x, y, extra) in mm

    # ---------------------------------------------------------------- primitives
    def e(self, s, mock=False):
        (self.mock if mock else self.out).append(s)

    def text(self, x, y, s, size=1.7, color=TXT, weight=600, anchor="middle", sp=0.3, mock=False):
        if not s:
            return
        gs, cmap, upm, hmtx = font(weight)
        sc = size / upm
        w = width(s, size, weight, sp)
        cx = x - (w / 2 if anchor == "middle" else (w if anchor == "end" else 0))
        if not mock:
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
            self.e(f'<path fill="{color}" d="{" ".join(paths)}"/>', mock)

    def add(self, kind, name, x, y, **kw):
        if name:
            self.comps.append((kind, name, x, y, kw))

    def area(self, name, x, y, w, h):
        self.add("area", name, x, y, w=w, h=h)

    def ring(self, x, y, r, hot):
        self.e(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r + 0.88:.2f}" fill="none" stroke="{self.acc if hot else RING}" '
               f'stroke-width="0.29" stroke-dasharray="0.29 0.73" stroke-linecap="round"/>')

    def knob(self, kind, name, x, y, label, dyn=None):
        self.add(kind, name, x, y)
        self.ring(x, y, R[kind], kind == "large")
        ly = y + R[kind] + 3.4
        if dyn:  # the module draws this label (it follows a mode)
            self.area(dyn, x - 6.5, ly - 1.7, 13.0, 2.4)
            self.text(x, ly, label, 1.7, TXT, 600, "middle", 0.3, mock=True)
        else:
            self.text(x, ly, label, 1.7, TXT, 600, "middle", 0.3)

    def button(self, name, x, y, label, light=True):
        self.add("lightbutton" if light else "button", name, x, y)
        self.text(x, y + R["button"] + 3.0, label, 1.6, TXT, 600, "middle", 0.3)

    def field(self, name, x, y, w, a="", b=""):
        self.area(name, x, y, w, 5.4)
        self.e(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="5.4" rx="0.9" fill="{DARK}" stroke="{LINE}" stroke-width="0.4"/>')
        self.e(f'<polyline points="{x + w - 4.2:.2f},{y + 2.1:.2f} {x + w - 3:.2f},{y + 3.4:.2f} {x + w - 1.8:.2f},{y + 2.1:.2f}" '
               f'fill="none" stroke="{self.acc}" stroke-width="0.4" stroke-linecap="round" stroke-linejoin="round"/>')
        self.text(x + 2, y + 3.75, a, 1.9, TXT, 700, "start", 0.35, mock=True)
        self.text(x + w - 6, y + 3.6, b, 1.4, DIM, 600, "end", 0.25, mock=True)

    def jack(self, name, x, y, label, out_=False):
        self.add("output" if out_ else "input", name, x, y)
        if out_:
            self.e(f'<rect x="{x - 4.6:.2f}" y="{y - 3.9:.2f}" width="9.2" height="10.6" rx="0.9" fill="{mix(self.acc, PLATE, 0.32)}"/>')
        if label.startswith("~"):  # the module draws it: "~AREA:mock"
            area, mock = label[1:].split(":", 1)
            self.area(area, x - 4.7, y + 4.0, 9.4, 2.2)
            self.text(x, y + 5.7, mock, 1.5, DIM, 600, "middle", 0.25, mock=True)
        else:
            self.text(x, y + 5.7, label, 1.5, TXT if out_ else DIM, 600, "middle", 0.25)

    def tray(self, x, y, n, line=None):
        w = n * SLOT
        self.e(f'<rect x="{x + 0.3:.2f}" y="{y - 4.6:.2f}" width="{w - 0.6:.2f}" height="11.2" rx="1" fill="{TRAY}"/>')
        if line:
            self.e(f'<rect x="{x + 1.2:.2f}" y="{y - 4.6:.2f}" width="{w - 2.4:.2f}" height="0.55" rx="0.27" fill="{line}"/>')

    def section(self, x, y, w, h, title, dyn=None):
        self.e(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" rx="1.2" fill="{PLATE}"/>')
        self.e(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="0.88" rx="0.44" fill="{self.acc}"/>')
        if dyn:
            self.area(dyn, x, y + 2.6, w, 3.0)
            self.text(x + w / 2, y + 5.0, title, 2.03, self.acc, 800, "middle", 0.75, mock=True)
        else:
            self.text(x + w / 2, y + 5.0, title, 2.03, self.acc, 800, "middle", 0.75)


# ---------------------------------------------------------------- sizes
def col_w(col):
    return max(COL_W[it[0]] for it in col)


def col_h(col):
    return sum(ITEM_H[it[0]] for it in col)


def lamps_w(lr):
    label, names = lr[1], lr[2]
    return (10.5 if label else 0) + len(names) * max(10.5, max(width(n, 1.45, 600, 0.25) for n in names) + 2.5)


def chips_w(c):
    return len(c[1]) * max(9.0, max(width(n, 1.45, 700, 0.2) for n in c[1]) + 2.4)


def sec_size(s):
    w = sum(col_w(c) for c in s.get("cols", [])) + 6
    for lr in s.get("lamps", []):
        w = max(w, lamps_w(lr) + 6)
    w = max(w, width(s["title"], 2.03, 800, 0.75) + 10, s.get("minw", 0))
    if s.get("field"):
        w = max(w, 34)
    for fr in s.get("fields", []):
        w = max(w, s.get("fieldw", 28) * len(fr) + 6)
    h = 8.2 + (7.5 if s.get("field") else 0) + 7.5 * len(s.get("fields", [])) + (s["screen"][1] + 1.6 if s.get("screen") else 0)
    h += max([col_h(c) for c in s.get("cols", [])] + [0])
    h += 9.0 * len(s.get("lamps", []))
    return w, h + 0.6


def jack_rows(spec, W):
    rows = [[]]
    avail = W - 12

    def wg(g):
        return len(g[1]) * (11.0 if g[0] == "out" else SLOT)

    def used(r):
        return sum(wg(g) for g in r) + 2.3 * max(0, len(r) - 1)

    groups = []
    if spec.get("strip"):
        groups.append(("strip", spec["strip"]))
    if spec.get("sync"):
        groups.append(("sync", spec["sync"]))
    for cv in spec.get("cvs", []):
        groups.append(("cv", cv))
    for g in groups:
        r = rows[-1]
        if used(r + [g]) <= avail:
            r.append(g)
        else:
            if g[0] == "cv" and not spec.get("nosplit"):
                room = int((avail - used(r) - (2.3 if r else 0)) / SLOT)
                if room >= 2:
                    r.append(("cv", g[1][:room]))
                    g = ("cv", g[1][room:])
            rows.append([g])
    if spec.get("outs"):
        outs = ("out", spec["outs"])
        if used(rows[-1] + [outs]) <= avail and (len(rows) > 1 or spec.get("jackrows", 2) == 1):
            rows[-1].append(outs)
        else:
            rows.append([outs])
    return [r for r in rows if r]


# ---------------------------------------------------------------- the panel
def build(spec):
    p = Panel(spec)
    rows = spec["rows"]
    need = 0
    for r in rows:
        need = max(need, sum(sec_size(s)[0] for s in r) + 2 * (len(r) - 1) + 6)
    hp = max(spec.get("minhp", 10), math.ceil(need / 5.08))
    if spec.get("hp"):
        hp = max(hp, spec["hp"])
    while True:
        W = hp * 5.08
        jr = jack_rows(spec, W)
        last = jr[-1]
        lw = sum(len(g[1]) * (11.0 if g[0] == "out" else SLOT) for g in last) + 2.3 * (len(last) - 1)
        if len(jr) <= spec.get("jackrows", 2) and lw <= W - 23:
            break
        hp += 1
        if hp > 120:
            sys.exit(f"{spec['name']}: the jacks do not fit in {spec.get('jackrows', 2)} rows")
    W = hp * 5.08
    p.W, p.hp = W, hp
    p.e(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.2f}mm" height="{H}mm" viewBox="0 0 {W:.2f} {H}">')
    p.e(f'<rect width="{W:.2f}" height="{H}" fill="{BG}"/>')

    # header: name, subtitle (shortened or dropped when the preset field needs the room), preset field, accent bar
    name = spec["name"]
    namew = width(name, 5.1, 800, 1.08)
    p.text(12.9, 7.6, name, 5.1, TITLE, 800, "start", 1.08)
    right = W - 23.5
    if spec.get("preset"):
        pw = 31 if W < 120 else 38
        px = W - 23.5 - pw
        p.field("PRESETF", px, 3.4, pw, spec["preset"])
        p.boxes.append((px, 3.4, px + pw, 8.8, "[preset]"))
        right = px
    if spec.get("link"):
        right = min(right, W - 17.0 - width("LINK", 1.6, 700, 0.4) - 2.0)
    for sub in (spec.get("sub", ""), spec.get("subshort", "")):
        if sub and 12.9 + namew + 3.4 + width(sub, 1.76, 600, 0.47) + 2 <= right:
            p.text(12.9 + namew + 3.4, 7.45, sub, 1.76, DIM, 600, "start", 0.47)
            break
    if spec.get("link"):  # an expander: LINK and its light where the accent bar would be
        if 12.9 + namew + 2.0 < W - 17.0 - width("LINK", 1.6, 700, 0.4):  # (narrow panels: the light alone)
            p.text(W - 17.0, 7.45, "LINK", 1.6, DIM, 700, "end", 0.4)
        p.area("LINK", W - 16.2, 4.3, 3.6, 3.6)
    elif 12.9 + namew + 2 < W - 21.5:
        p.e(f'<rect x="{W - 21.5:.2f}" y="5.55" width="8" height="1.08" rx="0.54" fill="{p.acc}"/>')

    # the display: a picture on the left (SCREEN) when the module has one, the rest INFO. Its height is what the
    # sections and the jacks leave: the sections always sit 2 mm above the jacks, flush on every panel.
    dy = 10.5
    jtop = H - 0.7 - len(jr) * 11.9
    secs = sum(max(sec_size(s)[1] for s in r) for r in rows) + 2 * (len(rows) - 1)
    lanes_h = 24.5 if spec.get("lanes") else 0
    dh = jtop - 2.0 - secs - lanes_h - 2.0 - dy
    spare = 0.0
    if spec.get("display") is False:  # an expander without a display: the sections start under the header and
        dy, dh = 10.5 - 2.0, 0.0      # what is left makes the sections taller, so they still end flush
        spare = (jtop - 2.0 - secs - lanes_h - 2.0 - dy) / len(rows)
        if spare < 0:
            sys.exit(f"{spec['name']}: the sections are {-spare * len(rows):.1f} mm too tall")
    elif dh < 13.0:
        sys.exit(f"{spec['name']}: the display would be {dh:.1f} mm, less than 13 mm (too much on the panel)")
    if dh > 0:
        p.area("DISPLAY", 3, dy, W - 6, dh)
    if dh > 0:
        p.e(f'<rect x="3" y="{dy}" width="{W - 6:.2f}" height="{dh}" rx="1.6" fill="{DARK}" stroke="{LINE}" stroke-width="0.4"/>')
    x0 = 5.5
    if dh > 0 and spec.get("screen"):
        sw = spec["screen"]  # its width (mm); height: the display's inner height
        p.area("SCREEN", 4.5, dy + 1.5, sw, dh - 3)
        p.e(f'<rect x="4.5" y="{dy + 1.5}" width="{sw:.2f}" height="{dh - 3:.2f}" rx="0.8" fill="{mix(p.acc, DARK, 0.15)}"/>', mock=True)
        x0 = 4.5 + sw + 3
    if dh > 0:
        p.area("INFO", x0, dy + 1.5, W - 3 - x0 - 2.5, dh - 3)
    for k, (x, y, w, h) in spec.get("dareas", {}).items():  # areas inside the display, relative to INFO's top right
        p.area(k, W - 5.5 - x if x >= 0 else x0 - x, dy + y, w, h)

    y = dy + dh + 2.0
    # lanes (optional): a row of pads under the display, each with a level trimmer and a trigger input below
    if spec.get("lanes"):
        ln = spec["lanes"]
        n = len(ln["names"])
        p.e(f'<rect x="3" y="{y:.2f}" width="{W - 6:.2f}" height="22.5" rx="1.2" fill="{PLATE}"/>')
        pitch = (W - 6) / n
        bw = min(14.6, pitch - 1.6)
        for i, nm in enumerate(ln["names"]):
            cx = 3 + pitch * (i + 0.5)
            p.area(f"{ln['pad']}{i}", cx - bw / 2, y + 1.6, bw, 6.6)
            p.e(f'<rect x="{cx - bw / 2:.2f}" y="{y + 1.6:.2f}" width="{bw:.2f}" height="6.6" rx="1" fill="#2b2d35"/>', mock=True)
            p.text(cx, y + 5.6, nm, 1.6, TXT, 700, "middle", 0.25, mock=True)
            ky = y + 13.2
            p.add("trim", f"{ln['knob']}{i}", cx - 3.7, ky)
            p.ring(cx - 3.7, ky, R["trim"], False)
            p.text(cx - 3.7, ky + 5.7, ln.get("knoblabel", "LVL"), 1.3, TXT, 600, "middle", 0.2)
            p.jack(f"{ln['jack']}{i}", cx + 3.9, ky, ln.get("jacklabel", "TRIG"))
        y += 22.5 + 2.0
    # sections
    for r in rows:
        sizes = [sec_size(s) for s in r]
        rh = max(h for _, h in sizes) + spare
        natural = sum(w for w, _ in sizes) + 2 * (len(r) - 1)
        extra = (W - 6 - natural) / len(r)
        x = 3
        for s, (sw, _) in zip(r, sizes):
            draw_section(p, s, x, y, sw + extra, rh)
            x += sw + extra + 2
        y += rh + 2
    if y - 2 > jtop - 0.3:
        sys.exit(f"{name}: the sections reach {y - 2:.1f} mm, the jacks start at {jtop:.1f} mm")
    draw_jacks(p, jr, W)
    p.check()
    return p


def draw_section(p, s, x, y, w, h):
    p.section(x, y, w, h, s["title"], s.get("dyntitle"))
    cy = y + 8.2
    if s.get("screen"):  # a small display of the section (the module draws it)
        sn, sh = s["screen"]
        p.area(sn, x + 3, cy - 1.7, w - 6, sh)
        p.e(f'<rect x="{x + 3:.2f}" y="{cy - 1.7:.2f}" width="{w - 6:.2f}" height="{sh:.2f}" rx="0.9" fill="{DARK}" stroke="{LINE}" stroke-width="0.4"/>')
        cy += sh + 1.6
    if s.get("field"):
        f = s["field"]
        p.field(f[0], x + 3, y + 6.5, w - 6, *f[1:])
        cy += 7.5
    for fr in s.get("fields", []):
        fw = (w - 6 - 2 * (len(fr) - 1)) / len(fr)
        for i, f in enumerate(fr):
            p.field(f[0], x + 3 + i * (fw + 2), cy - 1.7, fw, *f[1:])
        cy += 7.5
    cols = s.get("cols", [])
    if cols:
        tot = sum(col_w(c) for c in cols)
        extra = (w - 6 - tot) / len(cols)
        cx = x + 3
        for c in cols:
            cw = col_w(c) + extra
            yy = cy
            for it in c:
                kind, name, label = it[0], it[1], it[2]
                dyn = it[3] if len(it) > 3 else None
                if kind == "step":
                    p.add("step", name, cx + cw / 2, yy + CEN[kind])
                    p.text(cx + cw / 2, yy + CEN[kind] + R["step"] + 2.6, label, 1.4, TXT, 600, "middle", 0.2)
                elif kind == "button":
                    p.button(name, cx + cw / 2, yy + CEN[kind], label, light=not (len(it) > 3 and it[3] == "plain"))
                else:
                    p.knob(kind, name, cx + cw / 2, yy + CEN[kind], label, dyn)
                yy += ITEM_H[kind]
            cx += cw
        cy += max(col_h(c) for c in cols)
    for lr in s.get("lamps", []):
        name, label, names = lr[0], lr[1], lr[2]
        n = len(names)
        lw = lamps_w(lr)
        x0 = x + (w - lw) / 2 + (10.5 if label else 0)
        dx = (lw - (10.5 if label else 0)) / n
        if label:
            p.text(x0 - 2.0, cy + 2.15, label, 1.6, DIM, 700, "end", 0.3)
        p.add("leds", name, x0 + dx / 2, cy + 1.5, dx=dx, n=n)
        for i, nm in enumerate(names):
            lx = x0 + dx * (i + 0.5)
            p.e(f'<circle cx="{lx:.2f}" cy="{cy + 1.5:.2f}" r="1.25" fill="#0d0e11"/>')
            p.text(lx, cy + 5.6, nm, 1.45, TXT, 600, "middle", 0.25)
        cy += 9.0


def draw_jacks(p, rows, W):
    n = len(rows)
    top = H - 0.7 - n * 11.9
    p.e(f'<rect x="3" y="{top:.2f}" width="{W - 6:.2f}" height="{n * 11.9 + 0.2:.2f}" rx="1.2" fill="{PLATE}"/>')
    for ri, r in enumerate(rows):
        y = top + 4.8 + ri * 11.9
        last = ri == n - 1  # the bottom row stays clear of the screws
        x = 11.5 if last else 6.0
        right = W - 11.5 if last else W - 6
        tot = sum(len(g[1]) * (11.0 if g[0] == "out" else SLOT) for g in r) + 2.3 * (len(r) - 1)
        if ri > 0 and r[-1][0] == "out" and len(r) == 1:
            x = right - tot
        for gi, (kind, items) in enumerate(r):
            if kind == "out":
                if gi > 0 or len(r) > 1:
                    x = max(x, right - len(items) * 11.0)
                for i, (name, lb) in enumerate(items):
                    p.jack(name, x + 5.5 + i * 11.0, y, lb, True)
                x += len(items) * 11.0 + 2.3
            else:
                line = mix(p.acc, TRAY, 0.35) if kind == "strip" else (SYNC_LINE if kind == "sync" else None)
                p.tray(x, y, len(items), line)
                for i, (name, lb) in enumerate(items):
                    p.jack(name, x + SLOT / 2 + i * SLOT, y, lb)
                x += len(items) * SLOT + 2.3


def check(self):
    bad = []
    bx = self.boxes
    for i, a in enumerate(bx):
        for b in bx[i + 1:]:
            if a[0] < b[2] - 0.05 and b[0] < a[2] - 0.05 and a[1] < b[3] - 0.05 and b[1] < a[3] - 0.05:
                bad.append((a[4], b[4]))
    if bad:
        sys.exit(f"{self.s['name']}: texts overlap: {bad}")


Panel.check = check


# ---------------------------------------------------------------- output
def layout(p, ns_outer, ns, unit="px"):
    k = PX if unit == "px" else 1.0  # px: Rack pixels; mm: for panels whose code converts with mm2px()
    groups, order = {}, []
    for kind, name, x, y, kw in p.comps:
        if kind == "area":
            v = [x * k, y * k, kw["w"] * k, kw["h"] * k]
        elif kind == "leds":
            v = [x * k, y * k, kw["dx"] * k]
        else:
            v = [x * k, y * k]
        m = re.match(r"(.*?[A-Z_])(\d+)$", name)
        key = m.group(1) if m else name
        if key not in groups:
            groups[key] = []
            order.append(key)
        groups[key].append((int(m.group(2)) if m else 0, v))
    for key in groups:  # arrays in the order of their numbers
        idx = [i for i, _ in groups[key]]
        if idx and sorted(idx) != list(range(len(idx))):
            groups[key] = None  # not 0..n-1 (STEP1 .. STEP16, MIX1 MIX2): each keeps its own name
            continue
        groups[key] = [v for _, v in sorted(groups[key], key=lambda t: t[0])]
    fmt = lambda v: "{" + ", ".join((f"{a:.1f}f" if unit == "px" else f"{a:.3f}f") for a in v) + "}"
    L = [f"namespace {ns} {{", (f"static const float W = {p.hp * 15}.f;" if unit == "px" else f"static const float W = {p.W:.3f}f;"), f"static const int HP = {p.hp};"]
    for key in order:
        vs = groups[key]
        if vs is None:
            for kind, name, x, y, kw in p.comps:
                if re.fullmatch(re.escape(key) + r"\d+", name):
                    v = [x * k, y * k, kw["w"] * k, kw["h"] * k] if kind == "area" else [x * k, y * k]
                    L.append(f"static const float {name}[{len(v)}] = {fmt(v)};")
            continue
        arr = re.search(r"\d$", next(n for k, n, *_ in p.comps if n.startswith(key)))
        if len(vs) == 1 and not arr:
            L.append(f"static const float {key}[{len(vs[0])}] = {fmt(vs[0])};")
        else:
            L.append(f"static const float {key.rstrip('_')}[{len(vs)}][{len(vs[0])}] = {{{', '.join(fmt(v) for v in vs)}}};")
    L.append(f"}}  // namespace {ns}")
    return "\n".join(L)


def stand_ins(p):
    s = ['<defs><radialGradient id="kn" cx="0.4" cy="0.35" r="0.7"><stop offset="0" stop-color="#3a3c45"/>'
         '<stop offset="1" stop-color="#0c0d10"/></radialGradient><radialGradient id="jk" cx="0.4" cy="0.35" r="0.75">'
         '<stop offset="0" stop-color="#ffffff"/><stop offset="0.6" stop-color="#c9cbd1"/><stop offset="1" stop-color="#8d9098"/>'
         '</radialGradient></defs>']
    for kind, n, x, y, kw in p.comps:
        if kind in ("large", "knob", "small", "trim", "mini"):
            r = R[kind]
            s.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#050507"/><circle cx="{x}" cy="{y}" r="{r - 0.35:.2f}" fill="url(#kn)"/>'
                     f'<line x1="{x}" y1="{y - r * 0.35:.2f}" x2="{x}" y2="{y - r * 0.8:.2f}" stroke="#f2f2f5" stroke-width="{r * 0.16:.2f}" stroke-linecap="round"/>')
        elif kind == "step":
            s.append(f'<rect x="{x - 2.6}" y="{y - 2.6}" width="5.2" height="5.2" rx="0.8" fill="#3a3c45" stroke="#050507" stroke-width="0.5"/>')
        elif kind in ("button", "lightbutton"):
            s.append(f'<circle cx="{x}" cy="{y}" r="{R["button"]}" fill="#050507"/><circle cx="{x}" cy="{y}" r="{R["button"] - 0.7:.2f}" fill="#7a7c84"/>')
        elif kind in ("input", "output"):
            s.append(f'<circle cx="{x}" cy="{y}" r="3.15" fill="url(#jk)"/><circle cx="{x}" cy="{y}" r="2.35" fill="#e8e9ec"/><circle cx="{x}" cy="{y}" r="1.55" fill="#050505"/>')
        elif kind == "leds":
            s.append(f'<circle cx="{x}" cy="{y}" r="1.25" fill="{p.acc}"/>')
    for sx in (7.62, p.W - 7.62):
        for sy in (2.54, H - 2.54):
            s.append(f'<circle cx="{sx}" cy="{sy}" r="1.6" fill="#2a2b31" stroke="#9a9ca4" stroke-width="0.25"/>')
    return s


def write(p, slug, src_dir, ns_outer, ns, generator):
    if len(sys.argv) > 1 and sys.argv[1] == "preview":
        out = sys.argv[2]
        svg = out[:-4] + ".svg"
        open(svg, "w").write("\n".join(p.out + p.mock + stand_ins(p) + ["</svg>"]))
        subprocess.run(["rsvg-convert", "-w", str(int(p.W * 15.5)), svg, "-o", out], check=True)
        print(f"{slug}: {p.hp} HP -> {out}")
        return
    open(os.path.join(ROOT, "res", slug + ".svg"), "w").write("\n".join(p.out + ["</svg>"]))
    d = os.path.join(ROOT, "src", src_dir)
    os.makedirs(d, exist_ok=True)
    hdr = [f"// Generated by tools/{generator} - do not edit", "// Rack pixels (1 HP = 15 px).", "#pragma once"]
    if ns_outer:
        hdr.append(f"namespace {ns_outer} {{")
    hdr.append(layout(p, ns_outer, ns))
    if ns_outer:
        hdr.append(f"}}  // namespace {ns_outer}")
    open(os.path.join(d, f"{slug}Layout.hpp"), "w").write("\n".join(hdr) + "\n")
    print(f"{slug}: {p.hp} HP, written res/{slug}.svg src/{src_dir}/{slug}Layout.hpp")
