#!/usr/bin/env python3
"""KESSEL's factory kits -> src/kessel/KesselKits.hpp.

Every kit starts from the LXR's init kit (the values below in BASE) and changes what its style needs. The voices are
then made as loud as they go: each voice is struck at full velocity in the real core (a build of kesselcore, loaded
with ctypes) and its LEVEL set so that its peak lands at TARGET (the LXR's saturating drive then does the rest).

Usage:  kessel_kits.py CORE.so      (CORE.so: kesselcore built for this machine, e.g. by `make res/kesselcore.so`)
"""
import ctypes
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SLOTS = ("WAVE TUNE FM FMFREQ CLICK PAMT PDEC PAN FTYPE CUT RES DRIVE LEVEL ATK DEC DECOPEN MODWAVE TRANSWAVE "
         "TRANSFREQ MIXOSC SLOPE PSLOPE DECIM FILTDRIVE FINE").split()
S = {n: i for i, n in enumerate(SLOTS)}
VOICES = ["DRUM1", "DRUM2", "DRUM3", "SNARE", "CYM", "HAT"]  # kick, tom, rim, snare, cymbal, hi-hat
TARGET = 0.92

# the LXR's init kit per voice (kessel/Core/Src/Preset/initKit.c, read through the slot table)
BASE = {
    "DRUM1": dict(WAVE=0, TUNE=31, FM=0, FMFREQ=0, CLICK=0, PAMT=44, PDEC=26, PAN=63, FTYPE=0, CUT=127, RES=0, DRIVE=63,
                  LEVEL=127, ATK=0, DEC=21, DECOPEN=0, MODWAVE=0, TRANSWAVE=0, TRANSFREQ=0, MIXOSC=0, SLOPE=46,
                  PSLOPE=22, DECIM=127, FILTDRIVE=0, FINE=63),
    "DRUM2": dict(WAVE=0, TUNE=41, FM=0, FMFREQ=0, CLICK=0, PAMT=14, PDEC=93, PAN=63, FTYPE=0, CUT=127, RES=0, DRIVE=63,
                  LEVEL=127, ATK=0, DEC=13, DECOPEN=0, MODWAVE=0, TRANSWAVE=0, TRANSFREQ=0, MIXOSC=0, SLOPE=11,
                  PSLOPE=10, DECIM=127, FILTDRIVE=0, FINE=63),
    "DRUM3": dict(WAVE=0, TUNE=58, FM=0, FMFREQ=0, CLICK=0, PAMT=127, PDEC=7, PAN=63, FTYPE=0, CUT=127, RES=0,
                  DRIVE=63, LEVEL=127, ATK=0, DEC=3, DECOPEN=0, MODWAVE=0, TRANSWAVE=0, TRANSFREQ=0, MIXOSC=0, SLOPE=4,
                  PSLOPE=14, DECIM=127, FILTDRIVE=0, FINE=63),
    "SNARE": dict(WAVE=1, TUNE=45, FM=61, FMFREQ=127, CLICK=0, PAMT=81, PDEC=14, PAN=63, FTYPE=0, CUT=127, RES=0,
                  DRIVE=63, LEVEL=127, ATK=0, DEC=10, DECOPEN=0, MODWAVE=0, TRANSWAVE=0, TRANSFREQ=0, MIXOSC=0,
                  SLOPE=10, PSLOPE=15, DECIM=127, FILTDRIVE=0, FINE=64),
    "CYM": dict(WAVE=4, TUNE=127, FM=0, FMFREQ=0, CLICK=0, PAMT=0, PDEC=0, PAN=63, FTYPE=1, CUT=68, RES=0, DRIVE=63,
                LEVEL=127, ATK=0, DEC=107, DECOPEN=0, MODWAVE=0, TRANSWAVE=0, TRANSFREQ=0, MIXOSC=0, SLOPE=0, PSLOPE=0,
                DECIM=127, FILTDRIVE=0, FINE=63),
    "HAT": dict(WAVE=0, TUNE=50, FM=127, FMFREQ=56, CLICK=0, PAMT=127, PDEC=127, PAN=63, FTYPE=1, CUT=104, RES=0,
                DRIVE=63, LEVEL=127, ATK=0, DEC=5, DECOPEN=17, MODWAVE=0, TRANSWAVE=0, TRANSFREQ=0, MIXOSC=0, SLOPE=6,
                PSLOPE=0, DECIM=127, FILTDRIVE=0, FINE=63),
}
# the FX send of each voice (0..127), the kit's own
SENDS = dict(DRUM1=0, DRUM2=20, DRUM3=10, SNARE=24, CYM=30, HAT=10)

# waves: 0 SINE 1 TRI 2 SAW 3 REC 4 NOISE 5 PWM; filter: 0 LP 1 HP 2 BP 3 UNITY BP 4 NOTCH 5 PEAK 6 LP 2-POLE
# (the snare's CLICK is its REPEAT: 0 a snare, 2..5 a clap; the cymbal's too)
K = []


def kit(name, style, send=None, **voices):
    K.append((name, style, voices, send or {}))


# ---------------------------------------------------------------- TECHNO
kit("TECHNO KIT", "TECHNO",
    DRUM1=dict(TUNE=30, PAMT=52, PDEC=30, DEC=34, DRIVE=92, CLICK=40, TRANSWAVE=3),
    SNARE=dict(TUNE=50, FM=84, DEC=18, CLICK=0, DRIVE=80),
    HAT=dict(DEC=6, DECOPEN=30, CUT=110),
    DRUM2=dict(TUNE=45, PAMT=30, PDEC=40, DEC=22),
    DRUM3=dict(TUNE=70, DEC=4, WAVE=3),
    CYM=dict(DEC=90, CUT=80))
kit("BERLIN HARD", "TECHNO", send=dict(DRUM1=0, SNARE=40, CYM=50),
    DRUM1=dict(TUNE=28, PAMT=64, PDEC=34, DEC=52, DRIVE=118, CLICK=70, TRANSWAVE=4, FILTDRIVE=40),
    SNARE=dict(TUNE=48, FM=100, FMFREQ=110, DEC=24, CLICK=3, ATK=12, DRIVE=100),
    HAT=dict(DEC=5, DECOPEN=40, CUT=116, DRIVE=90),
    DRUM2=dict(TUNE=40, PAMT=40, PDEC=46, DEC=30, DRIVE=96),
    DRUM3=dict(TUNE=74, DEC=4, WAVE=3, DRIVE=96),
    CYM=dict(DEC=110, CUT=74, DRIVE=90))
kit("MINIMAL CLICK", "TECHNO",
    DRUM1=dict(TUNE=33, PAMT=40, PDEC=18, DEC=22, DRIVE=70, CLICK=90, TRANSWAVE=0),
    SNARE=dict(TUNE=60, FM=50, FMFREQ=127, DEC=8, CLICK=0),
    HAT=dict(DEC=3, DECOPEN=14, CUT=120),
    DRUM2=dict(TUNE=52, PAMT=24, PDEC=30, DEC=12),
    DRUM3=dict(TUNE=80, DEC=2, WAVE=1),
    CYM=dict(DEC=60, CUT=100))
kit("ACID BOX", "TECHNO",
    DRUM1=dict(TUNE=31, PAMT=58, PDEC=28, DEC=40, DRIVE=100, CLICK=30, TRANSWAVE=2),
    SNARE=dict(TUNE=52, FM=90, DEC=16, DRIVE=90, FTYPE=2, CUT=100, RES=50),
    HAT=dict(DEC=5, DECOPEN=26, CUT=112, RES=30),
    DRUM2=dict(TUNE=46, PAMT=36, PDEC=38, DEC=20, RES=40, CUT=90),
    DRUM3=dict(TUNE=76, DEC=3, WAVE=2),
    CYM=dict(DEC=80, CUT=90))
kit("INDUSTRIAL", "TECHNO", send=dict(SNARE=50, DRUM2=40, CYM=60),
    DRUM1=dict(TUNE=27, PAMT=70, PDEC=36, DEC=46, DRIVE=124, CLICK=80, TRANSWAVE=6, DECIM=90),
    SNARE=dict(TUNE=46, FM=110, FMFREQ=90, DEC=26, CLICK=2, ATK=10, DRIVE=120, DECIM=80),
    HAT=dict(DEC=7, DECOPEN=36, CUT=100, DRIVE=110, DECIM=96),
    DRUM2=dict(TUNE=36, PAMT=60, PDEC=50, DEC=34, DRIVE=120, WAVE=3),
    DRUM3=dict(TUNE=66, DEC=5, WAVE=4, DRIVE=110),
    CYM=dict(DEC=116, CUT=60, DRIVE=110, DECIM=70))
# ---------------------------------------------------------------- HOUSE
kit("DEEP HOUSE", "HOUSE",
    DRUM1=dict(TUNE=32, PAMT=40, PDEC=26, DEC=30, DRIVE=70, CLICK=20, TRANSWAVE=2),
    SNARE=dict(TUNE=52, FM=70, DEC=20, CLICK=4, ATK=14),
    HAT=dict(DEC=5, DECOPEN=34, CUT=106),
    DRUM2=dict(TUNE=48, PAMT=26, PDEC=40, DEC=24),
    DRUM3=dict(TUNE=72, DEC=3, WAVE=1),
    CYM=dict(DEC=96, CUT=84))
kit("GARAGE 2-STEP", "HOUSE", send=dict(SNARE=36),
    DRUM1=dict(TUNE=33, PAMT=46, PDEC=22, DEC=26, DRIVE=76, CLICK=50, TRANSWAVE=3),
    SNARE=dict(TUNE=56, FM=76, FMFREQ=120, DEC=14, CLICK=0),
    HAT=dict(DEC=4, DECOPEN=22, CUT=114),
    DRUM2=dict(TUNE=50, PAMT=30, PDEC=34, DEC=16),
    DRUM3=dict(TUNE=78, DEC=2, WAVE=1),
    CYM=dict(DEC=70, CUT=96))
kit("DISCO HOUSE", "HOUSE", send=dict(SNARE=30, DRUM2=30),
    DRUM1=dict(TUNE=32, PAMT=36, PDEC=24, DEC=32, DRIVE=74),
    SNARE=dict(TUNE=50, FM=80, DEC=22, CLICK=4, ATK=16),
    HAT=dict(DEC=6, DECOPEN=46, CUT=104),
    DRUM2=dict(TUNE=44, PAMT=40, PDEC=46, DEC=30),
    DRUM3=dict(TUNE=70, DEC=4, WAVE=1),
    CYM=dict(DEC=104, CUT=78))
kit("AFRO HOUSE", "HOUSE", send=dict(DRUM2=34, DRUM3=30),
    DRUM1=dict(TUNE=31, PAMT=44, PDEC=28, DEC=36, DRIVE=74, CLICK=20),
    SNARE=dict(TUNE=58, FM=60, DEC=12, CLICK=3, ATK=10),
    HAT=dict(DEC=4, DECOPEN=20, CUT=112),
    DRUM2=dict(TUNE=52, PAMT=30, PDEC=36, DEC=26, WAVE=1),
    DRUM3=dict(TUNE=67, PAMT=60, PDEC=14, DEC=10, WAVE=1),
    CYM=dict(DEC=64, CUT=92))
# ---------------------------------------------------------------- ELECTRO
kit("808 ELECTRO", "ELECTRO",
    DRUM1=dict(TUNE=29, PAMT=30, PDEC=30, DEC=70, DRIVE=80, CLICK=20),
    SNARE=dict(TUNE=54, FM=70, DEC=14, CLICK=0),
    HAT=dict(DEC=4, DECOPEN=30, CUT=108),
    DRUM2=dict(TUNE=44, PAMT=26, PDEC=46, DEC=40),
    DRUM3=dict(TUNE=76, DEC=2, WAVE=3),
    CYM=dict(DEC=96, CUT=86))
kit("ELECTRO FUNK", "ELECTRO", send=dict(SNARE=40),
    DRUM1=dict(TUNE=31, PAMT=40, PDEC=26, DEC=44, DRIVE=86, CLICK=40, TRANSWAVE=2),
    SNARE=dict(TUNE=52, FM=88, DEC=18, CLICK=3, ATK=12, DRIVE=84),
    HAT=dict(DEC=5, DECOPEN=26, CUT=112),
    DRUM2=dict(TUNE=47, PAMT=40, PDEC=40, DEC=26, WAVE=2, CUT=90),
    DRUM3=dict(TUNE=79, DEC=3, WAVE=2),
    CYM=dict(DEC=84, CUT=88))
kit("MIAMI BASS", "ELECTRO",
    DRUM1=dict(TUNE=26, PAMT=34, PDEC=34, DEC=96, DRIVE=100, CLICK=30),
    SNARE=dict(TUNE=56, FM=80, DEC=12, CLICK=4, ATK=8),
    HAT=dict(DEC=3, DECOPEN=18, CUT=116),
    DRUM2=dict(TUNE=40, PAMT=34, PDEC=50, DEC=44),
    DRUM3=dict(TUNE=82, DEC=2, WAVE=3),
    CYM=dict(DEC=70, CUT=96))
# ---------------------------------------------------------------- HIP-HOP
kit("BOOM BAP", "HIP-HOP", send=dict(SNARE=30),
    DRUM1=dict(TUNE=31, PAMT=40, PDEC=24, DEC=34, DRIVE=96, CLICK=50, TRANSWAVE=5, DECIM=110),
    SNARE=dict(TUNE=50, FM=96, FMFREQ=100, DEC=20, DRIVE=96, DECIM=110),
    HAT=dict(DEC=5, DECOPEN=24, CUT=100, DECIM=110),
    DRUM2=dict(TUNE=46, PAMT=24, PDEC=38, DEC=22, DECIM=110),
    DRUM3=dict(TUNE=68, DEC=4, WAVE=1, DECIM=110),
    CYM=dict(DEC=76, CUT=80, DECIM=110))
kit("TRAP 808", "HIP-HOP",
    DRUM1=dict(TUNE=26, PAMT=24, PDEC=40, DEC=110, DRIVE=104, CLICK=40, TRANSWAVE=3),
    SNARE=dict(TUNE=56, FM=86, FMFREQ=120, DEC=16, CLICK=3, ATK=8),
    HAT=dict(DEC=3, DECOPEN=20, CUT=118),
    DRUM2=dict(TUNE=42, PAMT=30, PDEC=46, DEC=34),
    DRUM3=dict(TUNE=84, DEC=2, WAVE=3),
    CYM=dict(DEC=80, CUT=100))
kit("DRILL", "HIP-HOP", send=dict(SNARE=34),
    DRUM1=dict(TUNE=27, PAMT=30, PDEC=44, DEC=104, DRIVE=110, CLICK=50, TRANSWAVE=4),
    SNARE=dict(TUNE=60, FM=94, FMFREQ=124, DEC=14, DRIVE=90),
    HAT=dict(DEC=2, DECOPEN=16, CUT=122),
    DRUM2=dict(TUNE=43, PAMT=36, PDEC=44, DEC=28),
    DRUM3=dict(TUNE=86, DEC=2, WAVE=1),
    CYM=dict(DEC=74, CUT=104))
# ---------------------------------------------------------------- LO-FI
kit("LO-FI DUST", "LO-FI", send=dict(SNARE=40, DRUM2=30),
    DRUM1=dict(TUNE=32, PAMT=34, PDEC=22, DEC=30, DRIVE=84, CUT=80, DECIM=80),
    SNARE=dict(TUNE=50, FM=74, FMFREQ=80, DEC=18, CUT=86, DECIM=76),
    HAT=dict(DEC=5, DECOPEN=22, CUT=96, DECIM=80),
    DRUM2=dict(TUNE=47, PAMT=20, PDEC=36, DEC=22, CUT=80, DECIM=80),
    DRUM3=dict(TUNE=70, DEC=3, WAVE=1, DECIM=80),
    CYM=dict(DEC=70, CUT=76, DECIM=70))
kit("TAPE BEAT", "LO-FI", send=dict(SNARE=36),
    DRUM1=dict(TUNE=31, PAMT=38, PDEC=24, DEC=32, DRIVE=100, CUT=90, FILTDRIVE=50),
    SNARE=dict(TUNE=48, FM=80, FMFREQ=90, DEC=20, DRIVE=96, CUT=96),
    HAT=dict(DEC=6, DECOPEN=26, CUT=98, DRIVE=90),
    DRUM2=dict(TUNE=45, PAMT=22, PDEC=36, DEC=24, CUT=90),
    DRUM3=dict(TUNE=66, DEC=4, WAVE=1),
    CYM=dict(DEC=74, CUT=74))
# ---------------------------------------------------------------- DRUM & BASS
kit("JUNGLE", "DNB", send=dict(SNARE=44, DRUM2=30),
    DRUM1=dict(TUNE=33, PAMT=44, PDEC=22, DEC=28, DRIVE=96, CLICK=50, TRANSWAVE=5),
    SNARE=dict(TUNE=58, FM=90, FMFREQ=116, DEC=20, DRIVE=96, RES=30, FTYPE=2, CUT=110),
    HAT=dict(DEC=4, DECOPEN=24, CUT=110),
    DRUM2=dict(TUNE=50, PAMT=34, PDEC=36, DEC=22),
    DRUM3=dict(TUNE=78, DEC=3, WAVE=3),
    CYM=dict(DEC=96, CUT=84))
kit("LIQUID DNB", "DNB", send=dict(SNARE=50, CYM=50),
    DRUM1=dict(TUNE=32, PAMT=40, PDEC=20, DEC=26, DRIVE=80, CLICK=40),
    SNARE=dict(TUNE=60, FM=80, FMFREQ=124, DEC=18, CLICK=0),
    HAT=dict(DEC=3, DECOPEN=20, CUT=118),
    DRUM2=dict(TUNE=52, PAMT=26, PDEC=32, DEC=18),
    DRUM3=dict(TUNE=84, DEC=2, WAVE=1),
    CYM=dict(DEC=100, CUT=96))
kit("NEURO", "DNB", send=dict(SNARE=40),
    DRUM1=dict(TUNE=30, PAMT=60, PDEC=26, DEC=30, DRIVE=120, CLICK=70, TRANSWAVE=6),
    SNARE=dict(TUNE=56, FM=110, FMFREQ=110, DEC=18, DRIVE=116, FTYPE=2, CUT=106, RES=60),
    HAT=dict(DEC=3, DECOPEN=18, CUT=120, DRIVE=100),
    DRUM2=dict(TUNE=44, PAMT=50, PDEC=40, DEC=22, DRIVE=110, WAVE=2),
    DRUM3=dict(TUNE=80, DEC=3, WAVE=2, DRIVE=110),
    CYM=dict(DEC=86, CUT=90, DRIVE=100))
# ---------------------------------------------------------------- DUB
kit("DUB STEPPERS", "DUB", send=dict(SNARE=70, DRUM3=60, CYM=50),
    DRUM1=dict(TUNE=29, PAMT=34, PDEC=30, DEC=46, DRIVE=84, CUT=100),
    SNARE=dict(TUNE=46, FM=60, FMFREQ=80, DEC=22, CLICK=0, CUT=104),
    HAT=dict(DEC=5, DECOPEN=30, CUT=104),
    DRUM2=dict(TUNE=43, PAMT=30, PDEC=46, DEC=34),
    DRUM3=dict(TUNE=72, DEC=4, WAVE=1),
    CYM=dict(DEC=90, CUT=80))
kit("DUBSTEP", "DUB", send=dict(SNARE=50),
    DRUM1=dict(TUNE=28, PAMT=50, PDEC=30, DEC=54, DRIVE=110, CLICK=60, TRANSWAVE=4),
    SNARE=dict(TUNE=50, FM=104, FMFREQ=100, DEC=30, CLICK=3, ATK=14, DRIVE=110),
    HAT=dict(DEC=4, DECOPEN=26, CUT=110),
    DRUM2=dict(TUNE=40, PAMT=44, PDEC=48, DEC=34, DRIVE=100),
    DRUM3=dict(TUNE=74, DEC=3, WAVE=3),
    CYM=dict(DEC=100, CUT=82))
# ---------------------------------------------------------------- MORE
kit("BIG BEAT", "BREAKS", send=dict(SNARE=40),
    DRUM1=dict(TUNE=31, PAMT=50, PDEC=26, DEC=40, DRIVE=120, CLICK=60, TRANSWAVE=5, FILTDRIVE=60),
    SNARE=dict(TUNE=52, FM=100, FMFREQ=104, DEC=24, DRIVE=118),
    HAT=dict(DEC=6, DECOPEN=30, CUT=104, DRIVE=100),
    DRUM2=dict(TUNE=44, PAMT=40, PDEC=40, DEC=28, DRIVE=110),
    DRUM3=dict(TUNE=72, DEC=4, WAVE=3, DRIVE=110),
    CYM=dict(DEC=100, CUT=78, DRIVE=100))
kit("IDM GLITCH", "IDM", send=dict(SNARE=50, DRUM3=60, CYM=40),
    DRUM1=dict(TUNE=34, PAMT=70, PDEC=12, DEC=18, DRIVE=90, CLICK=90, TRANSWAVE=8, DECIM=70),
    SNARE=dict(TUNE=64, FM=90, FMFREQ=60, DEC=10, CLICK=5, ATK=4, DECIM=60),
    HAT=dict(DEC=2, DECOPEN=12, CUT=124, DECIM=50),
    DRUM2=dict(TUNE=58, PAMT=80, PDEC=16, DEC=12, WAVE=3, FM=60, FMFREQ=70),
    DRUM3=dict(TUNE=90, DEC=2, WAVE=4, DECIM=40),
    CYM=dict(DEC=50, CUT=100, DECIM=90))
kit("SYNTH POP", "POP", send=dict(SNARE=60, DRUM2=40, CYM=40),
    DRUM1=dict(TUNE=32, PAMT=42, PDEC=24, DEC=32, DRIVE=80, CLICK=30),
    SNARE=dict(TUNE=50, FM=84, FMFREQ=110, DEC=26, CLICK=0),
    HAT=dict(DEC=5, DECOPEN=30, CUT=108),
    DRUM2=dict(TUNE=46, PAMT=40, PDEC=44, DEC=32, WAVE=1),
    DRUM3=dict(TUNE=72, DEC=4, WAVE=1),
    CYM=dict(DEC=100, CUT=84))


def build(core_path):
    lib = ctypes.CDLL(os.path.abspath(core_path))

    class Api(ctypes.Structure):
        _fields_ = [("api", ctypes.c_uint32), ("sample_rate", ctypes.c_void_p), ("init", ctypes.CFUNCTYPE(None, ctypes.c_uint32)),
                    ("set", ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32)),
                    ("trigger", ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32)),
                    ("render", ctypes.CFUNCTYPE(None, ctypes.POINTER(ctypes.c_float))),
                    ("pan", ctypes.c_void_p), ("hat_open", ctypes.c_void_p), ("fx_process", ctypes.c_void_p),
                    ("fx_set", ctypes.c_void_p), ("fx_delay_samples", ctypes.c_void_p),
                    ("load_init", ctypes.CFUNCTYPE(None)),
                    ("set_slot", ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32)),
                    ("has_slot", ctypes.c_void_p)]

    lib.jks_get.restype = ctypes.POINTER(Api)
    a = lib.jks_get().contents
    buf = (ctypes.c_float * (6 * 64))()

    def peak(vals, v, open_=0):
        a.init(1)
        a.load_init()
        for s, x in enumerate(vals):
            a.set_slot(v, s, x)
        a.trigger(v, 127, open_)
        p = 0.0
        for _ in range(44100 // 64):
            a.render(buf)
            p = max(p, max(abs(buf[v * 64 + i]) for i in range(64)))
        return p

    out = []
    for name, style, voices, send in K:
        vals, gains = [], []
        for v, vn in enumerate(VOICES):
            d = dict(BASE[vn])
            d.update(voices.get(vn, {}))
            row = [d[n] for n in SLOTS]
            # loudest: LEVEL so that the peak reaches TARGET (at most 127)
            row[S["LEVEL"]] = 127
            p = peak(row, v)
            if p > 0:
                row[S["LEVEL"]] = max(20, min(127, int(127 * TARGET / p)))
            pk = peak(row, v)
            # what LEVEL cannot reach any more: a gain on the voice's output (up to x4), the kit's own
            g = min(4.0, TARGET / pk) if pk > 0 else 1.0
            gains.append(round(max(1.0, g), 2))
            vals.append(row)
            print(f"{name:14s} {vn:6s} peak {pk:.2f} level {row[S['LEVEL']]} gain {gains[-1]}")
            if pk < 0.05:
                sys.exit(f"{name}: {vn} is silent")
        sends = [send.get(vn, SENDS[vn]) for vn in VOICES]
        out.append((name, style, vals, sends, gains))
    return out


def write(kits):
    L = ["// Generated by tools/kessel_kits.py - do not edit", "// KESSEL's factory kits: per voice the slots of kesselcore.h (JKS_S_*), 0..127, and the voice's FX send.",
         "#pragma once", "#include <cstdint>", "namespace kessel_kits {", f"static const int NUM = {len(kits)};",
         f"static const int NSLOT = {len(SLOTS)};", "struct Kit {", "\tconst char* name;", "\tconst char* style;",
         "\tuint8_t v[6][NSLOT];", "\tuint8_t send[6];", "\tfloat gain[6];  // on the voice's output, for what LEVEL 127 does not reach", "};", "static const Kit KITS[NUM] = {"]
    for name, style, vals, sends, gains in kits:
        rows = ", ".join("{" + ", ".join(str(x) for x in r) + "}" for r in vals)
        gs = ", ".join(f"{g:.2f}f" for g in gains)
        L.append(f'\t{{"{name}", "{style}", {{{rows}}}, {{{", ".join(str(s) for s in sends)}}}, {{{gs}}}}},')
    L += ["};", "}  // namespace kessel_kits"]
    open(os.path.join(ROOT, "src", "kessel", "KesselKits.hpp"), "w").write("\n".join(L) + "\n")
    print("wrote src/kessel/KesselKits.hpp:", len(kits), "kits")


if __name__ == "__main__":
    write(build(sys.argv[1]))
