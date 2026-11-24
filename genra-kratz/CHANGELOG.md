# GENRA KRATZ VCV – Versionsgeschichte

### 2.1.0 (24. Nov. 2026, Test-Build, kein Release)
- **Kein Kratzen mehr:** Maus und Jogwheel kommen nur in Schritten an (60-mal pro Sekunde). Jeder Schritt wird jetzt
  über die Zeit bis zum nächsten verteilt, die Platte fährt durch statt zu springen und zu stehen. Die SCR-Werte
  (MIDI-Fader, 128 Stufen) gleiten genauso. Die Nadel wird bei fast stehender Platte weich leiser.
- **HOLD hält immer an:** die Hand liegt auf der Platte – auch unter einem Muster, einer Phrase, MOTION oder dem Motor.
  HOLD aus lässt los (mit SLIP: dort, wo die Platte ohne HOLD wäre).
- **33 Phrasen** nach dem Vorbild von Akais Flex Beat, über 1 oder 2 Takte, im PAT-Feld in eigenen Gruppen: DJ Scratch
  (Boom Bap, Techno / House), Turntablist, Backspins & Stops, Breaks / DnB / Big Beat, DJ Patterns. Sie spielen über die
  laufende Platte, die darunter im Takt bleibt; Start auf dem nächsten Takt. Bei einer Phrase wird das RATE-Feld zu
  MODE: LOOP, HOLD (solange PAT-Gate oder MIDI-Note gehalten), ONE SHOT. Im Display: Länge, Modus, Fortschritt.

### 2.0.0 (24. Nov. 2026, erster Test-Build, kein Release)
- Neues Modul **KRATZ** (24 HP, Look von GENRA TECHNO, Entwurf 5): eine Platte (WAV laden, Drag & Drop, REC von
  IN L/R), Plattenteller mit der Maus, Fader LVL und SCR (für MIDI-Fader, geglättet), 23 Scratch-Muster im Takt (RATE),
  HOLD, SLIP, LOOP, REV, CUT, 8 Cues, MOTION (ein Takt Handbewegung, geloopt), SPEED, PITCH, DRAG, TONE, RANGE.
- SYNC GENRA / SAMPLE / FREE (Standard SAMPLE, springt bei Follow GENRA oder CLK-Kabel auf GENRA), VIN/STR, TUNE
  OFF / KEY / BASS, Tonart- und Tempo-Erkennung beim Laden, MIDI-Eingang (Learn für SCR, LVL, Jogwheel; Noten für Cues,
  CUT und Muster), MIDI-MAP für alle Regler, Fader, Tasten und Felder.
