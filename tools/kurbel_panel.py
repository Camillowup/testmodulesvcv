#!/usr/bin/env python3
"""The panel of KURBEL (ESTAMPIE's hurdy-gurdy and shawm) in the instruments' look (tools/gpanel.py).

Usage:  kurbel_panel.py real           -> res/Kurbel.svg (text as paths) + src/kurbel/KurbelLayout.hpp
        kurbel_panel.py preview OUT.png
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpanel  # noqa: E402

E = ["D LVL", "5TH", "WHEEL", "BUZZ", "BODY", "WOBBLE"]  # (the widget draws the names: they follow the engine)
SPEC = dict(
    name="KURBEL", sub="GENRA · HURDY-GURDY", subshort="HURDY-GURDY", acc="#c25a1c", preset="01  HURDY GURDY", dh=20,
    screen=22,
    rows=[
        [dict(title="ENGINE", fieldw=25, fields=[[("ENGF", "WHEEL", "ENGINE"), ("TEMPF", "PYTH", "TEMP")]],
              cols=[[("large", "E2", E[2], "E_TEXT2")], [("knob", "E0", E[0], "E_TEXT0"), ("small", "E4", E[4], "E_TEXT4")],
                    [("knob", "E1", E[1], "E_TEXT1"), ("small", "E5", E[5], "E_TEXT5")], [("knob", "E3", E[3], "E_TEXT3")]]),
         dict(title="PLAY", fieldw=23, fields=[[("DRONEF", "D", "DRONE"), ("COUPF", "1/8", "COUP")]],
              cols=[[("knob", "CRANK", "CRANK")], [("button", "CALL", "CALL"), ("button", "RING", "RING")],
                    [("knob", "LEVEL", "LEVEL")]])],
        [dict(title="ENVELOPE", cols=[[("small", "ATK", "A")], [("small", "DEC", "D")], [("small", "SUS", "S")], [("small", "REL", "R")]]),
         dict(title="FX", cols=[[("small", "DIST", "DIST")], [("small", "CHOR", "CHORUS")], [("small", "DLY", "DELAY")],
                                [("small", "REV", "REVERB")]])],
    ],
    strip=[("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL")], sync=[("CLOCK", "CLOCK"), ("RESET", "RESET")],
    cvs=[[("CRANK_IN", "CRANK"), ("BUZZ_IN", "BUZZ")] + [(f"E_IN{k}", f"~E_INL{k}:{E[k]}") for k in range(6)]],
    outs=[("OUT_L", "L"), ("OUT_R", "R")])

p = gpanel.build(SPEC)
gpanel.write(p, "Kurbel", "kurbel", "klayout", "kurbel", "kurbel_panel.py")
