#!/usr/bin/env python3
"""The panel of SWARM (JIANT FM's swarm of devices, without a radio: virtual ones) in the GENRA look.

Usage:  swarm_panel.py real                    -> res/Swarm.svg (text as paths) + src/swarm/SwarmLayout.hpp
        swarm_panel.py preview OUTDIR RACK     -> OUTDIR/Swarm.png with Rack components and mock display content
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jpanel import Panel, engine, io_row, write_real, write_preview, bold, mono, R_TRIM, TXT, DIM  # noqa: E402


def swarm_panel():
    p = Panel("Swarm", "SWARM", 26, "JIANT FM · A SWARM OF DEVICES")
    p.display(14, 30, 362, 106)
    p.plate(14, 140, 362, 40)
    p.button("PREV", 32, 154, "◀ PRE")
    p.button("NEXT", 62, 154, "PRE ▶")
    p.button("NEW", 94, 154, "NEW", light=True)
    p.sep(112, 146, 174)
    p.block(118, 143, 84, 34)
    p.text(160, 151, "PLAY", 4.6, p.C, 800, "middle", 1.6)
    p.leds("PLAY", 141, 160.5, 38, ["THRU", "KEYS"], 172.5)
    p.sep(208, 146, 174)
    p.knob("VOICES", 236, 155, label="VOICES", ly=175.5, v=0.4)
    p.area("VOICES_TEXT", 252, 147, 30, 16)
    p.sep(326, 146, 174)
    p.knob("LEVEL", 352, 155, label="LEVEL", ly=175.5, v=0.82)
    engine(p, 6)
    # AIR: how the devices behave (BUSY MOVE NEAR, NEAR with its CV below), and FX
    p.plate(14, 270, 178, 42, True)
    p.title(103, 280, "AIR", 5.0, 2.0)
    for i, (n, lab) in enumerate([("BUSY", "BUSY"), ("MOVE", "MOVE"), ("NEAR", "NEAR")]):
        p.knob(n, 14 + 178 / 3 * (i + 0.5), 294, R_TRIM, lab, 309.5, ring=False)
    p.plate(198, 270, 178, 42, True)
    p.title(287, 280, "FX", 5.0, 2.0)
    for i, (n, lab) in enumerate([("DIST", "DIST"), ("CHOR", "CHORUS"), ("DLY", "DELAY"), ("REV", "REVERB")]):
        p.knob(n, 198 + 178 / 4 * (i + 0.5), 294, R_TRIM, lab, 309.5, ring=False)
    io_row(p, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL"), ("CLOCK", "CLOCK"), ("RESET", "RESET"),
               ("NEAR_IN", "NEAR"), ("ARRIVE_IN", "ARRIVE"), ("NEW_IN", "NEW")], rest_dx=26, rest_gap=4)
    return p


p = swarm_panel()
if len(sys.argv) < 2 or sys.argv[1] != "preview":
    write_real(p, "swarm_panel.py", "slayout")
    sys.exit(0)

labels = ["PLUCK", "FM", "IMAGE", "DRIFT", "OCTAVE", "CHAOS"]
values = ["0%", "16%", "31%", "16%", "0%", "79%"]
s = []
for kind, name, x, y, kw in p.comp:
    if kind == "area" and name.startswith("E_TEXT"):
        k = int(name[6:])
        cx = x + kw["w"] / 2
        s.append(bold(cx, y + 6.5, labels[k], 5.3, TXT, "middle", 0.4))
        s.append(mono(p, cx, y + 13.5, values[k], 5.0, p.C, "middle"))
    elif kind == "area" and name == "VOICES_TEXT":
        s.append(bold(x + 2, y + 11, "4", 9, "#f2f3f7", "start", 0.3))
    elif kind == "area" and name == "SCREEN":
        import math
        pts = [(0.15, 0.55), (0.3, 0.3), (0.5, 0.62), (0.72, 0.38), (0.86, 0.7)]
        W, H = kw["w"], kw["h"]
        for (u, v) in pts[1:]:
            s.append(f'<line x1="{x + W * pts[0][0]:.1f}" y1="{y + H * pts[0][1]:.1f}" x2="{x + W * u:.1f}" y2="{y + H * v:.1f}" stroke="{p.C}" stroke-width="0.6" opacity="0.5"/>')
        for i, (u, v) in enumerate(pts):
            z = 4 + (i * 3) % 5
            s.append(f'<rect x="{x + W * u - z / 2:.1f}" y="{y + H * v - z / 2:.1f}" width="{z}" height="{z}" fill="{p.C if i else "#f2f3f7"}"/>')
        s.append(f'<rect x="{x + 8}" y="{y + H - 9}" width="{W - 16}" height="2" fill="#2a2c33"/>')
        s.append(f'<rect x="{x + 8}" y="{y + H - 9}" width="{(W - 16) * 0.5:.1f}" height="2" fill="{p.C}"/>')
        s.append(bold(x + W / 2, y + 10, "SWARM 4", 5.5, p.C, "middle", 0.6))
    elif kind == "area" and name == "INFO":
        ix, iy, iw = x, y, kw["w"]
        s.append(bold(ix, iy + 20, "SWARM", 13, "#f2f3f7", "start", 0.6))
        s.append(bold(ix + iw, iy + 20, "· 1/3", 6, DIM, "end", 0.8))
        s.append(mono(p, ix, iy + 33, "THRU · 4 DEVICES · C CHR"))
        s.append(mono(p, ix, iy + 50, "NEAR 50%  ·  FAR ░░░░░▓▓▓▓▓ NEAR", 5.4, DIM))
        s.append(mono(p, ix, iy + 71, "TIME  free · 120 BPM", 5.6, DIM))
        s.append(mono(p, ix, iy + 92, "PLUCK 0%  FM 16%  IMAGE 31%", 5.2, p.C))
write_preview(p, s, sys.argv[2], sys.argv[3])
