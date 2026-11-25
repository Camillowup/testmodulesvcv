# GENRA MAP-X

MIDI-Mapper für VCV Rack im GENRA-Look (12 HP, ohne Buchsen), ein eigenes Test-Plugin (`GenraMapX`) im Repo von GENRA Test.
Anders als VCV MIDI-MAP versteht MAP-X auch **Noten** (Tasten, Pads), Program Change und SysEx – so lassen sich z. B.
NEW ALL, MUTE oder RUN von GENRA mit einer Taste am Controller drücken.

- **IN 1 / IN 2:** zwei Controller in einem Modul. Ein neues MAP-X sucht MIDImix (IN 1) und MPD218 (IN 2) selbst.
- **MAP (fertige Belegung):** MIDI MIX+MPD 218, nur MIDI MIX, nur MPD 218 oder OFF. Sie findet das GENRA-Hauptmodul im
  Rack von selbst und erwartet die Werkseinstellungen (MIDImix wie geliefert, MPD218 Preset 1).
  - MIDImix Kanal 1–4 (DRUMS, BASS, MELODY, PADS): Knopf oben Genre der Spur, Mitte FILL/MOVE/RANGE/VOICING, unten
    GHOST/OCT, Fader DENSITY, MUTE = Mute (ein/aus), REC ARM = NEW der Spur, SOLO halten + MUTE = LOCK.
  - MIDImix Kanal 5–8: Knöpfe GENRE, KEY, SCALE, VARIANT, LENGTH, BPM (die übrigen sechs frei), Fader COMPLEX, COLOR,
    MUTATE, SWING, MUTE-Reihe NEW ALL, HARMONY NEW, HARMONY LOCK, MAKE SONG, REC-Reihe RUN, STOP, FILL (halten), TAP.
    BANK ◀ ▶ = Genre zurück/weiter. Master-Fader frei.
  - MPD218 (Bank A, von unten): RUN, STOP, FILL, TAP · NEW der vier Spuren · MUTE der vier Spuren · NEW ALL, HARMONY NEW,
    HARMONY LOCK, MAKE SONG. Knöpfe 1–6: GENRE, VARIANT, COMPLEX, COLOR, MUTATE, SWING.
  - MPD218 **Bank B: der Looper SCHLEIFE** (GENRA Test 2.9.0, wird im Rack von selbst gefunden). Eine Spalte pro Spur,
    von unten: REC/DUB, UNDO, CLEAR (Pads 1–12). Oberste Reihe: CLR ALL (zweimal drücken), GENRA RUN, GENRA STOP, frei.
    Bank C frei.
- **GENRA TECHNO (ab 2.2.0):** steht statt GENRA ein GENRA TECHNO im Rack, nimmt MAP-X von selbst dessen Belegung
  (Rechtsklick › „Ready-made map for“: Automatisch, GENRA oder GENRA TECHNO fest).
  - MIDImix Kanal 1–5 = DRUMS, BASS, LEAD, ATMO, STAB: Knopf oben TYPE, Mitte der Regler der Spur (GROOVE, ACID, LOOP,
    MOVE, DUB), unten **PRESET** des Instruments, das der Spur per Follow GENRA folgt (MONO, FUNKE …; ohne Follow
    bleibt er leer), Fader DENSITY, MUTE, REC ARM = NEW, SOLO+MUTE = LOCK.
  - Kanal 6–8: nur was nicht auf dem MPD218 liegt: Knöpfe KEY, SCALE, BUILD, LENGTH; MUTE-Reihe NEW ALL, MUTATE, LOCK
    ALL; REC-Reihe PLAY, STOP, UNDO; BANK ◀ ▶ = PREV / NEXT. Frei: fünf Knöpfe, die Fader 6–8, Master, SOLO+MUTE 6–8.
  - MPD218 Bank A (von unten): PLAY, STOP, UNDO, MUTATE · NEW DRUMS, BASS, LEAD, ATMO · MUTE derselben · NEW ALL,
    PATTERN/LIVE (schaltet um, braucht GENRA TECHNO 2.0.10), frei, MUTE STAB. Knöpfe 1–6: STYLE, ENERGY, EVOLVE, SWING,
    FEEL, BPM.
  - Bank B: SCHLEIFE wie oben, RUN/STOP = PLAY/STOP von GENRA TECHNO.
  - Bank C (von unten): BUILD, DROP, BREAK, FILL (halten = bleiben) · LOCK DRUMS, BASS, LEAD, ATMO · NEW STAB, LOCK STAB,
    LOCK ALL, frei · PREV, NEXT, frei, frei.
  - GENRA TECHNO liest seine Tasten beim Zeichnen (60-mal pro Sekunde): MAP-X hält sie deshalb so lange wie das Pad.
- **LEARN:** LEARN drücken, am Controller etwas bewegen, auf einen Regler/Taster eines beliebigen Moduls klicken (oder
  umgekehrt). Eigene Belegungen (im Display mit *) gehen immer vor der fertigen Belegung und bleiben erhalten, auch wenn
  MAP auf OFF steht.
- **CLEAR:** löscht die gewählte eigene Belegung; bei einer Taste der fertigen Belegung schaltet es sie aus (OFF), noch
  einmal CLEAR bringt sie zurück. **UNDO:** nimmt die letzte Änderung zurück.
- **Tasten-Logik (Lämpchen):** ONCE (kurz drücken), ON/OFF (jeder Druck schaltet um), HOLD (an, solange gehalten), STEP
  (jeder Druck der nächste Wert). Knöpfe und Fader setzen den Wert direkt (kein Lämpchen); ein Lämpchen macht aus einem
  Knopf eine Taste, ein Klick auf das leuchtende schaltet zurück auf direkt.
- **Display:** oben, was gerade ankommt; darunter die Liste (Mausrad: blättern, Klick: wählen). Eine Taste am Controller
  wählt ihre Zeile. „TAKEN“: der Parameter ist schon von einem anderen Mapper (z. B. MIDI-CAT) belegt.

Build: `make genra-src && make RACK_DIR=… dist` (die gemeinsamen Teile kommen aus Genra-for-Vcv, Zweig `dev`).
Panel: `tools/mapx_panel.py real` (Vorschau: `preview docs/panels/mapx.png`).
