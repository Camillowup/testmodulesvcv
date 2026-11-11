#!/usr/bin/env python3
"""The panel of SAURUS (PALEOTONE's creature calls) in the GENRA look.

Usage:  saurus_panel.py real                    -> res/Saurus.svg (text as paths) + src/saurus/SaurusLayout.hpp
        saurus_panel.py preview OUTDIR RACK     -> OUTDIR/Saurus.png with Rack components and mock display content
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jpanel import Panel, env_fx, io_row, write_real, write_preview, bold, mono, TXT, DIM  # noqa: E402

THROAT = ["SIZE", "SWOOP", "LEN", "GROWL", "BRTH", "MOUTH", "WILD"]


def saurus_panel():
    p = Panel("Saurus", "SAURUS", 26, "PALEOTONE · CALLS")
    p.display(14, 30, 362, 106)
    p.plate(14, 140, 362, 40)
    p.button("PREV", 27, 154)
    p.button("NEXT", 56, 154)
    p.pair_label(27, 56, 173.5, "PRESET")
    p.button("CALL", 88, 154, "CALL", light=True)
    p.sep(104, 146, 174)
    p.block(110, 143, 70, 34)
    p.text(145, 151, "TYPE", 4.6, p.C, 800, "middle", 1.6)
    p.area("TYPE_SEL", 114, 155, 62, 17)
    p.sep(185, 146, 174)
    p.block(190, 143, 128, 34)
    p.text(254, 151, "VOICE", 4.6, p.C, 800, "middle", 1.6)
    p.leds("VOICE", 212, 160.5, 42, ["POLY", "MONO", "LEGATO"], 172.5)
    p.sep(324, 146, 174)
    p.knob("LEVEL", 350, 155, label="LEVEL", ly=175.5, v=0.82)
    # THROAT: the engine's seven, each with its CV
    p.plate(14, 184, 362, 82, True)
    p.title(195, 194.5, "THROAT", 6.0)
    n = len(THROAT)
    for k in range(n):
        x = 14 + 362 / n * (k + 0.5)
        p.knob(f"E{k}", x, 211)
        p.area(f"E_TEXT{k}", x - 362 / n / 2, 224.5, 362 / n, 15)
        p.jack(f"E_IN{k}", x, 253.5)
    env_fx(p)
    io_row(p, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL"), ("CALL_IN", "CALL"), ("TYPE_IN", "TYPE")])
    return p


p = saurus_panel()
if len(sys.argv) < 2 or sys.argv[1] != "preview":
    write_real(p, "saurus_panel.py", "slayout")
    sys.exit(0)

values = ["105", "58", "1.63s", "79%", "35%", "127", "24%"]
TREX = ["........GGGGGG..", "......GGGGGGGGG.", "......GGWEGGGGGG", "......GGGGGGGGGG", "......GGGGGMMMMM",
        "......GGGGGT.T.T", "......GGGGGGGG..", "..G..GGGGGGG....", ".GG.GGGGGGGGGG..", "GGGGGGGGGGGG.g..",
        "GGGGGGGGGGGG....", ".GGGGGGGGGGG....", "...GGGGGGGG.....", "....GG...GG.....", "....GG...GG.....",
        "...GGG..GGG....."]
PAL = {"G": p.C, "g": "#3f6e27", "W": "#ffffff", "E": "#111111", "M": "#8f2f2a", "T": "#f2f0e6"}
s = []
for kind, name, x, y, kw in p.comp:
    if kind == "area" and name.startswith("E_TEXT"):
        k = int(name[6:])
        cx = x + kw["w"] / 2
        s.append(bold(cx, y + 6.5, THROAT[k], 5.3, TXT, "middle", 0.4))
        s.append(mono(p, cx, y + 13.5, values[k], 5.0, p.C, "middle"))
    elif kind == "area" and name == "TYPE_SEL":
        s.append(f'<rect x="{x}" y="{y}" width="{kw["w"]}" height="{kw["h"]}" rx="2.2" fill="#0d0e12" stroke="#34363f" stroke-width="0.8"/>')
        s.append(bold(x + kw["w"] / 2, y + 11.5, "ROAR ▾", 6.2, TXT, "middle", 0.4))
    elif kind == "area" and name == "SCREEN":
        P = 5.5
        ox, oy = x + (kw["w"] - 16 * P) / 2, y + (kw["h"] - 16 * P) / 2
        for r, row in enumerate(TREX):
            for c, ch in enumerate(row):
                if ch != ".":
                    s.append(f'<rect x="{ox + c * P:.1f}" y="{oy + r * P:.1f}" width="{P + 0.1}" height="{P + 0.1}" fill="{PAL[ch]}"/>')
    elif kind == "area" and name == "INFO":
        ix, iy, iw = x, y, kw["w"]
        s.append(bold(ix, iy + 20, "TYRANT ROAR", 13, "#f2f3f7", "start", 0.6))
        s.append(bold(ix + iw, iy + 20, "· 2/21", 6, DIM, "end", 0.8))
        s.append(mono(p, ix, iy + 33, "T-REX · ROAR · MONO"))
        gx, gy, gw, gh = ix, iy + 40, iw, 52
        s.append(f'<rect x="{gx}" y="{gy}" width="{gw}" height="{gh}" rx="2.2" fill="#0d0e12" stroke="#34363f" stroke-width="0.6"/>')
        pts = lambda arr, off: " ".join(f"{gx + 8 + i * (gw - 50) / 4:.1f},{gy + gh / 2 + off - v:.1f}" for i, v in enumerate(arr))
        s.append(f'<polyline points="{pts([6, 4, 0, -8, -16], 0)}" fill="none" stroke="{p.C}" stroke-width="1.6"/>')
        s.append(f'<polyline points="{pts([16, 16, 14, 10, 5], 8)}" fill="none" stroke="#d9dbe2" stroke-opacity=".55" stroke-width="1" stroke-dasharray="3 2"/>')
        s.append(mono(p, gx + gw - 4, gy + 14, "pitch", 5.0, p.C, "end"))
        s.append(mono(p, gx + gw - 4, gy + 30, "mouth", 5.0, DIM, "end"))
write_preview(p, s, sys.argv[2], sys.argv[3])
