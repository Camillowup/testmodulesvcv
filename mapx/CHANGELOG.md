# GENRA MAP-X – Versionsgeschichte

### 2.3.0 (25. Nov. 2026, Test-Build, kein Release)
- Fertige Belegung für das **Novation Launchpad X** auf GENRA TECHNO (MAP: „MIDI MIX+LAUNCHPAD X“ oder „LAUNCHPAD X“,
  neu hinten angehängt). Custom 1 auf Kanal 1 = Spuren (PLAY … NEXT, NEW/MUTE/LOCK pro Spur in GENRA TECHNOs
  Reihenfolge DRUMS, BASS, STAB, LEAD, ATMO, LOCK ALL, BUILD/DROP/BREAK/FILL); Custom 2 auf Kanal 2 = SCHLEIFE wie MPD218
  Bank B plus **GENRA TECHNO ABLAUF** (Bearbeiten, Varianten, MAKE SONG, TAKE LIVE, Bänke, PATT/SONG/LOOP/DJ);
  Custom 3 (Kanal 3, Fader) frei für LEARN. Beim Launchpad zählt der Kanal mit (Seite 1 und 2 haben dieselben Noten).
- Ein neues MAP-X nimmt ohne MPD218 das Launchpad (seinen MIDI-Port, nicht DAW) als IN 2.
- Display: Launchpad-Pads als „P1 R3·2“ (Seite, Reihe von unten, Spalte).

### 2.2.0 (25. Nov. 2026, Test-Build, kein Release)
- Fertige Belegung auch für **GENRA TECHNO**: MIDImix Kanal 1–5 = DRUMS, BASS, LEAD, ATMO, STAB (TYPE, Spur-Regler,
  PRESET des folgenden Instruments, DENSITY, MUTE, NEW, LOCK), Kanal 6–8 nur KEY, SCALE, BUILD, LENGTH und sechs Tasten
  (Rest frei); MPD218 Bank A Transport, NEW/MUTE pro Spur, NEW ALL, PATTERN/LIVE; Bank B SCHLEIFE (RUN/STOP =
  PLAY/STOP); Bank C BUILD/DROP/BREAK/FILL, LOCK pro Spur, PREV/NEXT. Tasten von GENRA TECHNO werden gehalten (HOLD).
- Automatische Wahl GENRA / GENRA TECHNO nach dem Rack, per Rechtsklick fest einstellbar.

### 2.1.0 (25. Nov. 2026, Test-Build, kein Release)
- Fertige Belegung: MPD218 Bank B (Noten 52–67) spielt den Looper **SCHLEIFE** aus GENRA Test: pro Spur REC/DUB, UNDO,
  CLEAR (eine Spalte pro Spur), dazu CLR ALL, GENRA RUN und STOP, ein Pad frei. SCHLEIFE wird im Rack von selbst
  gefunden; ohne SCHLEIFE steht die Zeile mit „·“ in der Liste. LEVEL bleibt frei für LEARN.

### 2.0.0 (24. Nov. 2026, Test-Build, kein Release)
- Erste Version: MIDI-Mapper mit zwei Eingängen (IN 1, IN 2); versteht Noten, CC, Program Change und SysEx.
- Fertige Belegung für Akai MIDImix und MPD218 auf das GENRA-Hauptmodul (Feld MAP: OFF, beide, nur MIDI MIX, nur MPD 218).
- Eigene Belegungen mit LEARN, CLEAR, UNDO; Tasten-Logik ONCE, ON/OFF, HOLD, STEP (CC sonst direkt).
