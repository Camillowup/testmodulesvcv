#!/usr/bin/env python3
"""The panel of MATH (bytebeat / floatbeat from JIANT FM) in the instruments' look (tools/gpanel.py).

Usage:  math_panel.py real           -> res/Math.svg (text as paths) + src/math/MathLayout.hpp
        math_panel.py preview OUT.png
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpanel  # noqa: E402

E = ["ALGO", "VAR", "MOTION", "GRIT", "BEND", "LOOP"]  # (the widget draws the names: they follow WAVE)
SPEC = dict(
    name="MATH", sub="GENRA · BYTEBEAT", subshort="BYTEBEAT", acc="#c0549a", preset="02  BYTE VAR", dh=14, screen=20,
    rows=[
        [dict(title="ENGINE", fields=[[("WAVEF", "BYTE", "WAVE"), ("CHIPF", "8BIT", "CHIP")]],
              cols=[[("large", "E0", E[0], "E_TEXT0")], [("knob", "E1", E[1], "E_TEXT1"), ("small", "E4", E[4], "E_TEXT4")],
                    [("knob", "E2", E[2], "E_TEXT2"), ("small", "E5", E[5], "E_TEXT5")], [("knob", "E3", E[3], "E_TEXT3")]]),
         dict(title="PLAY", cols=[[("button", "CALL", "CALL")], [("knob", "LEVEL", "LEVEL")]],
              lamps=[("PLAY", "", ["KEYS", "THRU"]), ("VOICE", "", ["POLY", "MONO", "LEGATO"])])],
        [dict(title="ENVELOPE", cols=[[("small", "ATK", "A")], [("small", "DEC", "D")], [("small", "SUS", "S")], [("small", "REL", "R")]]),
         dict(title="FX", cols=[[("small", "DIST", "DIST")], [("small", "CHOR", "CHORUS")], [("small", "DLY", "DELAY")],
                                [("small", "REV", "REVERB")]])],
    ],
    strip=[("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL")], sync=[("CLOCK", "CLOCK"), ("RESET", "RESET")],
    cvs=[[(f"E_IN{k}", f"~E_INL{k}:{E[k]}") for k in range(6)]], outs=[("OUT_L", "L"), ("OUT_R", "R")])

p = gpanel.build(SPEC)
gpanel.write(p, "Math", "math", "mlayout", "math", "math_panel.py")
