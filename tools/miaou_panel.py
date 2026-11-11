#!/usr/bin/env python3
"""The panel of MIAOU (MIAOU's toy sounds and its meow) in the GENRA look.

Usage:  miaou_panel.py real                    -> res/Miaou.svg (text as paths) + src/miaou/MiaouLayout.hpp
        miaou_panel.py preview OUTDIR RACK     -> OUTDIR/Miaou.png with Rack components and mock display content
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jpanel  # noqa: E402
from jpanel import Panel, io_row, write_real, write_preview, bold, mono, TXT, DIM, LINE, R_TRIM  # noqa: E402

jpanel.COLORS["Miaou"] = "#e58cf0"   # keep in step with src/miaou/Miaou.cpp: MIAOU_RGB

SOUNDS = ["PIANO", "BELLS", "ORGAN", "BANJO", "MEOW"]
TOY = ["DECAY", "TONE", "MEOW", "SWOOP", "BRTH", "KITTEN", "SPEAKER"]

# the cat (16 x 13): L fur, P inner ear, N nose, w whiskers; the eyes (rows 5-6, cols 3-5 and 10-12) and the mouth
# (rows 9-10, cols 6-9) are drawn on top (keep in step with src/miaou/Miaou.cpp: CAT)
CAT = ["..L..........L..",
       "..LL........LL..",
       ".LPLL......LLPL.",
       ".LLLLLLLLLLLLLL.",
       "LLLLLLLLLLLLLLLL",
       "LLLLLLLLLLLLLLLL",
       "LLLLLLLLLLLLLLLL",
       "LLLLLLLLLLLLLLLL",
       "LLLLLLLNNLLLLLLL",
       "wwLLLLLLLLLLLLww",
       "LLLLLLLLLLLLLLLL",
       ".LLLLLLLLLLLLLL.",
       "..LLLLLLLLLLLL.."]
CAT_PAL = {"L": "#e58cf0", "P": "#ffc6f5", "N": "#ff6fa8", "w": "#f2f3f7"}


def miaou_panel():
    p = Panel("Miaou", "MIAOU", 26, "TOY KEYS · MEOW")
    # the display: the cat left, the sound and the keys right
    p.e(f'<rect x="14" y="30" width="362" height="106" rx="4" fill="#07080a" stroke="{LINE}" stroke-width="1.2"/>')
    p.area("SCREEN", 19, 34, 96, 98)
    p.sep(120, 38, 128)
    p.area("INFO", 128, 34, 243, 98)
    # the head: PAW, the sound, LEVEL
    p.plate(14, 140, 362, 40)
    p.button("PAW", 34, 155, "PAW", light=True)
    p.sep(54, 146, 174)
    p.block(60, 143, 258, 34)
    p.text(189, 151, "SOUND", 4.6, p.C, 800, "middle", 1.6)
    p.leds("SOUND", 86, 160.5, 52, SOUNDS, 172.5)
    p.sep(324, 146, 174)
    p.knob("LEVEL", 350, 155, label="LEVEL", ly=175.5, v=0.8)
    # TOY: seven knobs, each with its CV
    p.plate(14, 184, 362, 82, True)
    p.title(195, 194.5, "TOY", 6.0, 3.0)
    for k in range(len(TOY)):
        x = 14 + 362 / len(TOY) * (k + 0.5)
        p.knob(f"T{k}", x, 211, v=[0.5, 0.5, 0.5, 0.5, 0.3, 0.5, 0.4][k])
        p.area(f"T_TEXT{k}", x - 362 / len(TOY) / 2, 224.5, 362 / len(TOY), 15)
        p.jack(f"T_IN{k}", x, 253.5)
    # FX
    p.plate(14, 270, 362, 42, True)
    p.title(195, 280, "FX", 5.0, 2.0)
    for i, (n, lab) in enumerate([("CHOR", "CHORUS"), ("DLY", "DELAY"), ("REV", "REVERB")]):
        p.knob(n, 14 + 362 / 3 * (i + 0.5), 294, R_TRIM, lab, 309.5, ring=False, v=0.2)
    io_row(p, [("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL"), ("PAW_IN", "PAW"), ("SOUND_IN", "SOUND")])
    return p


p = miaou_panel()
if len(sys.argv) < 2 or sys.argv[1] != "preview":
    write_real(p, "miaou_panel.py", "mlayout")
    sys.exit(0)

values = ["100%", "50%", "0.55s", "50%", "30%", "+3st", "40%"]
s = []
for kind, name, x, y, kw in p.comp:
    if kind == "area" and name.startswith("T_TEXT"):
        k = int(name[6:])
        cx = x + kw["w"] / 2
        s.append(bold(cx, y + 6.5, TOY[k], 5.3, TXT, "middle", 0.4))
        s.append(mono(p, cx, y + 13.5, values[k], 5.0, p.C, "middle"))
    elif kind == "area" and name == "SCREEN":
        P = 5.5
        ox, oy = x + (kw["w"] - 16 * P) / 2, y + (kw["h"] - 13 * P) / 2
        for r, row in enumerate(CAT):
            for c, ch in enumerate(row):
                if ch in CAT_PAL:
                    s.append(f'<rect x="{ox + c * P:.2f}" y="{oy + r * P:.2f}" width="{P + 0.1}" height="{P + 0.1}" fill="{CAT_PAL[ch]}"/>')
        for ex in (3, 10):
            s.append(f'<rect x="{ox + ex * P:.2f}" y="{oy + 5 * P:.2f}" width="{3 * P}" height="{2 * P}" fill="#f2f3f7"/>')
            s.append(f'<rect x="{ox + (ex + 2) * P:.2f}" y="{oy + 5 * P:.2f}" width="{P}" height="{2 * P}" fill="#111"/>')
        s.append(f'<rect x="{ox + 6 * P:.2f}" y="{oy + 9 * P:.2f}" width="{4 * P}" height="{2 * P}" fill="#5a2350"/>')
    elif kind == "area" and name == "INFO":
        ix, iy, iw = x, y, kw["w"]
        s.append(bold(ix, iy + 20, "MEOW", 13, "#f2f3f7", "start", 0.6))
        s.append(bold(ix + iw, iy + 20, "PAW ON", 6, p.C, "end", 0.8))
        s.append(mono(p, ix, iy + 33, "POLY · G4 · SPEAKER 40%"))
        kw_ = iw / 27
        for i in range(27):
            black = (48 + i) % 12 in (1, 3, 6, 8, 10)
            col = p.C if i == 19 else ("#1c1d23" if black else "#2a2b33")
            s.append(f'<rect x="{ix + i * kw_:.2f}" y="{iy + 42}" width="{kw_ - 1.4:.2f}" height="38" rx="1.3" fill="{col}"/>')
        s.append(mono(p, ix, iy + 93, "C3 · · · D5", 5.0, DIM))
write_preview(p, s, sys.argv[2], sys.argv[3])
