#!/usr/bin/env python3
"""KRATZ (24 HP, GENRA TECHNO look): writes res/Kratz.svg, src/KratzLayout.hpp, docs/panels/kratz.png.

Draft 5 as Bastian approved it (24 Nov 2026): the record in the display, two rows of fields (PAT, RATE / SYNC, TUNE,
VIN|STR), five knobs, seven keys, eight cues, the faders LVL and SCR on the right, ten jacks per row with the outputs
on their plates at the bottom right.
"""
from tpanel import *

AC = "#a78bfa"  # KRATZ's accent (violet)
P = Panel("Kratz", 24, "KRATZ", "SCRATCH", AC)
W = P.W
L = 5.0          # left column
LW = 92.0        # its width (to 97)

P.button("B_LOAD", W - 25, 3.8, 20, 6.4, "LOAD", DISP, TXT, 1.5)
P.rect(W - 25, 3.8, 20, 6.4, "none", 1.0, AC, 0.35)

# ---- the display: the record, the lamps, the line at the bottom
dx, dy, dw, dh = 5, 12, W - 10, 36
P.display("DISP", dx, dy, dw, dh)
cx, cy, r = dx + 38, dy + 17.5, 15.5
P.p(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="#141519" stroke="{AC}" stroke-width="0.45"/>')
for rr in (r * 0.8, r * 0.62):
    P.p(f'<circle cx="{cx}" cy="{cy}" r="{rr:.2f}" fill="none" stroke="#212329" stroke-width="0.3"/>')
P.p(f'<circle cx="{cx}" cy="{cy}" r="{r * 0.3:.2f}" fill="#2c2e36" stroke="{AC}" stroke-width="0.3"/>')
P.p(f'<line x1="{cx}" y1="{cy}" x2="{cx}" y2="{cy - r + 1}" stroke="{AC}" stroke-width="0.6"/>')
for k, (lb, on) in enumerate((("HOLD", True), ("SLIP", True), ("LOOP 1/2", True), ("REV", False), ("MOTION", False))):
    yy = dy + 5 + k * 4.8
    P.p(f'<circle cx="{dx + 72}" cy="{yy - 0.5}" r="0.9" fill="{"#3d8fe0" if on else "#34363f"}"/>')
    P.text(dx + 74.5, yy, lb, 1.4, TXT if on else "#555a66", 700, "start", check=False, preview=True)
P.text(dx + 2.5, dy + dh - 2.2, "AHH_FRESH.WAV", 1.4, DIM, 700, "start", check=False, preview=True)
P.text(dx + dw - 2.5, dy + dh - 2.2, "SMP 125 › GENRA 128 · A MIN", 1.4, AC, 700, "end", check=False, preview=True)

# ---- the fields (two rows) and VIN | STR
fy1, fy2, fh = 50.5, 57.5, 5.6
P.field("F_PAT", L, fy1, 58, fh, "PAT  2-CLICK FLARE", AC)
P.field("F_RATE", L + 60, fy1, LW - 60, fh, "RATE  1/8", AC)
P.field("F_SYNC", L, fy2, 32, fh, "SYNC  GENRA", AC)
P.field("F_TUNE", L + 34, fy2, 32, fh, "TUNE  BASS", AC)
P.rect(L + 68, fy2, LW - 68, fh, DISP, 0.8, AC, 0.35)
bw2 = (LW - 68 - 1.6) / 2
P.button("B_VIN", L + 68.4, fy2 + 0.4, bw2, fh - 0.8, "VIN", "#185fa5", "#ffffff", 1.35)
P.button("B_STR", L + 68.4 + bw2 + 0.8, fy2 + 0.4, bw2, fh - 0.8, "STR", DISP, DIM, 1.35)

# ---- the knobs
ky = 69.6
for k, (n, lb) in enumerate((("K_SPEED", "SPEED"), ("K_PITCH", "PITCH"), ("K_DRAG", "DRAG"), ("K_TONE", "TONE"),
                             ("K_RANGE", "RANGE"))):
    P.knob(n, 12 + k * 18.5, ky, "medium", AC if n == "K_RANGE" else None, lb)

# ---- the keys and the cues
by1, by2, bh = 79.0, 85.8, 5.6
w4 = (LW - 3 * 1.4) / 4
for k, (n, lb, fill, tc) in enumerate((("B_MOTION", "MOTION", "#2c2e36", TXT), ("B_HOLD", "HOLD", "#185fa5", "#ffffff"),
                                     ("B_SLIP", "SLIP", "#185fa5", "#ffffff"), ("B_LOOP", "LOOP", "#2c2e36", AC))):
    P.button(n, L + k * (w4 + 1.4), by1, w4, bh, lb, fill, tc, 1.4)
w3 = (LW - 2 * 1.4) / 3
for k, (n, lb, fill, tc) in enumerate((("B_REV", "REV", "#2c2e36", TXT), ("B_REC", "REC", "#2c2e36", RED),
                                     ("B_CUT", "CUT", AC, "#17181d"))):
    P.button(n, L + k * (w3 + 1.4), by2, w3, bh, lb, fill, tc, 1.4)
cw = (LW - 7 * 1.2) / 8
for i in range(8):
    P.button(f"B_CUE{i + 1}", L + i * (cw + 1.2), 92.8, cw, bh, str(i + 1), "#212329", TXT, 1.4)

# ---- the faders
for n, x, lb, cap in (("A_LVL", 104.0, "LVL", "#d9dbe2"), ("A_SCR", 113.5, "SCR", AC)):
    P.rect(x - 0.7, 52.0, 1.4, 43.0, DISP, 0.6, LINE, 0.25)
    for t in range(9):
        yy = 52.0 + 43.0 * t / 8
        P.rect(x - 2.6, yy - 0.1, 1.0, 0.2, "#34363f", 0)
    P.area(n, x - 3.5, 50.5, 7.0, 46.0)
    P.text(x, 99.6, lb, 1.35, DIM, 700)
    cy0 = 60 if n == "A_LVL" else 78
    P.rect(x - 3.2, cy0 - 1.8, 6.4, 3.6, cap, 0.6, preview=True)

# ---- the jacks: inputs and triggers on the left, the outputs on their plates at the bottom right
P.strip()
Y1, Y2 = 108.6, 121.4
row1 = [("IN_L", "IN L"), ("IN_R", "IN R"), None, ("IN_CLK", "CLK"), ("IN_RST", "RST"), None, ("IN_REC", "REC"),
        ("IN_SCR", "SCR"), ("IN_LVL", "LVL"), ("IN_TUNE", "TUNE"), ("IN_SPD", "SPD"), ("IN_VOCT", "V/OCT")]
x = 9.0
for it in row1:
    if it is None:
        x += 2.0
        continue
    P.jack(it[0], x, Y1, it[1])
    if it[0] in ("IN_L", "IN_R"):
        P.rect(x - 3.0, Y1 - 4.6, 6.0, 0.5, mix(AC, 0.45, "#141519"), 0.2)
    elif it[0] in ("IN_CLK", "IN_RST"):
        P.rect(x - 3.0, Y1 - 4.6, 6.0, 0.5, "#5a5c66", 0.2)
    x += J
for k, (n, lb) in enumerate((("IN_PAT", "PAT"), ("IN_CUE", "CUE"), ("IN_HOLD", "HOLD"), ("IN_CUT", "CUT"),
                             ("IN_LOOP", "LOOP"), ("IN_MOT", "MOT"), ("IN_SLIP", "SLIP"), ("IN_REV", "REV"))):
    P.jack(n, 9.0 + k * J, Y2, lb)
ox = W - 9.0 - J
for k, (n, lb) in enumerate((("OUT_L", "OUT L"), ("OUT_R", "OUT R"))):
    P.jack(n, ox + k * J, Y2, lb, mix(AC, 0.32), True)
P.finish(1400)
