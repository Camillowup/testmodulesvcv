#!/usr/bin/env python3
"""The panel of CANTA (MIKU-FM's singing voice) in the instruments' look (tools/gpanel.py).

Usage:  canta_panel.py real           -> res/Canta.svg (text as paths), src/canta/CantaLayout.hpp and
                                         src/canta/CantaSprites.hpp (the singers, tools/canta_sprites.py)
        canta_panel.py preview OUT.png
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpanel  # noqa: E402
import canta_sprites  # noqa: E402

V = ["BREATH", "BRIGHT", "GLIDE", "TENSE", "FRY", "WHISPR", "FORMNT", "HUMAN"]  # V0..V7; FORMANT (V6) is the large one
SPEC = dict(
    name="CANTA", sub="GENRA · SINGING VOICE", subshort="SINGING VOICE", acc="#ff7a59", preset="03  KIKO SONG", dh=16,
    screen=20,
    rows=[
        [dict(title="VOICE", cols=[[("large", "V6", "FORMANT"), ("small", "V3", "TENSE")], [("knob", "V0", "BREATH"), ("small", "V4", "FRY")],
                                  [("knob", "V1", "BRIGHT"), ("small", "V5", "WHISPR")], [("knob", "V2", "GLIDE"), ("small", "V7", "HUMAN")]],
              lamps=[("CHR", "", ["DEEP", "LOW", "MID", "YOUNG", "TINY"])]),
         dict(title="PLAY", fieldw=25, fields=[[("LYRF", "FOLLOW", "LYRICS")], [("SYLF", "ra", "SYLLABLE"), ("HARMF", "+4", "HARMONY")]],
              cols=[[("button", "CALL", "CALL")], [("knob", "MIX", "MIX")], [("knob", "LEVEL", "LEVEL")]],
              lamps=[("LANG", "", ["JAPAN", "ANIMAL", "SCAT", "VOWEL", "DANCE"])])],
        [dict(title="SHAPE", cols=[[("small", "ATK", "ATK")], [("small", "REL", "REL")], [("small", "SCOOP", "SCOOP")],
                                  [("small", "VIB", "VIB")]]),
         dict(title="FX", cols=[[("small", "CHOR", "CHORUS")], [("small", "DLY", "DELAY")], [("small", "REV", "REVERB")],
                                [("small", "DRV", "DRIVE")]])],
    ],
    strip=[("VOCT", "V/OCT"), ("GATE", "GATE"), ("VEL", "VEL")], sync=[("CLOCK", "CLOCK"), ("RESET", "RESET")],
    cvs=[[("SYL_IN", "SYL"), ("NEXT_IN", "NEXT")], [(f"V_IN{k}", V[k]) for k in range(8)]],
    outs=[("OUT_L", "L"), ("OUT_R", "R")])

p = gpanel.build(SPEC)
gpanel.write(p, "Canta", "canta", "klayout", "canta", "canta_panel.py")
if len(sys.argv) < 2 or sys.argv[1] != "preview":
    open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "canta", "CantaSprites.hpp"), "w").write(
        canta_sprites.header())
    print("written: src/canta/CantaSprites.hpp")
