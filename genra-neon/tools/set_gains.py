#!/usr/bin/env python3
"""Writes the preset levels measured by test/neon_test.cpp into the preset files (the /*gain*/ field, in order).

    g++ -O2 -std=c++11 -I. test/neon_test.cpp -o build/neon_test && python3 tools/set_gains.py build/neon_test
(from genra-neon/; the genra/ folder must be there: make genra-src)."""
import re
import subprocess
import sys

exe = sys.argv[1] if len(sys.argv) > 1 else "build/neon_test"
out = subprocess.run([exe, "gains"], capture_output=True, text=True).stdout.split("\n")
MODS = (("WELLE", "src/welle/WellePresets.hpp"), ("NEBEL", "src/nebel/NebelPresets.hpp"), ("WOGE", "src/woge/WogePresets.hpp"),
        ("PULS", "src/puls/PulsPresets.hpp"), ("GLAS", "src/glas/GlasPresets.hpp"), ("FLUT", "src/flut/FlutPresets.hpp"))
gains = {m: {} for m, _ in MODS}
for line in out:
    p = line.split()
    if len(p) == 3:
        gains[p[0]][int(p[1])] = float(p[2])
for mod, path in MODS:
    s = open(path).read()
    i = [0]

    def rep(m):
        g = gains[mod].get(i[0])
        i[0] += 1
        return "/*gain*/ %.3ff" % g if g is not None else m.group(0)
    s = re.sub(r"/\*gain\*/ [0-9.]+f", rep, s)
    open(path, "w").write(s)
    print(mod, i[0], "presets")
