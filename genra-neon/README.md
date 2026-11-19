# GENRA NEON

The neon modules of the GENRA family for VCV Rack (brand **Genra**), in their own look: **NEON-TERRAIN** – dark violet
glass, pink to cyan neon, a glitch title, knobs with glowing value arcs, hexagon jacks in coloured groups. All labels and
menus are in English; the module names stay German.

| Module | What it is |
| --- | --- |
| **WELLE** (44 HP) | Wavetable synth. The table is a neon mountain range on the display; the frame that plays is its glowing ridge. |
| **NEBEL** (44 HP) | Cluster universe. Every note becomes a cloud of up to 8 voices; the display is a galaxy, the voices its stars. |
| **WOGE** (44 HP) | Scanned synthesis. A string of masses moves slowly; a scan head reads its shape as the wave. |
| **PULS** (44 HP) | Pulsar synthesis. A train of pulses with two formants: a rhythm when slow, a tone when fast. |
| **GLAS** (44 HP) | ANS synthesizer. Paint on the glass with the mouse; a head plays the picture in time. |
| **FLUT** (44 HP) | Tidal drone. Up to six voices on GENRA's chord, breathing with a slow tide. |

All follow **GENRA** and **GENRA TECHNO** without cables (right-click › Follow GENRA; presets change with the genre),
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

## WOGE

- **STRING:** MASS, TENSION, DAMP, CENTER – 24 masses on springs, moving about 1500 times a second; every voice has
  its own string.
- **EXCITE:** HAMMER, PLUCK, NOISE, BOW (as long as the gate is held) or AUDIO (EXC IN pushes the string); POSITION and
  FORCE. **STRIKE** hits every sounding string.
- **SCAN:** PATH (LINEAR, LOOP, BOUNCE, SPIRAL), WIDTH (how much of the string is read), DRIFT (that part wanders, in time).
- **FILTER** (ladder, CUTOFF, RES, DRIVE), envelope, POLY 4 / MONO / LEGATO, GLIDE, ACCENT. **FREEZE** holds the strings.
- Outputs: ENV, SHAPE (the middle of the string, ±5 V), L, R.

## PULS

- **PULSE:** CLOCK (note values 1/1 … 1/64, in time), FREE (0.25 Hz … 2 kHz: a rhythm turns into a tone) or NOTE (the
  pulse rate follows V/OCT: an instrument; the mask then cuts the tone in 16ths). F1 and F2: the two formants (in
  CLOCK / FREE V/OCT moves them).
- **PULSARET:** the wave (SINE … NOISE), the window (GAUSS, EXPO, REV EXPO, HANN, RECT), DUTY, SKEW.
- **MASK:** 16 steps; EUCLID, RANDOM, BURST, ALL or EDIT (click the steps), DENS, ROT, PROB, **NEW**.
- **SPACE:** SPREAD (pulses left and right), JITTER (time and formant), TAIL. Filter, envelope, voice mode.
- Outputs: ENV, PULSE (a trigger for every pulse that plays), L, R.

## GLAS

- **The glass:** paint with the mouse – pitch goes up, time across. Left button: the tool (PEN, SPRAY, LINE, ERASE),
  right button: erase. SIZE, **SNAP** (strokes land on the notes of SCALE). The picture is saved with the patch.
- **BANK:** pictures to start from (CHOIR, DRONE, BELLS, NOISE, GLISS, PULSE, AURORA); **NEW** paints a new one,
  **CLEAR** wipes the glass, **REC** paints the spectrum of IN at the head.
- **PLAY:** 1 … 32 bars in time, FORWARD / BACKWARD / PING-PONG, SPEED, POS (with POS CV patched the CV moves the head).
- **TONE:** SCALE (FREE, CHROMATIC, MAJOR, MINOR, DORIAN, PENTATONIC, **GENRA KEY**: the key of the notes GENRA plays,
  **GENRA CHORD**: the notes held now), LOW, RANGE, TILT. SCALE works when the picture plays, SNAP when you paint.
- **SPACE:** SPREAD, BLUR, SHIMMER. DRONE (plays all the time) or GATE (while a note is held; V/OCT transposes).
- Outputs: ENV, BRIGHT (how much the head plays, 0–10 V), L, R.

## FLUT

- **VOICES:** 1–6 voices (the clouds) on the chord that comes in (one note makes its own chord), OCT, SPREAD over the
  octaves (NEW: a new voicing), DRIFT, MORPH (glide into the next chord), SUB one or two octaves below.
- **TIDE:** SYNC (1 … 128 bars) or FREE (2 s … 10 min) with TIME; DEPTH (how much the tide changes loudness, brightness
  and density), PHASE, SKEW.
- **TEXTURE:** GLASS, REED, CHOIR, BOWED, DUST; BRIGHT, GRIT, BREATH. **SPACE:** BLOOM, WIDTH, HAZE (IN L/R go in too).
- SWELL, FADE, LEVEL; DRONE or GATE; **FREEZE** holds tide, drift and chord.
- Outputs: TIDE (0–10 V), SUB, POLY (the voices), L, R.

## Presets

WELLE has 32 factory presets, NEBEL 24, WOGE 18, PULS 16, GLAS 12, FLUT 14, sorted by style (the styles match GENRA's genres). Every preset is levelled by
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
