#!/usr/bin/env python3
"""The panel of MIAOU (the toy keyboard with the cat) in the instruments' look (tools/gpanel.py).

Usage:  miaou_panel.py real           -> res/Miaou.svg (text as paths) + src/miaou/MiaouLayout.hpp
        miaou_panel.py preview OUT.png
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpanel  # noqa: E402

T = ["DECAY", "TONE", "MEOW", "SWOOP", "BRTH", "KITTEN", "SPEAKER"]  # T0..T6; MEOW (T2) is the large one
# (the header's field picks the SOUND: PIANO BELLS ORGAN BANJO MEOW)
SPEC = dict(
    name="MIAOU", sub="GENRA · TOY KEYS", subshort="TOY KEYS", acc="#e58cf0", preset="05  MEOW", dh=20, screen=22,
    rows=[
        [dict(title="TOY", cols=[[("large", "T2", "MEOW")], [("knob", "T0", "DECAY"), ("small", "T4", "BRTH")],
                                [("knob", "T1", "TONE"), ("small", "T5", "KITTEN")],
                                [("knob", "T3", "SWOOP"), ("small", "T6", "SPEAKER")]]),
         dict(title="PLAY", cols=[[("button", "PAW", "PAW"), ("knob", "LEVEL", "LEVEL")]])],
        [dict(title="FX", cols=[[("knob", "CHOR", "CHORUS")], [("knob", "DLY", "DELAY")], [("knob", "REV", "REVERB")]])],
    ],
    strip=[("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL")], sync=[("PAW_IN", "PAW"), ("SOUND_IN", "SOUND")],
    cvs=[[(f"T_IN{k}", "SPKR" if k == 6 else T[k]) for k in range(7)]], outs=[("OUT_L", "L"), ("OUT_R", "R")])

p = gpanel.build(SPEC)
gpanel.write(p, "Miaou", "miaou", "mlayout", "miaou", "miaou_panel.py")
