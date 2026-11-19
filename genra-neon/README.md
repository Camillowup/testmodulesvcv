# GENRA NEON

The neon modules of the GENRA family for VCV Rack (brand **Genra**), in their own look: **NEON-TERRAIN** – dark violet
glass, pink to cyan neon, a glitch title, knobs with glowing value arcs, hexagon jacks in coloured groups. All labels and
menus are in English; the module names stay German.

| Module | What it is |
| --- | --- |
| **WELLE** (44 HP) | Wavetable synth. The table is a neon mountain range on the display; the frame that plays is its glowing ridge. |
| **NEBEL** (44 HP) | Cluster universe. Every note becomes a cloud of up to 8 voices; the display is a galaxy, the voices its stars. |

Both follow **GENRA** and **GENRA TECHNO** without cables (right-click › Follow GENRA; presets change with the genre),
take MIDI-MAP on every knob, button, field and the preset field, and have many CV inputs (no built-in LFO: patch one).

## WELLE

- **Two oscillators.** The FOLDER/TABLE field picks a folder and a table; **TABLE** turns through the tables of the
  folder (it stops at the end), **POSITION** through the frames of the table, **WARP** bends the phase or the wave
  (BEND, SYNC, PWM, MIRROR, FOLD, CRUSH). OSC A has OCT, OSC B TUNE.
- **32 built-in tables** in five folders: BASIC, DIGITAL, VOCAL, TECHNO (ACID, STAB, DUB CHORD, REESE, HOOVER, RAVE
  ORGAN, METALLIC, INDUSTRIAL), TEXTURE. Each has 64 frames and band-limited copies, so high notes do not alias.
- **Your own wavetables:** SET ROOT FOLDER … once (a Windows folder window), then every folder below it with WAV files
  shows up in the field (sub folders too). LOAD WAV … picks a single file. Common wavetable WAVs work (frames of 2048
  samples one after the other, 16/24/32-bit or float; a `clm ` chunk with another frame size is read).
- **FILTER:** GENRA's eight filter models, CUTOFF, RES, ENV, DRIVE, KEY, F.ATK, F.DEC.
- **MOTION / FX:** SCAN moves the position in time (RATE: 8 bars … 1/32, with CLOCK or GENRA), P.ENV with the filter
  envelope; CHORUS, ping-pong DELAY (time in the context menu), REVERB.
- **MIX:** A / B, SUB, UNISON (1–7), SPREAD. **VOICE:** POLY / MONO / LEGATO, GLIDE (on GENRA's SLIDE), ACCENT.
- **MUTATE** makes new tables for A and B from the building blocks, **FREEZE** holds the frame, **UNDO** takes back the
  last MUTATE / INIT, **INIT** goes back to a plain sound. MUTATE and FREEZE have trigger inputs.
- Outputs: ENV, MOTION (±5 V), L, R.

## NEBEL

- **GENERATOR:** WAVETABLE, ANALOG, ADDITIVE, MODAL, STRING – TIMBRE, MORPH, OCT.
- **CLUSTER:** the chord (MINOR 9 … WHOLE TONE, CHAOS), SPREAD over the octaves, VOICES (1–8), DETUNE, CHAOS, DRIFT,
  STRUM (the voices one after the other), ACCENT.
- **CHORD PANEL / GENRA:** with GENRA the notes that come in (GENRA's chord on the pad track) make the cluster.
- **TEXTURE:** SHIMMER, GRAIN, DUST, SMEAR, FOLD – AMOUNT, COLOR, DENSITY, WIDTH.
- **SPACE:** three reverbs, NEBULA (shimmer cloud), HALL, VOID (endless dark) – SIZE, DECAY, MIX, TONE.
- **ORBIT:** RATE and DEPTH turn the voices in the stereo field in time; MACRO gives more of everything; GLIDE.
- **BIG BANG** throws a new cluster, **FREEZE** holds it, UNDO, INIT. IN L/R send other sounds through texture and space.
- Outputs: ENV, ORBIT (±5 V), L, R.

## Presets

WELLE has 32 factory presets, NEBEL 24, sorted by style (the styles match GENRA's genres). Every preset is levelled by
the engine test (`test/neon_test.cpp`): it plays each one and sets its gain, so they are even – not too hot, not too
quiet. Own presets: right-click › Save preset as …

## Build

```
make genra-src                     # GENRA's shared parts (knobs, Follow GENRA, presets, link hub)
make RACK_DIR=/path/to/Rack-SDK dist
g++ -O2 -std=c++11 -I. test/neon_test.cpp -o neon_test && ./neon_test   # the engine test, no Rack needed
```

Panels: `tools/neon_panel.py real` writes `res/*.svg` and `src/*/…Layout.hpp`; `preview` writes `docs/panels/`.
GitHub Actions builds Windows on every push (Mac by hand: Actions › Run workflow). License: GPL-3.0-only.
