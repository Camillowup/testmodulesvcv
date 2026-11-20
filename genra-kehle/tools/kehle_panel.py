#!/usr/bin/env python3
"""KEHLE's panel (copied from GENRA Neon's tools/neon_panel.py; KEHLE is a test plugin of its own for now).
The panels of GENRA NEON (WELLE, NEBEL) in the NEON-TERRAIN look: a framed display (the module draws the picture),
sections in glass frames with pink tabs, glitch title. Every CV input sits right under its knob (2.1.1: the panels got
narrower); at the bottom only the GENRA strip, the clock, audio and the outputs. Text becomes paths (fontTools, Inter),
so Rack's SVG renderer shows it.

    neon_panel.py real      -> res/Welle.svg, res/Nebel.svg, src/welle/WelleLayout.hpp, src/nebel/NebelLayout.hpp
    neon_panel.py preview   -> docs/panels/welle.png, docs/panels/nebel.png (with stand-ins for knobs, jacks, display)
WOGE, PULS, GLAS, FLUT: tools/neon_panel_classic.py (44 HP, CV jacks in the bottom rows).
"""
import math
import os
import random
import subprocess
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H = 128.5
W44 = 44 * 5.08
C1, C2, C3 = "#ff2bd6", "#22e6ff", "#8b5bff"
LBL, LBL2 = "#d6ccff", "#a99bd6"
GR, SY, TR, CV, AU = C2, "#b8b2d8", "#ff8ad8", C3, "#5ce0a0"
COL, PAD, SGAP, RGAP = 11.0, 1.6, 2.0, 1.0   # column, section padding, between sections, between rows
KR = 3.8                                       # knob radius
FH = 5.0                                       # field height (in the band beside the title)
JS, GAP = 8.8, 2.0                             # bottom jacks: spacing, between groups
BOX_TOP = 115.6                                # the bottom jack box

FONT_DIR = "/usr/share/fonts/opentype/inter/"
FONTS = {600: "Inter-SemiBold.otf", 700: "Inter-Bold.otf", 800: "Inter-ExtraBold.otf", 900: "Inter-BlackItalic.otf"}
_fonts = {}


def font(w):
    if w not in _fonts:
        f = TTFont(FONT_DIR + FONTS[w])
        _fonts[w] = (f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm, f["hmtx"])
    return _fonts[w]


def twidth(s, size, weight, sp):
    gs, cmap, upm, hmtx = font(weight)
    return sum(hmtx[cmap.get(ord(c), "space")][0] * size / upm + sp for c in s) - sp


class Out:
    def __init__(self):
        self.o, self.boxes = [], []

    def e(self, s):
        self.o.append(s)

    def text(self, x, y, s, size, color, weight=700, anchor="middle", sp=0.3, opacity=1.0, check=True):
        gs, cmap, upm, hmtx = font(weight)
        sc = size / upm
        w = twidth(s, size, weight, sp)
        cx = x - (w / 2 if anchor == "middle" else (w if anchor == "end" else 0))
        if check:
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
            op = f' fill-opacity="{opacity}"' if opacity < 1 else ""
            self.e(f'<path fill="{color}"{op} d="{" ".join(paths)}"/>')

    def check(self, name):
        b = self.boxes
        for i in range(len(b)):
            for j in range(i + 1, len(b)):
                p, q = b[i], b[j]
                if p[0] < q[2] - 0.05 and q[0] < p[2] - 0.05 and p[1] < q[3] - 0.05 and q[1] < p[3] - 0.05:
                    sys.exit(f"{name}: '{p[4]}' overlaps '{q[4]}'")


def cut(x0, y0, x1, y1, c=2.0):
    return f"M{x0:.2f} {y0:.2f} H{x1 - c:.2f} L{x1:.2f} {y0 + c:.2f} V{y1:.2f} H{x0 + c:.2f} L{x0:.2f} {y1 - c:.2f} Z"


def corners(o, x0, y0, x1, y1, col, L=2.4, w=0.35):
    for (cx, cy, dx, dy) in ((x0, y0, 1, 1), (x1, y1, -1, -1)):
        o.e(f'<path d="M{cx:.2f} {cy + dy * L:.2f} V{cy:.2f} H{cx + dx * L:.2f}" fill="none" stroke="{col}" stroke-width="{w * 2.6:.2f}" stroke-opacity="0.18"/>')
        o.e(f'<path d="M{cx:.2f} {cy + dy * L:.2f} V{cy:.2f} H{cx + dx * L:.2f}" fill="none" stroke="{col}" stroke-width="{w}"/>')


def hexagon(x, y, r):
    return " ".join(f"{x + r * math.cos(math.pi / 3 * k + math.pi / 6):.2f},{y + r * math.sin(math.pi / 3 * k + math.pi / 6):.2f}" for k in range(6))



def col_w(c):
    if c[0] == "lamps":
        return len(c[2]) * 5.8 + 1.0
    if c[0] == "steps":
        return c[2] * 4.8 + 1.0
    return COL


def sec_w(sec):
    return sum(col_w(c) for c in sec["cols"]) + 2 * PAD


def sec_h(sec):
    return 1.4 + 5.0 + 10.6 + 3.4


def band_h(row):
    """The band above a row: the title tabs, and the fields beside them when a section has some."""
    return 5.4 if any(s.get("fields") for s in row) else 2.4


def build(S):
    """The panel (static parts) and the layout (where the module puts its widgets)."""
    rows = S["rows"]
    need = max(sum(sec_w(s) for s in r) + SGAP * (len(r) - 1) for r in rows) + 7.0
    hp = max(math.ceil(need / 5.08), S.get("hp", 0))
    W = hp * 5.08
    S["W"] = W
    o = Out()
    lay, pos = [], {}  # pos: jack name -> (x, y, colour)
    o.e(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.2f}mm" height="{H}mm" viewBox="0 0 {W:.2f} {H}">')
    o.e('<defs><linearGradient id="bg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#05020c"/>'
        '<stop offset="0.45" stop-color="#140630"/><stop offset="1" stop-color="#07030f"/></linearGradient>'
        f'<linearGradient id="edge" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="{C1}"/>'
        f'<stop offset="0.5" stop-color="{C3}"/><stop offset="1" stop-color="{C2}"/></linearGradient></defs>')
    o.e(f'<rect width="{W:.2f}" height="{H}" fill="url(#bg)"/>')
    # the rows from the bottom up: the display gets what is left
    rows_h = sum(max(sec_h(s) for s in r) + band_h(r) for r in rows) + RGAP * (len(rows) - 1)
    top = BOX_TOP - 2.0 - rows_h
    dy0, dy1 = 12.5, top - 1.6
    if dy1 - dy0 < 15.0:
        sys.exit(f"{S['name']}: the display would be only {dy1 - dy0:.1f} mm")
    o.e(f'<path d="{cut(4, dy0, W - 4, dy1, 3)}" fill="#05020c"/>')
    lay.append(("DISPLAY", [4.0, dy0, W - 8.0, dy1 - dy0]))
    o.e(f'<path d="{cut(4, dy0, W - 4, dy1, 3)}" fill="none" stroke="url(#edge)" stroke-width="1.2" stroke-opacity="0.22"/>')
    o.e(f'<path d="{cut(4, dy0, W - 4, dy1, 3)}" fill="none" stroke="url(#edge)" stroke-width="0.45"/>')
    corners(o, 5.2, dy0 + 1.2, W - 5.2, dy1 - 1.2, C2, 3.2, 0.45)
    for k in range(1, 20):
        x = 18 + k * (W - 36) / 20
        o.e(f'<line x1="{x:.2f}" y1="{dy1}" x2="{x:.2f}" y2="{dy1 - (1.0 if k % 5 else 1.6):.2f}" stroke="{C2}" stroke-width="0.2" stroke-opacity="0.7"/>')
    # header
    for dx, col, op in ((-0.7, C1, 0.9), (0.7, C2, 0.9), (0.0, "#ffffff", 1.0)):
        o.text(8 + dx, 10.4, S["name"], 8.6, col, 900, "start", 1.4, op, check=dx == 0)
    for gx, gy, gw in ((9, 5.9, 20), (24, 8.2, 13), (35, 4.6, 9)):
        o.e(f'<rect x="{gx}" y="{gy}" width="{gw}" height="0.55" fill="#05020c"/>')
        o.e(f'<rect x="{gx + 2}" y="{gy + 0.55}" width="{gw * 0.6:.1f}" height="0.3" fill="{C2}" fill-opacity="0.8"/>')
    px = W - 58.0
    sx = S.get("subx", 53)
    o.text(sx, 6.0, S["sub"], 1.75, C2, 700, "start", 0.5)
    o.text(sx, 8.9, S.get("brand", "GENRA // NEON PUNK"), 1.5, "#7d6aa8", 600, "start", 0.45)
    for i, nm in enumerate(("UNDO", "INIT") if S.get("undo_header", True) else ()):
        bx = px - 22.0 + i * 10.6
        o.e(f'<path d="M{bx:.2f} 3.6 h7.8 l1.4 1.4 v4.2 h-9.2 z" fill="#1a0a2e" stroke="{C3}" stroke-width="0.3"/>')
        o.text(bx + 4.6, 7.8, nm, 1.45, "#ffffff", 800, "middle", 0.3)
        lay.append((nm, [bx, 3.6, 9.2, 5.6]))
    o.e(f'<path d="M{px:.2f} 3.0 h47 l3 3 v4.4 h-47 l-3 -3 z" fill="#0a0518" stroke="{C1}" stroke-width="0.3"/>')
    o.e(f'<polyline points="{px + 45:.2f},5.6 {px + 46.4:.2f},7 {px + 47.8:.2f},5.6" fill="none" stroke="{C2}" stroke-width="0.4"/>')
    lay.append(("PRESETF", [px + 2.0, 3.0, 42.0, 7.4]))
    # sections, row by row
    y = top
    for r in rows:
        hb = band_h(r)
        y += hb
        rh = max(sec_h(s) for s in r)
        total = sum(sec_w(s) for s in r) + SGAP * (len(r) - 1)
        stretch = (W - 7.0 - total) / len(r)  # the rows fill the width
        x = 3.5
        for sec in r:
            w = sec_w(sec) + stretch
            x0, x1, y0, y1 = x, x + w, y, y + rh
            o.e(f'<path d="{cut(x0, y0, x1, y1)}" fill="#0a0618" fill-opacity="0.8"/>')
            o.e(f'<path d="{cut(x0, y0, x1, y1)}" fill="none" stroke="#3a2b66" stroke-width="0.3"/>')
            corners(o, x0, y0, x1, y1, C2, 2.2, 0.35)
            tw = twidth(sec["title"], 1.7, 800, 0.5) + 2.6
            o.e(f'<path d="M{x0:.2f} {y0:.2f} V{y0 - hb:.2f} H{x0 + tw:.2f} L{x0 + tw + 2.4:.2f} {y0:.2f} Z" fill="#2a0f3f" stroke="{C1}" stroke-width="0.3"/>')
            o.text(x0 + 1.6, y0 - hb / 2 + 0.65, sec["title"], 1.7, "#ffffff", 800, "start", 0.5)
            fy = y0 - hb + 0.2
            if sec.get("fields"):  # beside the title, in the band
                fx = x0 + tw + 3.6
                fw_all = x1 - 0.6 - fx
                for name, frac in sec["fields"]:
                    fw = fw_all * frac - (1.0 if frac < 1 else 0.0)
                    if fw < 12.0:
                        sys.exit(f"{S['name']}: field {name} only {fw:.1f} mm")
                    o.e(f'<path d="M{fx:.2f} {fy:.2f} h{fw - 1.6:.2f} l1.6 1.6 v{FH - 1.6:.2f} h-{fw:.2f} z" fill="#120a28" stroke="{C3}" stroke-width="0.25"/>')
                    o.e(f'<polyline points="{fx + fw - 4:.2f},{fy + 2.0:.2f} {fx + fw - 3:.2f},{fy + 3.0:.2f} {fx + fw - 2:.2f},{fy + 2.0:.2f}" fill="none" stroke="{C2}" stroke-width="0.3"/>')
                    lay.append((name, [fx, fy, fw, FH]))
                    fx += fw + 2.0
            ky = y0 + 1.4 + 5.0
            jy = ky + 10.6
            cw_extra = stretch / len(sec["cols"])
            cx = x0 + PAD
            for c in sec["cols"]:
                cw = col_w(c) + cw_extra
                mx = cx + cw / 2
                kind = c[0]
                if kind == "knob":
                    _, name, label, big, cv = c
                    o.text(mx, ky + KR + 2.9, label, 1.65, LBL if big else LBL2, 700, "middle", 0.3)
                    lay.append((name, [mx, ky, KR, float(big)]))
                    if cv:
                        o.e(f'<polygon points="{hexagon(mx, jy, 2.9)}" fill="#141024" stroke="{CV}" stroke-width="0.3"/>')
                        pos[cv] = (mx, jy, CV)
                elif kind == "jack":  # a CV for the field above (generator, chord, texture)
                    _, cv, label = c
                    o.text(mx, ky - 0.4, label, 1.6, LBL2, 700, "middle", 0.3)
                    o.text(mx, ky + 2.6, "CV", 1.4, "#6d5c99", 700, "middle", 0.3)
                    o.e(f'<line x1="{mx:.2f}" y1="{ky + 3.6:.2f}" x2="{mx:.2f}" y2="{jy - 3.4:.2f}" stroke="{CV}" stroke-width="0.25" stroke-opacity="0.6"/>')
                    o.e(f'<polygon points="{hexagon(mx, jy, 2.9)}" fill="#141024" stroke="{CV}" stroke-width="0.3"/>')
                    pos[cv] = (mx, jy, CV)
                elif kind == "lamps":
                    _, name, names = c
                    dx = (cw - 1.0) / len(names)
                    lx0 = cx + 0.5 + dx / 2
                    for i, nm in enumerate(names):
                        o.text(lx0 + i * dx, ky + 4.2, nm, 1.45, LBL2, 700, "middle", 0.15)
                    lay.append((name, [lx0, ky, dx, float(len(names))]))
                elif kind == "steps":  # PULS: the mask steps, frames the module lights (layout: x, y, pitch, h, n)
                    _, name, n = c
                    pitch = (cw - 1.0) / n
                    sx0, sy0, sh = cx + 0.5, ky - 3.4, 11.6
                    for i in range(n):
                        sx = sx0 + i * pitch
                        o.e(f'<path d="M{sx + 0.3:.2f} {sy0:.2f} h{pitch - 1.6:.2f} l1.0 1.0 v{sh - 1.0:.2f} h-{pitch - 0.6:.2f} z" fill="#120a28" stroke="{C1 if i % 4 == 0 else "#5b3a7a"}" stroke-width="0.25"/>')
                        o.text(sx + pitch / 2, sy0 + sh + 1.9, str(i + 1), 1.35, LBL2, 700, "middle", 0.1)
                    lay.append((name, [sx0, sy0, pitch, sh, float(n)]))
                elif kind == "btn":
                    _, name, label, trig = c
                    bw = min(cw - 1.2, 11.0)
                    o.e(f'<path d="M{mx - bw / 2:.2f} {ky - 3:.2f} h{bw - 2:.2f} l2 2 v4 h-{bw:.2f} z" fill="#1a0a2e" stroke="{C1}" stroke-width="0.35"/>')
                    o.text(mx, ky + 0.6, label, 1.5, "#ffffff", 800, "middle", 0.2)
                    lay.append((name, [mx - bw / 2, ky - 3, bw, 6.0]))
                    if trig:
                        o.text(mx, ky + 6.0, "TRIG", 1.4, "#6d5c99", 700, "middle", 0.3)
                        o.e(f'<polygon points="{hexagon(mx, jy, 2.9)}" fill="#141024" stroke="{TR}" stroke-width="0.3"/>')
                        pos[trig] = (mx, jy, TR)
                cx += cw
            x = x1 + SGAP
        y += rh + RGAP
    # the bottom: GENRA strip, clock, audio; the outputs on the right
    o.e(f'<path d="{cut(3, BOX_TOP, W - 3, H - 1.2, 2)}" fill="#04020a" stroke="#24183d" stroke-width="0.3"/>')
    jy = BOX_TOP + 4.6
    x = 13.0
    for col, items in S["bottom"]:
        o.e(f'<rect x="{x - 4.3:.2f}" y="{jy - 3.6:.2f}" width="{len(items) * JS:.2f}" height="9.4" rx="0.8" fill="none" stroke="{col}" stroke-width="0.25" stroke-opacity="0.75"/>')
        for name, lb in items:
            o.e(f'<polygon points="{hexagon(x, jy, 3.0)}" fill="#141024" stroke="{col}" stroke-width="0.3"/>')
            o.text(x, jy + 4.6, lb, 1.45, LBL2, 700, "middle", 0.15)
            pos[name] = (x, jy, col)
            x += JS
        x += GAP
    outs = []
    ox = W - 13.0 - (len(S["outs"]) - 1) * JS
    if ox - 6.0 < x - GAP:
        sys.exit(f"{S['name']}: the outputs do not fit beside the strip")
    if S["outs"]:  # GLANZ has none
        o.e(f'<rect x="{ox - 4.6:.2f}" y="{jy - 3.6:.2f}" width="{(len(S["outs"]) - 1) * JS + 9.2:.2f}" height="9.4" rx="0.8" fill="{C1}" fill-opacity="0.07" stroke="{C1}" stroke-width="0.25"/>')
    for i, (name, lb) in enumerate(S["outs"]):
        x = ox + i * JS
        o.e(f'<polygon points="{hexagon(x, jy, 3.1)}" fill="#141024" stroke="{C1}" stroke-width="0.3"/>')
        o.text(x, jy + 4.6, lb, 1.45, "#ffffff", 700, "middle", 0.2)
        outs.append((name, x, jy, C1))
    if S["outs"]:
        o.text(ox - 6.2, jy + 0.5, "OUT", 1.5, C1, 800, "end", 0.4)
    for sx in (7.62, W - 7.62):
        for sy in (2.54, H - 2.54):
            o.e(f'<circle cx="{sx:.2f}" cy="{sy:.2f}" r="1.5" fill="#0b0716" stroke="#5b4a8a" stroke-width="0.25"/>')
    missing = [n for n in S["inputs"] if n not in pos]
    extra = [n for n in pos if n not in S["inputs"]]
    if missing or extra:
        sys.exit(f"{S['name']}: inputs missing {missing}, not in the list {extra}")
    ins = [(n, pos[n][0], pos[n][1], pos[n][2]) for n in S["inputs"]]
    o.check(S["name"])
    print(f"{S['name']}: {hp} HP, display {dy1 - dy0:.1f} mm")
    return o, lay, ins, outs

def header(S, lay, ins, outs, ns):
    h = [f"// Generated by tools/neon_panel.py - do not edit (mm; points [x, y], rects [x, y, w, h], knobs [x, y, r, big]).",
         "#pragma once", f"namespace {ns} {{", f"static const float PANEL_WIDTH = {S["W"]:.3f}f;"]
    for name, v in lay:
        h.append(f"static const float {name}[{len(v)}] = {{{', '.join(f'{x:.3f}f' for x in v)}}};")
    h.append(f"static const int NUM_JACK_IN = {len(ins)};")
    h.append("static const float JACK_IN[][2] = {" + ", ".join(f"{{{x:.3f}f, {y:.3f}f}}" for _, x, y, _ in ins) + "};  // " +
             " ".join(n for n, _, _, _ in ins))
    h.append("static const unsigned JACK_IN_COL[] = {" + ", ".join("0x" + c[1:] for _, _, _, c in ins) + "};")
    h.append(f"static const int NUM_JACK_OUT = {len(outs)};")
    if outs:  # (GLANZ has no outputs: no zero-size array)
        h.append("static const float JACK_OUT[][2] = {" + ", ".join(f"{{{x:.3f}f, {y:.3f}f}}" for _, x, y, _ in outs) + "};  // " +
                 " ".join(n for n, _, _, _ in outs))
    h.append(f"}}  // namespace {ns}")
    return "\n".join(h) + "\n"



# ---------------------------------------------------------------- preview stand-ins
def stand_ins(S, lay):
    W = S["W"]
    s = ['<defs><radialGradient id="kn" cx=".38" cy=".3" r=".8"><stop offset="0" stop-color="#2c2440"/>'
         '<stop offset="1" stop-color="#050309"/></radialGradient></defs>']
    rnd = random.Random(3)
    for name, v in lay:
        if len(v) == 4 and name not in ("DISPLAY", "PRESETF", "UNDO", "INIT") and v[2] == KR and v[3] in (0.0, 1.0, 2.0):
            x, y, r, big = v
            val = rnd.uniform(0.2, 0.8)
            a0, a1 = math.radians(-135), math.radians(-135 + 270 * val)
            rr = r + 1.2
            col = C1 if big else C2
            sw = 0.7 if big == 1 else 0.45
            sx, sy = x + rr * math.sin(a0), y - rr * math.cos(a0)
            ex, ey = x + rr * math.sin(a1), y - rr * math.cos(a1)
            tx, ty = x + rr * math.sin(math.radians(135)), y - rr * math.cos(math.radians(135))
            s.append(f'<path d="M{sx:.2f} {sy:.2f} A{rr} {rr} 0 1 1 {tx:.2f} {ty:.2f}" fill="none" stroke="#24183d" stroke-width="{sw}"/>')
            s.append(f'<path d="M{sx:.2f} {sy:.2f} A{rr} {rr} 0 {1 if a1 - a0 > math.pi else 0} 1 {ex:.2f} {ey:.2f}" fill="none" stroke="{col}" stroke-width="{sw}" stroke-linecap="round"/>')
            s.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="url(#kn)" stroke="#3a2d5c" stroke-width="0.2"/>')
            s.append(f'<line x1="{x + r * .3 * math.sin(a1):.2f}" y1="{y - r * .3 * math.cos(a1):.2f}" x2="{x + r * .85 * math.sin(a1):.2f}" y2="{y - r * .85 * math.cos(a1):.2f}" stroke="#fff" stroke-width="{r * .13:.2f}" stroke-linecap="round"/>')
    return s


def write(S, slug, ns, sub):
    o, lay, ins, outs = build(S)
    if len(sys.argv) > 1 and sys.argv[1] == "preview":
        svg = os.path.join(ROOT, "docs", "panels", slug.lower() + ".svg")
        png = svg[:-4] + ".png"
        open(svg, "w").write("\n".join(o.o + stand_ins(S, lay)  + ["</svg>"]))
        subprocess.run(["rsvg-convert", "-w", "2400", svg, "-o", png], check=True)
        os.remove(svg)
        print(f"{slug}: preview {png}")
        return
    open(os.path.join(ROOT, "res", slug + ".svg"), "w").write("\n".join(o.o + ["</svg>"]))
    open(os.path.join(ROOT, "src", sub, slug + "Layout.hpp"), "w").write(header(S, lay, ins, outs, ns))
    print(f"{slug}: res/{slug}.svg, src/{sub}/{slug}Layout.hpp ({len(ins)} in, {len(outs)} out)")



KEHLE = dict(
    name="KEHLE", sub="VOCODER", hp=24, subx=44.5, brand="GENRA // NEON", undo_header=False,
    rows=[
        [dict(title="VOICE", fields=[("CHARF", 1.0)],
              cols=[("knob", "BANDS", "BANDS", 0, None), ("knob", "FORMANT", "FORMANT", 1, "C_FORM"),
                    ("knob", "RESO", "RESO", 0, "C_RESO"), ("knob", "SIBIL", "SIBIL", 0, "C_SIBIL")]),
         dict(title="CARRIER", fields=[("CARF", 1.0)],
              cols=[("knob", "OCT", "OCT", 0, None), ("knob", "ENS", "ENSEMBLE", 0, "C_ENS")])],
        [dict(title="ENV", cols=[("knob", "ATK", "ATTACK", 0, None), ("knob", "REL", "RELEASE", 0, "C_REL")]),
         dict(title="OUT", cols=[("knob", "MIX", "DRY", 0, "C_MIX"), ("knob", "LEVEL", "LEVEL", 0, None)]),
         dict(title="PLAY", cols=[("btn", "FREEZE", "FREEZE", "T_FREEZE"), ("btn", "HOLD", "HOLD", None),
                                  ("btn", "UNDO", "UNDO", None), ("btn", "INIT", "INIT", None)])],
    ],
    bottom=[(AU, [("CAR_L", "CAR L"), ("CAR_R", "CAR R")]), (TR, [("MOD", "MOD")]),
            (GR, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL")])],
    outs=[("O_ENV", "ENV"), ("O_L", "L"), ("O_R", "R")],
    # the module's InputId order
    inputs=["CAR_L", "CAR_R", "MOD", "VOCT", "GATE", "VEL", "T_FREEZE", "C_FORM", "C_RESO", "C_SIBIL", "C_ENS", "C_REL",
            "C_MIX"],
)

MODS = {"kehle": (lambda: KEHLE, "Kehle", "kl")}
for key in (sys.argv[2:] or list(MODS)):  # kehle_panel.py real|preview
    spec, slug, ns = MODS[key]
    write(spec(), slug, ns, key)
