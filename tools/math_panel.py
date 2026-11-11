#!/usr/bin/env python3
"""The panel of MATH (bytebeat / floatbeat from JIANT FM) in the GENRA look.

Usage:  math_panel.py real                    -> res/Math.svg (text as paths) + src/math/MathLayout.hpp
        math_panel.py preview OUTDIR RACK     -> OUTDIR/Math.png with Rack components and mock display content
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jpanel import Panel, engine, env_fx, io_row, write_real, write_preview, bold, mono, chips, TXT, DIM  # noqa: E402


def math_panel():
    p = Panel("Math", "MATH", 26, "JIANT FM · BYTEBEAT / FLOATBEAT")
    p.display(14, 30, 362, 106)
    p.plate(14, 140, 362, 40)
    p.button("PREV", 32, 154)
    p.button("NEXT", 62, 154)
    p.pair_label(32, 62, 173.5, "PRESET")
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


p = math_panel()
if len(sys.argv) < 2 or sys.argv[1] != "preview":
    write_real(p, "math_panel.py", "mlayout")
    sys.exit(0)

labels = ["ALGO", "VAR", "MOTION", "GRIT", "BEND", "LOOP"]
values = ["B17", "31%", "SYNC 1", "0%", "0%", "0%"]
s = []
for kind, name, x, y, kw in p.comp:
    if kind == "area" and name.startswith("E_TEXT"):
        k = int(name[6:])
        cx = x + kw["w"] / 2
        s.append(bold(cx, y + 6.5, labels[k], 5.3, TXT, "middle", 0.4))
        s.append(mono(p, cx, y + 13.5, values[k], 5.0, p.C, "middle"))
    elif kind == "area" and name == "SCREEN":
        for i in range(30):
            v = ((i * 5 & i >> 1) | (i * 3 & i >> 2)) % 9
            s.append(f'<rect x="{x + 6 + i * 4.6:.1f}" y="{y + kw["h"] - 10 - v * 9}" width="3.6" height="{v * 9 + 4}" fill="{p.C}" opacity="{0.35 + v / 14:.2f}"/>')
    elif kind == "area" and name == "INFO":
        ix, iy, iw = x, y, kw["w"]
        s.append(bold(ix, iy + 20, "BYTE VAR", 13, "#f2f3f7", "start", 0.6))
        s.append(bold(ix + iw, iy + 20, "· 2/12", 6, DIM, "end", 0.8))
        s.append(mono(p, ix, iy + 33, "BYTE · 8BIT · KEYS · POLY"))
        s.append(chips(ix, iy + 41, ["BYTE", "FLOAT"], {0}, 36, 14, p.C))
        s.append(chips(ix + 82, iy + 41, ["4BIT", "4B/2", "8BIT", "1BIT"], {2}, (iw - 82 - 9) / 4, 14, p.C))
        s.append(mono(p, ix, iy + 71, "TIME  free · 120 BPM · Q 17", 5.6, DIM))
        s.append(mono(p, ix, iy + 92, "ALGO B17  VAR 31%  MOTION SYNC 1", 5.2, p.C))
write_preview(p, s, sys.argv[2], sys.argv[3])
