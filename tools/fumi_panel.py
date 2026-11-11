#!/usr/bin/env python3
"""The panel of FUMI (FuMi-1's shigin koto on 6-operator FM) in the GENRA look.

Usage:  fumi_panel.py real                    -> res/Fumi.svg (text as paths) + src/fumi/FumiLayout.hpp
        fumi_panel.py preview OUTDIR RACK     -> OUTDIR/Fumi.png with Rack components and mock display content
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jpanel import Panel, R_TRIM, io_row, write_real, write_preview, bold, mono, TXT, DIM  # noqa: E402

SOUND = ["YOIN", "RATE", "VIB", "FEEL", "ORO", "CHAR", "REV"]
HANDS = ["VIB", "TRILL", "DAMP", "HARD", "OSHI", "HIKI", "ATO"]


def fumi_panel():
    p = Panel("Fumi", "FUMI", 26, "FUMI-1 · SHIGIN KOTO · FM6")
    p.display(14, 30, 362, 106)
    # the head: presets, key, tuning, scale, voice, level
    p.plate(14, 140, 362, 40)
    p.button("PREV", 28, 154)
    p.button("NEXT", 56, 154)
    p.pair_label(28, 56, 175.5, "PRESET")
    p.sep(74, 146, 174)
    for name, title, x, w in (("KEY_SEL", "KEY", 78, 48), ("TUNE_SEL", "TUNING", 130, 58), ("SCALE_SEL", "SCALE", 192, 52)):
        p.block(x, 143, w, 34)
        p.text(x + w / 2, 151, title, 4.6, p.C, 800, "middle", 1.6)
        p.area(name, x + 4, 155, w - 8, 17)
    p.sep(248, 146, 174)
    p.block(252, 143, 70, 34)
    p.text(287, 151, "VOICE", 4.6, p.C, 800, "middle", 1.6)
    p.leds("VOICE", 272, 160.5, 30, ["MONO", "POLY"], 172.5)
    p.sep(326, 146, 174)
    p.knob("LEVEL", 351, 155, label="LEVEL", ly=175.5, v=0.8)
    # SOUND: seven knobs with CV
    p.plate(14, 184, 362, 82, True)
    p.title(195, 194.5, "SOUND", 6.0)
    for k, lab in enumerate(SOUND):
        x = 14 + 362 / 7 * (k + 0.5)
        p.knob(f"S{k}", x, 211)
        p.area(f"S_TEXT{k}", x - 362 / 7 / 2, 224.5, 362 / 7, 15)
        p.jack(f"S_IN{k}", x, 253.5)
    # HANDS: the ornaments, a button and a gate each (VIB, TRILL latch)
    p.plate(14, 270, 362, 42, True)
    p.title(195, 280, "HANDS · ORNAMENTS", 5.0, 2.0)
    for k, lab in enumerate(HANDS):
        x = 14 + 362 / 7 * (k + 0.5)
        p.button(f"H{k}", x - 12, 294, light=True)
        p.jack(f"H_IN{k}", x + 12, 294)
        p.text(x, 309.5, lab, 4.7, TXT)
    io_row(p, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL"), ("BEND_IN", "BEND"), ("KEY_IN", "KEY")])
    return p


p = fumi_panel()
if len(sys.argv) < 2 or sys.argv[1] != "preview":
    write_real(p, "fumi_panel.py", "flayout")
    sys.exit(0)

values = ["2.4s", "85", "80", "100", "Long", "0", "55"]
s = []
for kind, name, x, y, kw in p.comp:
    if kind == "area" and name.startswith("S_TEXT"):
        k = int(name[6:])
        cx = x + kw["w"] / 2
        s.append(bold(cx, y + 6.5, SOUND[k], 5.3, TXT, "middle", 0.4))
        s.append(mono(p, cx, y + 13.5, values[k], 5.0, p.C, "middle"))
    elif kind == "area" and name.endswith("_SEL"):
        s.append(f'<rect x="{x}" y="{y}" width="{kw["w"]}" height="{kw["h"]}" rx="2.2" fill="#0d0e12" stroke="#34363f" stroke-width="0.8"/>')
        t = {"KEY_SEL": "1本 ▾", "TUNE_SEL": "SUIKO ▾", "SCALE_SEL": "IN ▾"}[name]
        s.append(f'<text x="{x + kw["w"] / 2}" y="{y + 11.5}" font-family="Noto Sans CJK JP" font-weight="700" font-size="6.2" '
                 f'fill="{TXT}" text-anchor="middle">{t}</text>')
    elif kind == "area" and name == "SCREEN":
        cx, h = x + kw["w"] / 2, kw["h"]
        s.append(f'<text x="{cx}" y="{y + 62}" font-family="Noto Sans CJK JP" font-weight="700" font-size="44" fill="{p.C}" '
                 f'text-anchor="middle">爪音</text>')
        s.append(f'<text x="{cx}" y="{y + 84}" font-family="Noto Sans CJK JP" font-size="9" fill="{DIM}" '
                 f'text-anchor="middle">つめおと</text>')
    elif kind == "area" and name == "INFO":
        ix, iy, iw = x, y, kw["w"]
        s.append(bold(ix, iy + 20, "KOTO PLUCK", 13, "#f2f3f7", "start", 0.6))
        s.append(bold(ix + iw, iy + 20, "· 1/6", 6, DIM, "end", 0.8))
        s.append(mono(p, ix, iy + 33, "1本 · 三 = A3 · SUIKO · IN"))
        kw2 = iw / 16
        deg = ["mi", "fa", "la", "ti", "do"]
        for i in range(16):
            col = p.C if i == 7 else ("#2d3160" if i % 5 == 0 else "#22232a")
            s.append(f'<rect x="{ix + i * kw2:.1f}" y="{iy + 42}" width="{kw2 - 1.6:.1f}" height="24" rx="1.6" fill="{col}"/>')
            s.append(mono(p, ix + i * kw2 + (kw2 - 1.6) / 2, iy + 62, deg[i % 5], 4.2, "#f2f3f7" if i == 7 else DIM, "middle"))
        s.append(mono(p, ix, iy + 80, "VOICES 2/8", 5.6, DIM))
        s.append(mono(p, ix + iw, iy + 80, "VIB · TRILL 颪", 5.6, p.C, "end"))
write_preview(p, s, sys.argv[2], sys.argv[3])
