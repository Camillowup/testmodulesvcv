# GENRA GLANZ

Musik-Visualisierungen für VCV Rack im GENRA-Look (44 HP), ein eigenes Plugin (`GenraGlanz`) im Repo von GENRA Test.

- **Folgt GENRA** (Rechtsklick › Follow GENRA; auch GENRA CHOP und GENRA TECHNO): jede Spur leuchtet in ihrer Farbe genau
  mit ihren Noten – DRUMS violett, BASS grün, MELODY cyan, PADS rosa. Takt und BPM kommen von GENRA.
- **Audio:** L/R (die Summe: Spektrum, Scope) und DRUMS, BASS, MELODY, PADS (jede Spur ihre Farbe). Ohne GENRA reagiert
  GLANZ nur auf das Audio (die Summe in Tiefen, Mitten, Höhen aufgeteilt).
- **Neun Szenen:** SPECTRUM RING, BARS, SCOPE, TUNNEL, STARFIELD, PLASMA, WATERFALL, PARTICLES, DANCE FLOOR.
  Wahl im Feld oben (auch per MIDI-MAP), mit NEXT, SCENE-CV oder AUTO BARS (alle N Takte eine neue).
- **REACT:** AMOUNT, SMOOTH, DECAY. **LOOK:** SPEED, COLOR (0: Spurfarben, hoch: Farbton dreht), TRAILS, ZOOM, GLOW.
  **TRACKS:** jede Spur im Bild an/aus. **FREEZE:** das Bild steht.

Build: `make genra-src && make RACK_DIR=… dist` (die gemeinsamen Teile kommen aus Genra-for-Vcv, Zweig `dev`).
Panel: `tools/glanz_panel.py real` (Vorschau: `preview docs/panels/glanz.png`).
