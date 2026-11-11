#!/usr/bin/env python3
"""The panel of FUMI (the shigin koto) in the instruments' look (tools/gpanel.py).

Usage:  fumi_panel.py real           -> res/Fumi.svg (text as paths) + src/fumi/FumiLayout.hpp
        fumi_panel.py preview OUT.png
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpanel  # noqa: E402

S = ["YOIN", "RATE", "VIB", "FEEL", "ORO", "CHAR", "REV"]   # S0..S6; YOIN (S0) is the large one
H = ["VIB", "TRILL", "DAMP", "HARD", "OSHI", "HIKI", "ATO"]  # the hands H0..H6 (buttons with their lights)
SPEC = dict(
    name="FUMI", sub="GENRA · SHIGIN KOTO", subshort="SHIGIN KOTO", acc="#6f7cf5", preset="01  KOTO PLUCK", dh=18, screen=24,
    rows=[
        [dict(title="SOUND", cols=[[("large", "S0", "YOIN")], [("knob", "S1", "RATE"), ("small", "S4", "ORO")],
                                  [("knob", "S2", "VIB"), ("small", "S5", "CHAR")], [("knob", "S3", "FEEL"), ("small", "S6", "REV")]]),
         dict(title="TUNING · KEY", fieldw=24, fields=[[("TUNEF", "SUIKO", "TUNING"), ("KEYF", "1", "KEY")]],
              cols=[[("knob", "LEVEL", "LEVEL")]],
              lamps=[("SCALE", "", ["IN", "YO", "MIN'YO"]), ("VOICE", "", ["MONO", "POLY"])])],
        [dict(title="HANDS", cols=[[("button", f"H{k}", H[k])] for k in range(7)])],
    ],
    strip=[("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL")], sync=[("BEND_IN", "BEND"), ("KEY_IN", "KEY")],
    cvs=[[(f"S_IN{k}", S[k]) for k in range(7)], [(f"H_IN{k}", H[k]) for k in range(7)]],
    outs=[("OUT_L", "L"), ("OUT_R", "R")], jackrows=3)

p = gpanel.build(SPEC)
gpanel.write(p, "Fumi", "fumi", "flayout", "fumi", "fumi_panel.py")
