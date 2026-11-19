#!/usr/bin/env python3
"""The panels of GENRA NEON (WELLE, NEBEL, WOGE, PULS, GLAS, FLUT) in the NEON-TERRAIN look: a framed display (the module draws the picture),
sections in glass frames with pink tabs, glitch title, knob labels, jacks in coloured groups. Text becomes paths
(fontTools, Inter), so Rack's SVG renderer shows it.

    neon_panel.py real      -> res/<Name>.svg, src/<name>/<Name>Layout.hpp for every module
    neon_panel.py preview   -> docs/panels/<name>.png (with stand-ins for knobs, jacks, display)
A module may set: disp_y1 (the display's lower edge), r1 / r2 (the two section rows), fy (the fields' row),
steps (a row of step frames: name, x, y, width, height, count).
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
R1, R2 = (50.0, 87.0), (90.6, 105.4)
FY, FH = 51.6, 5.0
JY1, JY2, JS, GAP = 111.6, 121.8, 9.2, 2.3

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


def build(S):
    """The panel (static parts) and the layout (where the module puts its widgets)."""
    W = S["W"]
    r1, r2, fy = S.get("r1", R1), S.get("r2", R2), S.get("fy", FY)
    dy1 = S.get("disp_y1", 46.4)
    o = Out()
    lay = []  # (name, values)
    o.e(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.2f}mm" height="{H}mm" viewBox="0 0 {W:.2f} {H}">')
    o.e('<defs><linearGradient id="bg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#05020c"/>'
        '<stop offset="0.45" stop-color="#140630"/><stop offset="1" stop-color="#07030f"/></linearGradient>'
        f'<linearGradient id="edge" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="{C1}"/>'
        f'<stop offset="0.5" stop-color="{C3}"/><stop offset="1" stop-color="{C2}"/></linearGradient></defs>')
    o.e(f'<rect width="{W:.2f}" height="{H}" fill="url(#bg)"/>')
    # display frame (the module draws inside)
    o.e(f'<path d="{cut(4, 13, W - 4, dy1, 3)}" fill="#05020c"/>')
    lay.append(("DISPLAY", [4.0, 13.0, W - 8.0, dy1 - 13.0]))
    o.e(f'<path d="{cut(4, 13, W - 4, dy1, 3)}" fill="none" stroke="url(#edge)" stroke-width="1.2" stroke-opacity="0.22"/>')
    o.e(f'<path d="{cut(4, 13, W - 4, dy1, 3)}" fill="none" stroke="url(#edge)" stroke-width="0.45"/>')
    corners(o, 5.2, 14.2, W - 5.2, dy1 - 1.2, C2, 3.2, 0.45)
    for k in range(1, 24):
        x = 20 + k * (W - 40) / 24
        o.e(f'<line x1="{x:.2f}" y1="{dy1}" x2="{x:.2f}" y2="{dy1 - (1.0 if k % 6 else 1.6):.2f}" stroke="{C2}" stroke-width="0.2" stroke-opacity="0.7"/>')
    # header: glitch title (three offset copies), subtitle, UNDO / INIT frames, preset field frame
    for dx, col, op in ((-0.7, C1, 0.9), (0.7, C2, 0.9), (0.0, "#ffffff", 1.0)):
        o.text(8 + dx, 10.6, S["name"], 9.0, col, 900, "start", 1.6, op, check=dx == 0)
    for gx, gy, gw in ((9, 5.9, 22), (25, 8.3, 14), (38, 4.6, 10)):
        o.e(f'<rect x="{gx}" y="{gy}" width="{gw}" height="0.55" fill="#05020c"/>')
        o.e(f'<rect x="{gx + 2}" y="{gy + 0.55}" width="{gw * 0.6:.1f}" height="0.3" fill="{C2}" fill-opacity="0.8"/>')
    o.text(56, 6.2, S["sub"], 1.7, C2, 700, "start", 0.6)
    o.text(56, 9.2, "GENRA // NEON", 1.4, "#7d6aa8", 600, "start", 0.5)
    px = W - 66
    for i, (nm, lb) in enumerate((("UNDO", "UNDO"), ("INIT", "INIT"))):
        bx = px - 25 + i * 11.5
        o.e(f'<path d="M{bx:.2f} 3.6 h8.6 l1.4 1.4 v4.2 h-10 z" fill="#1a0a2e" stroke="{C3}" stroke-width="0.3"/>')
        o.text(bx + 5, 7.8, lb, 1.35, "#ffffff", 800, "middle", 0.3)
        lay.append((nm, [bx, 3.6, 10.0, 5.6]))
    o.e(f'<path d="M{px:.2f} 3.0 h55 l3 3 v4.4 h-55 l-3 -3 z" fill="#0a0518" stroke="{C1}" stroke-width="0.3"/>')
    o.e(f'<polyline points="{px + 53:.2f},5.6 {px + 54.4:.2f},7 {px + 55.8:.2f},5.6" fill="none" stroke="{C2}" stroke-width="0.4"/>')
    lay.append(("PRESETF", [px + 2.0, 3.0, 50.0, 7.4]))
    # sections
    for name, x0, x1, row in S["secs"]:
        y0, y1 = r1 if row == 1 else r2
        o.e(f'<path d="{cut(x0, y0, x1, y1)}" fill="#0a0618" fill-opacity="0.8"/>')
        o.e(f'<path d="{cut(x0, y0, x1, y1)}" fill="none" stroke="#3a2b66" stroke-width="0.3"/>')
        corners(o, x0, y0, x1, y1, C2, 2.2, 0.35)
        tw = len(name) * 1.2 + 4
        o.e(f'<path d="M{x0} {y0} V{y0 - 2.4} H{x0 + tw:.2f} L{x0 + tw + 2.4:.2f} {y0} Z" fill="#2a0f3f" stroke="{C1}" stroke-width="0.3"/>')
        o.text(x0 + 1.6, y0 - 0.55, name, 1.5, "#ffffff", 800, "start", 0.55)
    for name, x, w in S["fields"]:
        o.e(f'<path d="M{x} {fy} h{w - 1.6:.2f} l1.6 1.6 v{FH - 1.6:.2f} h-{w:.2f} z" fill="#120a28" stroke="{C3}" stroke-width="0.25"/>')
        o.e(f'<polyline points="{x + w - 4:.2f},{fy + 2.0:.2f} {x + w - 3:.2f},{fy + 3.0:.2f} {x + w - 2:.2f},{fy + 2.0:.2f}" fill="none" stroke="{C2}" stroke-width="0.3"/>')
        lay.append((name, [x, fy, w, FH]))
    for name, x, y, w, h, n in S.get("steps", []):
        pitch = w / n
        for i in range(n):
            sx = x + i * pitch
            o.e(f'<path d="M{sx + 0.3:.2f} {y:.2f} h{pitch - 1.6:.2f} l1.0 1.0 v{h - 1.0:.2f} h-{pitch - 0.6:.2f} z" fill="#120a28" stroke="{C1 if i % 4 == 0 else "#5b3a7a"}" stroke-width="0.25"/>')
            o.text(sx + pitch / 2, y + h + 2.0, str(i + 1), 1.05, LBL2, 700, "middle", 0.1)
        lay.append((name, [x, y, pitch, h, float(n)]))
    for name, x, y, r, label, big in S["knobs"]:
        o.text(x, y + r + 3.3, label, 1.55 if big == 1 else 1.35, LBL if big else LBL2, 700, "middle", 0.35)
        lay.append((name, [x, y, r, float(big)]))
    for name, x, y, names in S.get("lamps", []):
        for i, nm in enumerate(names):
            o.text(x + i * 8.6, y + 4.0, nm, 1.25, LBL2, 700, "middle", 0.25)
        lay.append((name, [x, y, 8.6, float(len(names))]))
    for name, x, y, label in S["btn"]:
        o.e(f'<path d="M{x - 5.5:.2f} {y - 3:.2f} h9 l2 2 v4 h-11 z" fill="#1a0a2e" stroke="{C1}" stroke-width="0.35"/>')
        o.text(x, y + 0.6, label, 1.3, "#ffffff", 800, "middle", 0.25)
        lay.append((name, [x - 5.5, y - 3, 11.0, 6.0]))
    if S.get("hint"):
        x, y, s = S["hint"]
        o.text(x, y, s, 1.05, "#6d5c99", 600, "middle", 0.25)
    # jacks
    o.e(f'<path d="{cut(3, 106.6, W - 3, H - 1.2, 2)}" fill="#04020a" stroke="#24183d" stroke-width="0.3"/>')
    nout = len(S["outs"])
    obw = 8.0 * nout + 3.0
    ox = W - 12.5 - obw
    ins = []
    for jy, groups in ((JY1, S["row1"]), (JY2, S["row2"])):
        x = 15.5
        for col, items in groups:
            o.e(f'<rect x="{x - 4.6:.2f}" y="{jy - 3.6:.2f}" width="{len(items) * JS:.2f}" height="9.6" rx="0.8" fill="none" stroke="{col}" stroke-width="0.25" stroke-opacity="0.75"/>')
            for name, lb in items:
                o.e(f'<polygon points="{hexagon(x, jy, 3.1)}" fill="#141024" stroke="{col}" stroke-width="0.3"/>')
                o.text(x, jy + 4.75, lb, 1.15, LBL2, 700, "middle", 0.15)
                ins.append((name, x, jy, col))
                x += JS
            x += GAP
        if x - JS - GAP + 4.6 >= ox - 1.5:
            sys.exit(f"{S['name']}: the jacks of row {jy} reach the OUT box")
    o.e(f'<rect x="{ox:.2f}" y="108" width="{obw:.2f}" height="19.2" rx="0.8" fill="{C1}" fill-opacity="0.06" stroke="{C1}" stroke-width="0.25"/>')
    o.text(ox + 2.5, 111.3, "OUT", 1.3, C1, 800, "start", 0.5)
    outs = []
    for i, (name, lb) in enumerate(S["outs"]):
        x, y = ox + 5.5 + i * 8.0, 117.4
        o.e(f'<polygon points="{hexagon(x, y, 3.2)}" fill="#141024" stroke="{C1}" stroke-width="0.3"/>')
        o.text(x, y + 4.8, lb, 1.25, "#ffffff", 700, "middle", 0.2)
        outs.append((name, x, y, C1))
    for sx in (7.62, W - 7.62):
        for sy in (2.54, H - 2.54):
            o.e(f'<circle cx="{sx:.2f}" cy="{sy:.2f}" r="1.5" fill="#0b0716" stroke="#5b4a8a" stroke-width="0.25"/>')
    o.check(S["name"])
    return o, lay, ins, outs


def header(S, lay, ins, outs, ns):
    h = [f"// Generated by tools/neon_panel.py - do not edit (mm; points [x, y], rects [x, y, w, h], knobs [x, y, r, big]).",
         "#pragma once", f"namespace {ns} {{", f"static const float PANEL_WIDTH = {S['W']:.3f}f;"]
    for name, v in lay:
        h.append(f"static const float {name}[{len(v)}] = {{{', '.join(f'{x:.3f}f' for x in v)}}};")
    h.append(f"static const int NUM_JACK_IN = {len(ins)};")
    h.append("static const float JACK_IN[][2] = {" + ", ".join(f"{{{x:.3f}f, {y:.3f}f}}" for _, x, y, _ in ins) + "};  // " +
             " ".join(n for n, _, _, _ in ins))
    h.append("static const unsigned JACK_IN_COL[] = {" + ", ".join("0x" + c[1:] for _, _, _, c in ins) + "};")
    h.append(f"static const int NUM_JACK_OUT = {len(outs)};")
    h.append("static const float JACK_OUT[][2] = {" + ", ".join(f"{{{x:.3f}f, {y:.3f}f}}" for _, x, y, _ in outs) + "};  // " +
             " ".join(n for n, _, _, _ in outs))
    h.append(f"}}  // namespace {ns}")
    return "\n".join(h) + "\n"


WELLE = dict(
    W=W44, name="WELLE", sub="WAVETABLE MUTANT",
    secs=[("OSC A", 4, 46, 1), ("OSC B", 48, 90, 1), ("FILTER", 92, 163, 1), ("MOTION / FX", 165, W44 - 3.5, 1),
          ("ENVELOPE", 4, 58, 2), ("MIX", 60, 114, 2), ("VOICE", 116, 177, 2), ("PLAY", 179, W44 - 3.5, 2)],
    fields=[("TABLEFA", 6.5, 23.5), ("WARPFA", 31, 13), ("TABLEFB", 50.5, 23.5), ("WARPFB", 75, 13), ("FTYPEF", 94.5, 66)],
    knobs=[("TBLA", 15, 63.5, 4.0, "TABLE", 0), ("POSA", 35, 63.5, 4.0, "POSITION", 2), ("WARPA", 15, 78, 4.0, "WARP", 0),
           ("OCTA", 35, 78, 4.0, "OCT", 0),
           ("TBLB", 59, 63.5, 4.0, "TABLE", 0), ("POSB", 79, 63.5, 4.0, "POSITION", 2), ("WARPB", 59, 78, 4.0, "WARP", 0),
           ("TUNEB", 79, 78, 4.0, "TUNE", 0),
           ("CUTOFF", 106, 69, 7.0, "CUTOFF", 1), ("RES", 124, 63.5, 4.0, "RES", 0), ("FENV", 124, 78, 4.0, "ENV", 0),
           ("DRIVE", 139, 63.5, 4.0, "DRIVE", 0), ("KEY", 139, 78, 4.0, "KEY", 0), ("FATK", 154, 63.5, 4.0, "F.ATK", 0),
           ("FDEC", 154, 78, 4.0, "F.DEC", 0),
           ("SCAN", 177, 63.5, 4.0, "SCAN", 0), ("RATE", 194, 63.5, 4.0, "RATE", 0), ("PENV", 211, 63.5, 4.0, "P.ENV", 0),
           ("CHORUS", 177, 78, 4.0, "CHORUS", 0), ("DELAY", 194, 78, 4.0, "DELAY", 0), ("REVERB", 211, 78, 4.0, "REVERB", 0),
           ("ATK", 12, 96.6, 4.0, "A", 0), ("DEC", 24.5, 96.6, 4.0, "D", 0), ("SUS", 37, 96.6, 4.0, "S", 0),
           ("REL", 49.5, 96.6, 4.0, "R", 0),
           ("MIX", 68, 96.6, 4.0, "A / B", 0), ("SUB", 81, 96.6, 4.0, "SUB", 0), ("UNISON", 94, 96.6, 4.0, "UNISON", 0),
           ("SPREAD", 107, 96.6, 4.0, "SPREAD", 0),
           ("GLIDE", 156, 96.6, 4.0, "GLIDE", 0), ("ACCENT", 169, 96.6, 4.0, "ACCENT", 0)],
    lamps=[("VMODE", 123, 95.4, ["POLY", "MONO", "LEGATO"])],
    btn=[("MUTATE", 192, 97.4, "MUTATE"), ("FREEZE", 208, 97.4, "FREEZE")], hint=(200, 103.6, "ROLL TABLE · HOLD FRAME"),
    row1=[(GR, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL"), ("ACC", "ACC"), ("SLIDE", "SLIDE")]),
          (SY, [("CLOCK", "CLOCK"), ("RESET", "RESET")]), (TR, [("T_MUTATE", "MUTATE"), ("T_FREEZE", "FREEZE")]),
          (CV, [("C_POSA", "POS A"), ("C_POSB", "POS B"), ("C_WARPA", "WARP A"), ("C_WARPB", "WARP B"), ("C_TBLA", "TBL A"),
                ("C_TBLB", "TBL B"), ("C_MIX", "A/B"), ("C_SUB", "SUB")])],
    row2=[(AU, [("FM", "FM")]),
          (CV, [("C_UNISON", "UNISON"), ("C_SPREAD", "SPREAD"), ("C_CUT", "CUT"), ("C_RES", "RES"), ("C_FENV", "F.ENV"),
                ("C_DRIVE", "DRIVE"), ("C_SCAN", "SCAN"), ("C_RATE", "RATE"), ("C_ATK", "ATK"), ("C_DEC", "DEC"),
                ("C_REL", "REL"), ("C_GLIDE", "GLIDE"), ("C_DELAY", "DELAY"), ("C_VERB", "VERB")])],
    outs=[("O_ENV", "ENV"), ("O_MOTION", "MOTION"), ("O_L", "L"), ("O_R", "R")],
)

NEBEL = dict(
    W=W44, name="NEBEL", sub="CLUSTER UNIVERSE",
    secs=[("GENERATOR", 4, 50, 1), ("CLUSTER", 52, 126, 1), ("TEXTURE", 128, 172, 1), ("SPACE", 174, W44 - 3.5, 1),
          ("ENVELOPE", 4, 58, 2), ("ORBIT", 60, 114, 2), ("CHORD", 116, 156, 2), ("PLAY", 158, W44 - 3.5, 2)],
    fields=[("GENF", 6.5, 41), ("CHORDF", 54.5, 69), ("TEXTF", 130.5, 39), ("SPACEF", 176.5, 41.5)],
    knobs=[("TIMBRE", 18, 69, 7.0, "TIMBRE", 1), ("MORPH", 38, 63.5, 4.0, "MORPH", 0), ("OCT", 38, 78, 4.0, "OCT", 0),
           ("SPREAD", 67, 69, 7.0, "SPREAD", 1), ("VOICES", 85, 63.5, 4.0, "VOICES", 0), ("DETUNE", 85, 78, 4.0, "DETUNE", 0),
           ("CHAOS", 100, 63.5, 4.0, "CHAOS", 0), ("DRIFT", 100, 78, 4.0, "DRIFT", 0), ("STRUM", 115, 63.5, 4.0, "STRUM", 0),
           ("ACCENT", 115, 78, 4.0, "ACCENT", 0),
           ("AMOUNT", 140, 63.5, 4.0, "AMOUNT", 0), ("COLOR", 159, 63.5, 4.0, "COLOR", 0), ("DENSITY", 140, 78, 4.0, "DENSITY", 0),
           ("WIDTH", 159, 78, 4.0, "WIDTH", 0),
           ("SIZE", 187, 63.5, 4.0, "SIZE", 0), ("DECAY", 205, 63.5, 4.0, "DECAY", 0), ("SMIX", 187, 78, 4.0, "MIX", 0),
           ("TONE", 205, 78, 4.0, "TONE", 0),
           ("ATK", 12, 96.6, 4.0, "A", 0), ("DEC", 24.5, 96.6, 4.0, "D", 0), ("SUS", 37, 96.6, 4.0, "S", 0),
           ("REL", 49.5, 96.6, 4.0, "R", 0),
           ("ORATE", 68, 96.6, 4.0, "RATE", 0), ("ODEPTH", 81, 96.6, 4.0, "DEPTH", 0), ("MACRO", 94, 96.6, 4.0, "MACRO", 0),
           ("GLIDE", 107, 96.6, 4.0, "GLIDE", 0)],
    lamps=[("CSRC", 124, 95.4, ["PANEL", "GENRA"])],
    btn=[("BANG", 169, 97.4, "BIG BANG"), ("FREEZE", 185, 97.4, "FREEZE")], hint=(177, 103.6, "NEW CLUSTER · HOLD CLUSTER"),
    row1=[(GR, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL"), ("ACC", "ACC"), ("SLIDE", "SLIDE")]),
          (SY, [("CLOCK", "CLOCK"), ("RESET", "RESET")]), (TR, [("T_BANG", "BANG"), ("T_FREEZE", "FREEZE")]),
          (CV, [("C_TIMBRE", "TIMBRE"), ("C_MORPH", "MORPH"), ("C_GEN", "GEN"), ("C_CHORD", "CHORD"), ("C_SPREAD", "SPREAD"),
                ("C_VOICES", "VOICES"), ("C_DETUNE", "DETUNE"), ("C_CHAOS", "CHAOS")])],
    row2=[(AU, [("IN_L", "IN L"), ("IN_R", "IN R")]),
          (CV, [("C_STRUM", "STRUM"), ("C_TEXT", "TEXT."), ("C_AMOUNT", "AMOUNT"), ("C_COLOR", "COLOR"), ("C_SIZE", "SIZE"),
                ("C_DECAY", "DECAY"), ("C_SMIX", "S.MIX"), ("C_ORBIT", "ORBIT"), ("C_DEPTH", "DEPTH"), ("C_MACRO", "MACRO"),
                ("C_ATK", "ATK"), ("C_REL", "REL"), ("C_GLIDE", "GLIDE")])],
    outs=[("O_ENV", "ENV"), ("O_ORBIT", "ORBIT"), ("O_L", "L"), ("O_R", "R")],
)

WOGE = dict(
    W=W44, name="WOGE", sub="SCANNED STRING",
    secs=[("STRING", 4, 62, 1), ("EXCITE", 64, 120, 1), ("SCAN", 122, 170, 1), ("FILTER", 172, W44 - 3.5, 1),
          ("ENVELOPE", 4, 58, 2), ("VOICE", 60, 120, 2), ("PLAY", 122, W44 - 3.5, 2)],
    fields=[("EXCF", 66.5, 51), ("PATHF", 124.5, 43)],
    knobs=[("MASS", 17, 69, 7.0, "MASS", 1), ("TENS", 37, 63.5, 4.0, "TENSION", 0), ("DAMP", 37, 78, 4.0, "DAMP", 0),
           ("CNTR", 53, 63.5, 4.0, "CENTER", 0),
           ("POS", 82, 71, 4.0, "POSITION", 2), ("FORCE", 104, 71, 4.0, "FORCE", 2),
           ("WIDTH", 135, 71, 4.0, "WIDTH", 0), ("DRIFT", 157, 71, 4.0, "DRIFT", 0),
           ("CUTOFF", 187, 69, 7.0, "CUTOFF", 1), ("RES", 208, 63.5, 4.0, "RES", 0), ("DRIVE", 208, 78, 4.0, "DRIVE", 0),
           ("ATK", 12, 96.6, 4.0, "A", 0), ("DEC", 24.5, 96.6, 4.0, "D", 0), ("SUS", 37, 96.6, 4.0, "S", 0),
           ("REL", 49.5, 96.6, 4.0, "R", 0),
           ("GLIDE", 100, 96.6, 4.0, "GLIDE", 0), ("ACCENT", 113, 96.6, 4.0, "ACCENT", 0)],
    lamps=[("VMODE", 66, 95.4, ["POLY", "MONO", "LEGATO"])],
    btn=[("STRIKE", 140, 97.4, "STRIKE"), ("FREEZE", 156, 97.4, "FREEZE")], hint=(148, 103.6, "HIT THE STRING · HOLD IT"),
    row1=[(GR, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL"), ("ACC", "ACC"), ("SLIDE", "SLIDE")]),
          (SY, [("CLOCK", "CLOCK"), ("RESET", "RESET")]), (TR, [("T_STRIKE", "STRIKE"), ("T_FREEZE", "FREEZE")]),
          (CV, [("C_MASS", "MASS"), ("C_TENS", "TENS"), ("C_DAMP", "DAMP"), ("C_CNTR", "CNTR"), ("C_POS", "POS"),
                ("C_FORCE", "FORCE"), ("C_WIDTH", "WIDTH"), ("C_DRIFT", "DRIFT")])],
    row2=[(AU, [("EXC", "EXC IN"), ("FM", "FM")]),
          (CV, [("C_CUT", "CUT"), ("C_RES", "RES"), ("C_DRIVE", "DRIVE"), ("C_ATK", "ATK"), ("C_DEC", "DEC"),
                ("C_REL", "REL"), ("C_GLIDE", "GLIDE")])],
    outs=[("O_ENV", "ENV"), ("O_SHAPE", "SHAPE"), ("O_L", "L"), ("O_R", "R")],
)

PULS = dict(
    W=W44, name="PULS", sub="PULSAR TRAIN",
    secs=[("PULSE", 4, 54, 1), ("PULSARET", 56, 110, 1), ("MASK", 112, W44 - 3.5, 1),
          ("FILTER", 4, 56, 2), ("SPACE", 58, 110, 2), ("ENVELOPE", 112, 166, 2), ("VOICE", 168, W44 - 3.5, 2)],
    fields=[("PMODEF", 6.5, 26), ("SHAPEF", 58.5, 24), ("WINF", 84.5, 23.5), ("PATF", 114.5, 26)],
    knobs=[("RATE", 17, 71, 7.0, "RATE", 1), ("F1", 43, 60.5, 4.0, "F1", 2), ("F2", 43, 76.5, 4.0, "F2", 2),
           ("DUTY", 70, 70, 4.0, "DUTY", 0), ("SKEW", 96, 70, 4.0, "SKEW", 0),
           ("DENS", 150, 55.5, 4.0, "DENS", 0), ("ROT", 165, 55.5, 4.0, "ROT", 0), ("PROB", 180, 55.5, 4.0, "PROB", 0),
           ("CUTOFF", 14, 96.6, 4.0, "CUTOFF", 0), ("RES", 29, 96.6, 4.0, "RES", 0), ("DRIVE", 44, 96.6, 4.0, "DRIVE", 0),
           ("SPREAD", 70, 96.6, 4.0, "SPREAD", 2), ("JITTER", 84, 96.6, 4.0, "JITTER", 2), ("TAIL", 98, 96.6, 4.0, "TAIL", 2),
           ("ATK", 120, 96.6, 4.0, "A", 0), ("DEC", 132.5, 96.6, 4.0, "D", 0), ("SUS", 145, 96.6, 4.0, "S", 0),
           ("REL", 157.5, 96.6, 4.0, "R", 0),
           ("GLIDE", 203, 96.6, 4.0, "GLIDE", 0), ("ACCENT", 214, 96.6, 4.0, "ACCENT", 0)],
    lamps=[("VMODE", 174, 95.4, ["POLY", "MONO", "LEGATO"])],
    btn=[("NEW", 199, 54.6, "NEW")],
    steps=[("STEPS", 115, 66.5, 102, 11, 16)], hint=(166, 85.6, "CLICK A STEP: ON / OFF"),
    row1=[(GR, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL"), ("ACC", "ACC"), ("SLIDE", "SLIDE")]),
          (SY, [("CLOCK", "CLOCK"), ("RESET", "RESET")]), (TR, [("T_NEW", "NEW")]),
          (CV, [("C_RATE", "RATE"), ("C_F1", "F1"), ("C_F2", "F2"), ("C_DUTY", "DUTY"), ("C_SKEW", "SKEW"),
                ("C_DENS", "DENS"), ("C_ROT", "ROT"), ("C_PROB", "PROB")])],
    row2=[(AU, [("FM", "FM")]),
          (CV, [("C_SPREAD", "SPREAD"), ("C_JITTER", "JITTER"), ("C_TAIL", "TAIL"), ("C_CUT", "CUT"), ("C_RES", "RES"),
                ("C_DRIVE", "DRIVE"), ("C_ATK", "ATK"), ("C_REL", "REL"), ("C_GLIDE", "GLIDE")])],
    outs=[("O_ENV", "ENV"), ("O_PULSE", "PULSE"), ("O_L", "L"), ("O_R", "R")],
)

GLAS = dict(
    W=W44, name="GLAS", sub="PAINTED SPECTRUM", disp_y1=62.0, r1=(67.0, 87.6), fy=69.0,
    secs=[("PAINT", 4, 80, 1), ("PLAY", 82, 140, 1), ("TONE", 142, W44 - 3.5, 1),
          ("ENVELOPE", 4, 58, 2), ("SPACE", 60, 100, 2), ("MODE", 102, 128, 2), ("GLASS", 130, W44 - 3.5, 2)],
    fields=[("BANKF", 6.5, 40), ("LENF", 84.5, 25), ("DIRF", 112.5, 25), ("SCALEF", 144.5, 44)],
    knobs=[("SIZE", 53, 79.5, 4.0, "SIZE", 0),
           ("SPEED", 98, 79, 4.0, "SPEED", 0), ("POS", 125, 79, 4.0, "POS", 0),
           ("LOW", 155, 79, 4.0, "LOW", 2), ("RNG", 172, 79, 4.0, "RANGE", 2), ("TILT", 189, 79, 4.0, "TILT", 2),
           ("ATK", 12, 96.6, 4.0, "A", 0), ("DEC", 24.5, 96.6, 4.0, "D", 0), ("SUS", 37, 96.6, 4.0, "S", 0),
           ("REL", 49.5, 96.6, 4.0, "R", 0),
           ("SPREAD", 68, 96.6, 4.0, "SPREAD", 0), ("BLUR", 80, 96.6, 4.0, "BLUR", 0), ("SHIMMER", 92, 96.6, 4.0, "SHIMMER", 0)],
    lamps=[("TOOL", 9, 77.0, ["PEN", "SPRAY", "LINE", "ERASE"]), ("PMODE", 107, 95.4, ["DRONE", "GATE"])],
    btn=[("SNAP", 69, 79.0, "SNAP"), ("NEW", 141, 97.4, "NEW"), ("CLEAR", 157, 97.4, "CLEAR"), ("REC", 173, 97.4, "REC")],
    hint=(198, 97.8, "PICTURE · WIPE · PAINT IN"),
    row1=[(GR, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL"), ("ACC", "ACC"), ("SLIDE", "SLIDE")]),
          (SY, [("CLOCK", "CLOCK"), ("RESET", "RESET")]), (TR, [("T_NEW", "NEW"), ("T_REC", "REC"), ("T_CLR", "CLEAR")]),
          (CV, [("C_SPEED", "SPEED"), ("C_POS", "POS"), ("C_LOW", "LOW"), ("C_RNG", "RANGE"), ("C_TILT", "TILT"),
                ("C_SPREAD", "SPREAD"), ("C_BLUR", "BLUR")])],
    row2=[(AU, [("IN", "IN")]),
          (CV, [("C_SHIMMER", "SHIMMER"), ("C_ATK", "ATK"), ("C_REL", "REL")])],
    outs=[("O_ENV", "ENV"), ("O_BRIGHT", "BRIGHT"), ("O_L", "L"), ("O_R", "R")],
)

FLUT = dict(
    W=W44, name="FLUT", sub="TIDAL DRONE",
    secs=[("VOICES", 4, 66, 1), ("TIDE", 68, 124, 1), ("TEXTURE", 126, 172, 1), ("SPACE", 174, W44 - 3.5, 1),
          ("SHAPE", 4, 50, 2), ("MODE", 52, 80, 2), ("PLAY", 82, W44 - 3.5, 2)],
    fields=[("TIDEF", 70.5, 24), ("TEXF", 128.5, 41)],
    knobs=[("VOICES", 13, 63.5, 4.0, "VOICES", 0), ("OCT", 13, 78, 4.0, "OCT", 0), ("SPRD", 28, 63.5, 4.0, "SPREAD", 2),
           ("DRIFT", 28, 78, 4.0, "DRIFT", 2), ("MORPH", 43, 63.5, 4.0, "MORPH", 2), ("SUB", 43, 78, 4.0, "SUB", 0),
           ("TIME", 111, 64, 7.0, "TIME", 1), ("DEPTH", 76, 66, 4.0, "DEPTH", 0), ("PHS", 90, 66, 4.0, "PHASE", 0),
           ("SKEW", 83, 78.5, 4.0, "SKEW", 0),
           ("BRIGHT", 136, 70, 4.0, "BRIGHT", 0), ("GRIT", 149.5, 70, 4.0, "GRIT", 0), ("BREATH", 163, 70, 4.0, "BREATH", 0),
           ("BLOOM", 184, 70, 4.0, "BLOOM", 2), ("WIDTH", 197, 70, 4.0, "WIDTH", 2), ("HAZE", 210, 70, 4.0, "HAZE", 2),
           ("SWELL", 12, 96.6, 4.0, "SWELL", 0), ("FADE", 25, 96.6, 4.0, "FADE", 0), ("LEVEL", 38, 96.6, 4.0, "LEVEL", 0)],
    lamps=[("SUBOCT", 54, 76.0, ["-1 OCT", "-2 OCT"]), ("PMODE", 57, 95.4, ["DRONE", "GATE"])],
    btn=[("FREEZE", 95, 97.4, "FREEZE"), ("NEW", 111, 97.4, "NEW")], hint=(103, 103.6, "HOLD THE TIDE · NEW VOICING"),
    row1=[(GR, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL"), ("ACC", "ACC"), ("SLIDE", "SLIDE")]),
          (SY, [("CLOCK", "CLOCK"), ("RESET", "RESET")]), (TR, [("T_NEW", "NEW"), ("T_FREEZE", "FREEZE")]),
          (CV, [("C_SPRD", "SPREAD"), ("C_DRIFT", "DRIFT"), ("C_MORPH", "MORPH"), ("C_SUB", "SUB"), ("C_OCT", "OCT"),
                ("C_TIME", "TIME"), ("C_DEPTH", "DEPTH")])],
    row2=[(AU, [("IN_L", "IN L"), ("IN_R", "IN R")]),
          (CV, [("C_PHS", "PHASE"), ("C_SKEW", "SKEW"), ("C_BRIGHT", "BRIGHT"), ("C_GRIT", "GRIT"), ("C_BREATH", "BREATH"),
                ("C_BLOOM", "BLOOM"), ("C_WIDTH", "WIDTH"), ("C_HAZE", "HAZE"), ("C_LEVEL", "LEVEL")])],
    outs=[("O_TIDE", "TIDE"), ("O_SUB", "SUB"), ("O_POLY", "POLY"), ("O_L", "L"), ("O_R", "R")],
)


# ---------------------------------------------------------------- preview stand-ins
def stand_ins(S, lay):
    W = S["W"]
    s = ['<defs><radialGradient id="kn" cx=".38" cy=".3" r=".8"><stop offset="0" stop-color="#2c2440"/>'
         '<stop offset="1" stop-color="#050309"/></radialGradient></defs>']
    rnd = random.Random(3)
    for name, v in lay:
        if len(v) == 4 and name not in ("DISPLAY", "PRESETF", "UNDO", "INIT") and v[2] in (4.0, 7.0) and v[3] in (0.0, 1.0, 2.0):
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
        open(svg, "w").write("\n".join(o.o + stand_ins(S, lay) + ["</svg>"]))
        subprocess.run(["rsvg-convert", "-w", "2400", svg, "-o", png], check=True)
        os.remove(svg)
        print(f"{slug}: preview {png}")
        return
    open(os.path.join(ROOT, "res", slug + ".svg"), "w").write("\n".join(o.o + ["</svg>"]))
    open(os.path.join(ROOT, "src", sub, slug + "Layout.hpp"), "w").write(header(S, lay, ins, outs, ns))
    print(f"{slug}: res/{slug}.svg, src/{sub}/{slug}Layout.hpp ({len(ins)} in, {len(outs)} out)")


write(WELLE, "Welle", "wl", "welle")
write(NEBEL, "Nebel", "nl", "nebel")
write(WOGE, "Woge", "wg", "woge")
write(PULS, "Puls", "pl", "puls")
write(GLAS, "Glas", "gl", "glas")
write(FLUT, "Flut", "fl", "flut")
