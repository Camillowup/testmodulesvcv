#!/usr/bin/env python3
"""The panel of SCHLEIFE (a four-track looper in the GENRA look, tools/gpanel.py).

Usage:  schleife_panel.py real           -> res/Schleife.svg (text as paths) + src/schleife/SchleifeLayout.hpp
        schleife_panel.py preview OUT.png (with a picture of the display: four loops)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpanel  # noqa: E402

ACC = "#8fd3ff"  # light blue, as GENRA FX


def track(i):
    return dict(title=f"TRACK {i + 1}",
                cols=[[("knob", f"BARS{i}", "BARS"), ("small", f"LVL{i}", "LEVEL")],
                      [("button", f"REC{i}", "REC/DUB"), ("button", f"UNDO{i}", "UNDO"), ("button", f"CLR{i}", "CLEAR")]],
                lamps=[(f"STAT{i}", "", ["STATUS"])])


SPEC = dict(
    name="SCHLEIFE", sub="GENRA · LOOPER", subshort="LOOPER", acc=ACC, preset="16THS · 4 PPQN",
    rows=[[track(i) for i in range(4)] +
          [dict(title="ALL", cols=[[("small", "MASTER", "MASTER"), ("button", "CLRALL", "CLR ALL")]])]],
    sync=[("CLOCK", "CLOCK"), ("RESET", "RESET")],
    cvs=[[("MAININ", "MAIN IN")] + [(f"IN{i}", f"IN {i + 1}") for i in range(4)]],
    outs=[(f"OUT{i}", f"OUT {i + 1}") for i in range(4)] + [("MIXL", "MIX L"), ("MIXR", "MIX R")])

p = gpanel.build(SPEC)

if len(sys.argv) > 1 and sys.argv[1] == "preview":
    # the display as the module draws it: four loops with bar lines and where each one is
    d = [c for c in p.comps if c[1] == "DISPLAY"][0]
    x, y, w, h = d[2], d[3], d[4]["w"], d[4]["h"]
    shown = [("T1", "2 BARS", 0.62, "PLAY"), ("T2", "4 BARS", 0.31, "DUB"), ("T3", "1 BAR", 0.0, "EMPTY"),
             ("T4", "8 BARS", 0.08, "REC")]
    col = {"PLAY": "#5d8fa8", "DUB": "#b8873d", "REC": "#b84a4a", "EMPTY": "#2a2c33"}
    rowh = (h - 4) / 4
    for i, (t, bars, pos, st) in enumerate(shown):
        ry = y + 2 + i * rowh + rowh / 2
        p.text(x + 4, ry + 0.7, t, 1.9, ACC, 800, "start", 0.3, mock=True)
        p.text(x + 10, ry + 0.6, bars, 1.5, "#8a8d99", 600, "start", 0.25, mock=True)
        bx, bw = x + 24, w - 46
        p.e(f'<rect x="{bx}" y="{ry - 1.2}" width="{bw}" height="2.4" rx="0.6" fill="#1c1e24"/>', True)
        n = int(bars.split()[0])
        for k in range(1, n):
            p.e(f'<rect x="{bx + bw * k / n - 0.1}" y="{ry - 1.2}" width="0.2" height="2.4" fill="#07080a"/>', True)
        if st != "EMPTY":
            p.e(f'<rect x="{bx}" y="{ry - 1.2}" width="{bw * max(pos, 0.01)}" height="2.4" rx="0.6" fill="{col[st]}" opacity="0.8"/>', True)
            p.e(f'<rect x="{bx + bw * pos - 0.2}" y="{ry - 1.8}" width="0.4" height="3.6" fill="#d9dbe2"/>', True)
        p.text(x + w - 4, ry + 0.6, st, 1.5, "#8a8d99" if st in ("EMPTY", "PLAY") else col[st], 700, "end", 0.25, mock=True)

gpanel.write(p, "Schleife", "schleife", None, "schleifel", "schleife_panel.py")
