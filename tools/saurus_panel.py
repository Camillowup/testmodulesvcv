#!/usr/bin/env python3
"""The panel of SAURUS (PALEOTONE's creature calls) in the instruments' look (tools/gpanel.py).

Usage:  saurus_panel.py real           -> res/Saurus.svg (text as paths) + src/saurus/SaurusLayout.hpp
        saurus_panel.py preview OUT.png
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpanel  # noqa: E402

# the engine's seven in their order E0..E6; GROWL (E3) is the large one
T = ["SIZE", "SWOOP", "LEN", "GROWL", "BRTH", "MOUTH", "WILD"]
SPEC = dict(
    name="SAURUS", sub="GENRA · CALLS", subshort="CALLS", acc="#5c9c3a", preset="02  TYRANT ROAR", dh=20, screen=22,
    rows=[
        [dict(title="THROAT", cols=[[("large", "E3", "GROWL")], [("knob", "E0", "SIZE"), ("small", "E4", "BRTH")],
                                   [("knob", "E1", "SWOOP"), ("small", "E5", "MOUTH")],
                                   [("knob", "E2", "LEN"), ("small", "E6", "WILD")]]),
         dict(title="PLAY", field=("TYPEF", "ROAR", "TYPE"), cols=[[("button", "CALL", "CALL")], [("knob", "LEVEL", "LEVEL")]],
              lamps=[("VOICE", "", ["POLY", "MONO", "LEGATO"])])],
        [dict(title="ENVELOPE", cols=[[("small", "ATK", "A")], [("small", "DEC", "D")], [("small", "SUS", "S")], [("small", "REL", "R")]]),
         dict(title="FX", cols=[[("small", "DIST", "DIST")], [("small", "CHOR", "CHORUS")], [("small", "DLY", "DELAY")],
                                [("small", "REV", "REVERB")]])],
    ],
    strip=[("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL")], sync=[("CALL_IN", "CALL"), ("TYPE_IN", "TYPE")],
    cvs=[[(f"E_IN{k}", T[k]) for k in range(7)]], outs=[("OUT_L", "L"), ("OUT_R", "R")])

p = gpanel.build(SPEC)
gpanel.write(p, "Saurus", "saurus", "slayout", "saurus", "saurus_panel.py")
