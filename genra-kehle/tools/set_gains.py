#!/usr/bin/env python3
"""Writes the preset levels measured by test/kehle_test.cpp into src/kehle/KehlePresets.hpp (the /*gain*/ field, in order).

    g++ -O2 -std=c++11 -I. test/kehle_test.cpp -o build/kehle_test && python3 tools/set_gains.py build/kehle_test
(from genra-kehle/; the genra/ folder must be there: make genra-src)."""
import re
import subprocess
import sys

exe = sys.argv[1] if len(sys.argv) > 1 else "build/kehle_test"
gains = {}
for line in subprocess.run([exe, "gains"], capture_output=True, text=True).stdout.split("\n"):
    p = line.split()
    if len(p) == 3 and p[0] == "KEHLE":
        gains[int(p[1])] = float(p[2])
path = "src/kehle/KehlePresets.hpp"
s = open(path).read()
i = [0]


def rep(m):
    g = gains.get(i[0])
    i[0] += 1
    return "/*gain*/ %.3ff" % g if g is not None else m.group(0)


s = re.sub(r"/\*gain\*/ [0-9.]+f", rep, s)
open(path, "w").write(s)
print("KEHLE", i[0], "presets")
