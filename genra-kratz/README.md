# GENRA Kratz VCV

**KRATZ** is a scratch module for VCV Rack with one record, in the look of GENRA TECHNO. For now it is a test plugin of
its own; once it works it moves into GENRA TECHNO.

## The record
- **LOAD** a WAV (the first time: the sample root folder, the same as SCHLAG's; then the window to pick a file) or
  **drop a WAV** on the module. Or record **IN L / IN R** with **REC** (press again to stop, up to 30 s; it is kept with
  the patch).
- When a sample loads KRATZ finds its **key** and its **tempo** (a short sound has no tempo: ONE SHOT). The mouse wheel
  on the tempo in the display corrects it (Shift: in tenths).

## Scratching
- **The platter**: grab it with the mouse and turn it.
- **LVL** and **SCR**, two faders made for two MIDI faders: LVL is the fader (sharp: open after a few percent; smooth
  in the right-click menu), SCR is where the record is – the whole travel covers **RANGE**. Learn them in the
  right-click menu (or map them with MIDI-MAP). A jog wheel (relative CC) can be learned too.
- **Patterns** (PAT, in time with **RATE**): BABY, FORWARD, BACKWARD, DRAG, SCRIBBLE, UZI, TEAR, CLOVER TEAR, SWIPE,
  SPINBACK · CHIRP, TRANSFORMER, 1-/2-/3-CLICK FLARE, ORBIT, CRAB, TWIDDLE · MILITARY, AQUAMAN, BOOMERANG · RANDOM (one
  per cycle), COMBO (a phrase). A gate on **PAT** plays the pattern while it is high (unplugged: always). Your hand
  always wins over a pattern.
- **HOLD**: the record stands still while the hand rests on it (off: it runs on after a moment) · **SLIP**: when the
  hand lets go, the record is where it would have been · **LOOP** (length: right-click on LOOP) starts at the last
  cue · **REV** · **CUT** silences while held.
- **Cues 1–8**: empty: a click sets it, set: a click jumps (right-click: clear). A polyphonic trigger on **CUE**: channel
  n jumps to cue n.
- **MOTION** records your hand for one bar and loops it in time (press again: off).
- **SPEED** (the motor), **PITCH**, **DRAG** (how long the motor takes to start and stop), **TONE** (low pass left,
  high pass right).

## Time and key
- **SYNC**: GENRA (the tempo of the GENRA it follows – right-click › Follow GENRA – or of CLK), SAMPLE (the sample's
  own tempo), FREE. It starts on SAMPLE and turns to GENRA by itself when Follow GENRA or a clock comes in; without a
  clock it falls back to the sample's tempo.
- **VIN | STR**: matching GENRA's tempo changes the pitch like a record (VIN) or keeps it (STR).
- **TUNE**: OFF, KEY (to GENRA's key: its KEY knob, with GENRA CHOP / TECHNO the notes of its bass), BASS (follows
  GENRA's bass line). A note on **TUNE** sets the key instead.

## MIDI
Right-click › MIDI input: the device. Notes C1–G1: cues 1–8 · G#1: CUT · from C2: the patterns (they play while held).

## Build
    make genra-src
    make RACK_DIR=/path/to/Rack-SDK dist
