#!/usr/bin/env python3
"""The panel of SWARM (JIANT FM's device swarm) in the instruments' look (tools/gpanel.py).

Usage:  swarm_panel.py real           -> res/Swarm.svg + src/swarm/SwarmLayout.hpp
        swarm_panel.py preview OUT.png
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpanel  # noqa: E402

E = ["PLUCK", "FM", "IMAGE", "DRIFT", "OCTAVE", "CHAOS"]  # (the widget draws the names)
SPEC = dict(
    name="SWARM", sub="GENRA · DEVICE SWARM", subshort="DEVICE SWARM", acc="#13b8a6", preset="01  SWARM", dh=16, screen=26,
    rows=[
        [dict(title="ENGINE", cols=[[("large", "E0", E[0], "E_TEXT0")], [("knob", "E1", E[1], "E_TEXT1"), ("small", "E4", E[4], "E_TEXT4")],
                                   [("knob", "E2", E[2], "E_TEXT2"), ("small", "E5", E[5], "E_TEXT5")], [("knob", "E3", E[3], "E_TEXT3")]]),
         dict(title="PLAY", cols=[[("button", "NEW", "NEW")], [("knob", "VOICES", "VOICES")], [("knob", "LEVEL", "LEVEL")]],
              lamps=[("PLAY", "", ["THRU", "KEYS"])])],
        [dict(title="AIR", cols=[[("small", "BUSY", "BUSY")], [("small", "MOVE", "MOVE")], [("small", "NEAR", "NEAR")]]),
         dict(title="FX", cols=[[("small", "DIST", "DIST")], [("small", "CHOR", "CHORUS")], [("small", "DLY", "DELAY")],
                                [("small", "REV", "REVERB")]])],
    ],
    strip=[("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL")], sync=[("CLOCK", "CLOCK"), ("RESET", "RESET")],
    cvs=[[("NEAR_IN", "NEAR"), ("ARRIVE_IN", "ARRIVE"), ("NEW_IN", "NEW")], [(f"E_IN{k}", f"~E_INL{k}:{E[k]}") for k in range(6)]],
    outs=[("OUT_L", "L"), ("OUT_R", "R")])

p = gpanel.build(SPEC)
gpanel.write(p, "Swarm", "swarm", "slayout", "swarm", "swarm_panel.py")
