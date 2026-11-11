#!/usr/bin/env python3
"""The panel of KURBEL (ESTAMPIE's hurdy-gurdy and shawm) in the GENRA look.

Usage:  kurbel_panel.py real                    -> res/Kurbel.svg (text as paths) + src/kurbel/KurbelLayout.hpp
        kurbel_panel.py preview OUTDIR RACK     -> OUTDIR/Kurbel.png with Rack components and mock display content
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jpanel import Panel, engine, env_fx, io_row, write_real, write_preview, bold, mono, chips, TXT, DIM  # noqa: E402


def kurbel_panel():
    p = Panel("Kurbel", "KURBEL", 26, "ESTAMPIE · HURDY-GURDY · REED")
    p.display(14, 30, 362, 106)
    p.plate(14, 140, 362, 40)
    p.button("PREV", 27, 154)
    p.button("NEXT", 52, 154)
    p.pair_label(27, 52, 173.5, "PRESET")
    p.button("CALL", 77, 154, "CALL", light=True)
    p.button("RING", 102, 154, "RING", light=True)
    p.sep(117, 146, 174)
    p.block(121, 143, 50, 34)
    p.text(146, 151, "DRONE", 4.6, p.C, 800, "middle", 1.6)
    p.area("DRONE_SEL", 125, 155, 42, 17)
    p.block(176, 143, 62, 34)
    p.text(207, 151, "COUP", 4.6, p.C, 800, "middle", 1.6)
    p.area("COUP_SEL", 180, 155, 54, 17)
    p.sep(243, 146, 174)
    p.knob("CRANK", 262, 155, label="CRANK", ly=175.5, v=0.4)
    p.area("CRANK_TEXT", 279, 147, 42, 16)
    p.sep(325, 146, 174)
    p.knob("LEVEL", 352, 155, label="LEVEL", ly=175.5, v=0.82)
    engine(p, 6)
    env_fx(p)
    io_row(p, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL"), ("CLOCK", "CLOCK"), ("RESET", "RESET"),
               ("CRANK_IN", "CRANK"), ("BUZZ_IN", "BUZZ")])
    return p


p = kurbel_panel()
if len(sys.argv) < 2 or sys.argv[1] != "preview":
    write_real(p, "kurbel_panel.py", "klayout")
    sys.exit(0)

labels = ["D LVL", "5TH", "WHEEL", "BUZZ", "BODY", "WOBBLE"]
values = ["71%", "39%", "63%", "67%", "55%", "31%"]
s = []
for kind, name, x, y, kw in p.comp:
    if kind == "area" and name.startswith("E_TEXT"):
        k = int(name[6:])
        cx = x + kw["w"] / 2
        s.append(bold(cx, y + 6.5, labels[k], 5.3, TXT, "middle", 0.4))
        s.append(mono(p, cx, y + 13.5, values[k], 5.0, p.C, "middle"))
    elif kind == "area" and name in ("DRONE_SEL", "COUP_SEL"):
        s.append(f'<rect x="{x}" y="{y}" width="{kw["w"]}" height="{kw["h"]}" rx="2.2" fill="#0d0e12" stroke="#34363f" stroke-width="0.8"/>')
        s.append(bold(x + kw["w"] / 2, y + 11.5, "D ▾" if name == "DRONE_SEL" else "1/8 ▾", 6.2, TXT, "middle", 0.4))
    elif kind == "area" and name == "CRANK_TEXT":
        s.append(mono(p, x + kw["w"] / 2, y + 7, "42", 7.5, p.C, "middle"))
        s.append(mono(p, x + kw["w"] / 2, y + 14.5, "rpm", 5.0, DIM, "middle"))
    elif kind == "area" and name == "SCREEN":
        cx, cy, r = x + 44, y + kw["h"] / 2, 34
        s.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="{p.C}" stroke-width="2"/>')
        for i in range(8):
            a = i * math.pi / 4 + 0.3
            s.append(f'<line x1="{cx}" y1="{cy}" x2="{cx + math.cos(a) * r:.1f}" y2="{cy + math.sin(a) * r:.1f}" stroke="{p.C}" stroke-opacity="0.35"/>')
        for i, dy in enumerate((-18, -6, 6, 18)):
            s.append(f'<line x1="{x + 6}" y1="{cy + dy}" x2="{x + kw["w"] - 6}" y2="{cy + dy}" stroke="{p.C}" stroke-width="{1.8 if i == 1 else 1}" stroke-opacity="{1 if i == 1 else 0.5}"/>')
    elif kind == "area" and name == "INFO":
        ix, iy, iw = x, y, kw["w"]
        s.append(bold(ix, iy + 20, "HURDY GURDY", 13, "#f2f3f7", "start", 0.6))
        s.append(bold(ix + iw, iy + 20, "· 1/8", 6, DIM, "end", 0.8))
        s.append(mono(p, ix, iy + 33, "WHEEL · DRONE D · PYTH · RING"))
        s.append(chips(ix, iy + 41, ["WHEEL", "REED"], {0}, 40, 14, p.C))
        s.append(chips(ix + 88, iy + 41, ["EQUAL", "PYTH", "MEAN", "JUST"], {1}, (iw - 88 - 9) / 4, 14, p.C))
        s.append(mono(p, ix, iy + 71, "TEMPO 120 BPM · STROKES 1/8", 5.6, DIM))
        s.append(mono(p, ix, iy + 92, "D LVL 71%  5TH 39%  WHEEL 63%", 5.2, p.C))
write_preview(p, s, sys.argv[2], sys.argv[3])
