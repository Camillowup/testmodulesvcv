#!/usr/bin/env python3
"""The panel of CANTA (MIKU-FM's singing voice) in the GENRA look.

Usage:  canta_panel.py real                    -> res/Canta.svg (text as paths), src/canta/CantaLayout.hpp and
                                                  src/canta/CantaSprites.hpp (the singers, tools/canta_sprites.py)
        canta_panel.py preview OUTDIR RACK     -> OUTDIR/Canta.png with Rack components and mock display content
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jpanel import Panel, R_TRIM, io_row, write_real, write_preview, bold, mono, chips, TXT, DIM, ROOT  # noqa: E402
import canta_sprites  # noqa: E402

VOICE = ["BREATH", "BRIGHT", "GLIDE", "TENSE", "FRY", "WHISPR", "FORMNT", "HUMAN"]


def canta_panel():
    p = Panel("Canta", "CANTA", 26, "MIKU-FM · SINGING VOICE")
    p.display(14, 30, 362, 106)
    p.plate(14, 140, 362, 40)
    p.button("PREV", 27, 154, "◀ PRE")
    p.button("NEXT_BTN", 52, 154, "PRE ▶")
    p.button("CALL", 77, 154, "CALL", light=True)
    p.sep(92, 146, 174)
    for name, title, x, w in (("LYR_SEL", "LYRICS", 96, 54), ("SYL_SEL", "SYLLABLE", 154, 48), ("HARM_SEL", "HARMONY", 206, 48)):
        p.block(x, 143, w, 34)
        p.text(x + w / 2, 151, title, 4.6, p.C, 800, "middle", 1.6)
        p.area(name, x + 4, 155, w - 8, 17)
    p.sep(259, 146, 174)
    p.knob("MIX", 287, 155, label="MIX", ly=175.5, v=0.7)
    p.sep(316, 146, 174)
    p.knob("LEVEL", 346, 155, label="LEVEL", ly=175.5, v=0.79)
    # VOICE: eight knobs with CV
    p.plate(14, 184, 362, 82, True)
    p.title(195, 194.5, "VOICE", 6.0)
    for k in range(8):
        x = 14 + 362 / 8 * (k + 0.5)
        p.knob(f"V{k}", x, 211)
        p.area(f"V_TEXT{k}", x - 362 / 16, 224.5, 362 / 8, 15)
        p.jack(f"V_IN{k}", x, 253.5)
    # SHAPE and FX
    p.plate(14, 270, 178, 42, True)
    p.title(103, 280, "SHAPE", 5.0, 2.0)
    p.plate(198, 270, 178, 42, True)
    p.title(287, 280, "FX", 5.0, 2.0)
    for i, (n, lab) in enumerate([("ATK", "ATK"), ("REL", "REL"), ("SCOOP", "SCOOP"), ("VIB", "VIB")]):
        p.knob(n, 14 + 178 / 4 * (i + 0.5), 294, R_TRIM, lab, 309.5, ring=False)
    for i, (n, lab) in enumerate([("CHOR", "CHORUS"), ("DLY", "DELAY"), ("REV", "REVERB"), ("DRV", "DRIVE")]):
        p.knob(n, 198 + 178 / 4 * (i + 0.5), 294, R_TRIM, lab, 309.5, ring=False)
    io_row(p, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL"), ("CLOCK", "CLOCK"), ("RESET", "RESET"),
               ("SYL_IN", "SYL"), ("NEXT_IN", "NEXT")])
    return p


p = canta_panel()
if len(sys.argv) < 2 or sys.argv[1] != "preview":
    write_real(p, "canta_panel.py", "klayout")
    open(os.path.join(ROOT, "src", "canta", "CantaSprites.hpp"), "w").write(canta_sprites.header())
    print("written: src/canta/CantaSprites.hpp")
    sys.exit(0)

values = ["6%", "79%", "50 ms", "0", "0%", "0%", "0", "63%"]
lyr = ["ka", "ze", "ni", "no", "ru", "so", "ra", "e"]
s = []
for kind, name, x, y, kw in p.comp:
    if kind == "area" and name.startswith("V_TEXT"):
        k = int(name[6:])
        cx = x + kw["w"] / 2
        s.append(bold(cx, y + 6.5, VOICE[k], 5.3, TXT, "middle", 0.4))
        s.append(mono(p, cx, y + 13.5, values[k], 5.0, p.C, "middle"))
    elif kind == "area" and name.endswith("_SEL"):
        lab = {"LYR_SEL": "FOLLOW ▾", "SYL_SEL": "ra ▾", "HARM_SEL": "+4 ▾"}[name]
        s.append(f'<rect x="{x}" y="{y}" width="{kw["w"]}" height="{kw["h"]}" rx="2.2" fill="#0d0e12" stroke="#34363f" stroke-width="0.8"/>')
        s.append(bold(x + kw["w"] / 2, y + 11.5, lab, 6.2, TXT, "middle", 0.4))
    elif kind == "area" and name == "SCREEN":
        s.append(canta_sprites.svg(x + 4, y + 7, 4, 0))
        cx, cy, r = x + 114, y + kw["h"] / 2, 31
        s.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="{p.C}" stroke-opacity="0.35"/>')
        for i, m in enumerate(lyr):
            a = -math.pi / 2 + i * 2 * math.pi / len(lyr)
            on = i == 0
            s.append(bold(cx + math.cos(a) * r, cy + math.sin(a) * r + 2.5, m, 8 if on else 6,
                          p.C if on else DIM, "middle", 0.2))
        s.append(bold(cx, cy + 5, "KA", 14, "#f2f3f7", "middle", 0.6))
    elif kind == "area" and name == "INFO":
        ix, iy, iw = x, y, kw["w"]
        s.append(bold(ix, iy + 18, "KIKO", 13, "#f2f3f7", "start", 0.6))
        s.append(bold(ix + iw, iy + 18, "· 1/12", 6, DIM, "end", 0.8))
        s.append(mono(p, ix, iy + 29, "JAPANESE · MID · HARMONY +4"))
        s.append(chips(ix, iy + 35, ["JAPAN", "ANIMAL", "SCAT", "VOWEL"], {0}, 32.5, 12, p.C, 3, 4.8))
        s.append(chips(ix + 146, iy + 35, ["DANCE"], set(), iw - 146, 12, p.C, 3, 4.8))
        s.append(chips(ix, iy + 51, ["DEEP", "LOW", "MID", "YOUNG", "TINY"], {2}, (iw - 12) / 5, 12, p.C, 3, 4.8))
        s.append(f'<rect x="{ix}" y="{iy + 67}" width="{iw}" height="27" rx="2.2" fill="#0d0e12" stroke="#34363f" stroke-width="0.8"/>')
        s.append(mono(p, ix + 4, iy + 77, "LYRICS · CLICK TO EDIT", 5.0, DIM))
        line = [("ka", True), ("ze", False), ("ni", False), ("no-ru", False), ("|", False), ("so-ra", False), ("e", False), ("to-bu", False)]
        tx = ix + 4
        for t, on in line:
            s.append(mono(p, tx, iy + 89, t, 7.0, p.C if on else TXT))
            tx += len(t) * 4.3 + 5
write_preview(p, s, sys.argv[2], sys.argv[3])
