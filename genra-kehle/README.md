# GENRA Kehle

**KEHLE** is a neon vocoder for VCV Rack, in the NEON-TERRAIN look of the GENRA family. For now it is a test plugin
of its own; once it works it moves into GENRA Neon Punk.

## Patching
- **MOD**: the voice. Your microphone comes in through Rack's *Audio* module (choose your audio interface, take the
  input your mic is on) and goes from there into MOD. CANTA or LIED work too.
- **CAR L / CAR R**: the carrier, a synth that will speak (SCHMELZ, WELLE …). CAR R unplugged: the left one.
- Nothing in CAR: the **built-in carrier** (CARRIER field: SAW CHORD, PULSE, STRINGS, NOISE) plays the notes on
  **V/OCT + GATE** (polyphonic: chords). Follow GENRA (right-click) plays GENRA's pads. Nothing patched at all: a
  C minor chord, so it speaks right away.

## Controls
- **CHARACTER**: CLASSIC (robot), WHISPER (noise carrier, a ghost), CHOIR (detuned voices with vibrato and a wide
  chorus), METAL (ring-modulated bands, tin can), GLASS (very narrow singing bands, crystal), SHIFT (FORMANT over
  ±2 octaves: monster to mouse), CRUSH (a speech chip: 45 frames per second, 8 kHz, 6 bits), TALKBOX (fewer, softer
  bands, a driven carrier).
- **BANDS** 8–32 · **FORMANT** moves the carrier's bands (±1 octave, SHIFT ±2) · **RESO** narrower bands ·
  **SIBIL** the hiss of S, T, F comes through as noise · **OCT** and **ENSEMBLE** for the carrier.
- **ATTACK / RELEASE** of the band envelopes · **DRY** the voice itself in the output · **LEVEL** (not in a preset).
- **FREEZE** the bands stand still: the vowel you are singing stays · **HOLD** the built-in carrier keeps the last
  chord after the gates close · **UNDO / INIT**.
- CV under the knobs: FORMANT, RESO, SIBIL, ENSEMBLE, RELEASE, DRY; a trigger input toggles FREEZE.
- Outputs: **ENV** (how loud the voice is, 0–10 V), **L / R**.
- The display: the bands as a neon landscape, and a figure for each character that sings along with your voice.
- 16 factory presets (ROBOT, CHOIR, WHISPER, FX), your own presets, MIDI-MAP on every control.

## Build
`make genra-src && make RACK_DIR=/path/to/Rack-SDK dist` (GitHub Actions: workflow *Build GENRA Kehle*).
Engine test without Rack: `g++ -O2 -std=c++11 -I. test/kehle_test.cpp -o kehle_test && ./kehle_test check`.
Panel: `python3 tools/kehle_panel.py real` (preview: `preview`).
