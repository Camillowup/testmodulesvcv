# Merkliste GENRA Test (offen)

Nur Offenes für dieses Testplugin. Die grosse Merkliste der Familie liegt im GENRA-Repo (`docs/MERKLISTE.md` in
Camillowup/Genra-for-Vcv), die Regeln fürs Aussehen in dessen `docs/DESIGN.md`. Gebaut wird erst, wenn ausdrücklich
„bauen“ kommt; vorher wird jedes Panel als Entwurf gezeigt.

## Regel: Look wie die Vorlagen

Alle Module sollen aussehen wie **GENRA MONO, GRUND, FUNKE und SCHMELZ**: Preset-Feld im Kopf (Mausrad wechselt),
Display über die volle Breite, Sektionen mit Farblinie, der wichtigste Regler gross mit gepunktetem Ring in der
Akzentfarbe, Werte im Display statt unter den Reglern, alle Buchsen unten in zwei Reihen (GENRA-Leiste und CV oben,
Ausgänge auf farbigen Platten unten). Tasten gross genug (wie im MATH-Muster), Schriften überlappen nie, Panels so
schmal wie möglich. Alle Dropdown-Felder wechseln auch per Mausrad.

## Stand

Seit 2.9.0: SCHLEIFE (Looper, Test). Frühere Module: MATH, KURBEL, SWARM, LIED, CANTA, MIAOU, SAURUS, FUMI, KESSEL und KESSEL FX sind getestet
und mit GENRA 2.38.0 nach GENRA Instruments umgezogen. Ihre offenen Punkte (CANTA: „Arme hoch im Refrain“ und
Verbeugung am Ende; KESSEL: MIDI-Eingang im Modul) stehen jetzt in der Merkliste im GENRA-Repo.

## SCHLEIFE (Looper, seit 2.9.0)
- 2.9.0 gebaut (Test): in Rack prüfen – Aufnahme im Takt mit GENRA (CLOCK + RESET), 1–16 Takte pro Spur, REC/DUB,
  UNDO/Redo, CLEAR, CLR ALL, MAIN IN gegen IN 1–4, OUT 1–4 und MIX L/R, Stopp/Start von GENRA (Schleifen warten und
  starten von vorn), Speichern im Patch und Laden, Export (Spur, alle, Mix), MPD218 Bank B über MAP-X 2.1.0.
- Später: Presets (z. B. Längen aller Spuren), evtl. mehr Spuren (Expander).

## GENRA CHOP

Im eigenen Repo `Camillowup/Genra-Chop` (Merkliste dort in `docs/MERKLISTE.md`).

## GENRA GLANZ (eigenes Plugin in `genra-glanz/`, seit 19. Nov. 2026)
- 2.0.0 gebaut (Test): in Rack prüfen, ob jede Szene flüssig läuft und mit GENRA in den Spurfarben reagiert.
- Später: weitere Szenen (immer hinten anhängen), z. B. KALEIDOSKOP, Wellen-Landschaft, Akkordkreis.

## GENRA NEON (eigenes Plugin in `genra-neon/`, seit 19. Nov. 2026)
Neon als Überbegriff: neue Module im NEON-TERRAIN-Look kommen hierher (Name deutsch, alles andere Englisch).
Entwürfe im Archiv: `genra-neon/docs/archiv/` (dort auch die Ideen BIEST, Cyberdeck und Atom-Terminal).
- 2.0.0 gebaut (Test): WELLE und NEBEL in Rack prüfen – Klang der Presets (gleich laut? die perkussiven sind etwas
  leiser), Follow mit GENRA und GENRA TECHNO, MIDI-MAP (auch Preset-Feld), eigene WAV-Wavetables (SET ROOT FOLDER,
  Unterordner, TABLE bleibt am Ordnerende), MUTATE/UNDO/INIT, FREEZE, CPU bei vielen Stimmen (NEBEL ADDITIVE).
- Merkliste: Idee BIEST-Look (Maul-Display, Augen-Regler) behalten.
- 2.1.0 gebaut (Test): WOGE, PULS, GLAS, FLUT in Rack prüfen – Klang und Lautstärke der Presets (die rhythmischen
  PULS-Presets sind von Natur aus etwas leiser), Malen in GLAS (Werkzeuge, rechte Maustaste, SNAP, LINE), GLAS REC mit
  einem Eingang, FLUT-Gezeit (SYNC/FREE, TIME), PULS-Modi CLOCK/FREE/NOTE und die Masken-Schritte, CPU.
- 2.1.1 gebaut (Test): WELLE/NEBEL – Preset-Wechsel ohne Knacken (auch Mausrad), Spitzen unter 5 V, LEVEL-Regler,
  schmale Panels (WELLE 34 HP, NEBEL 35 HP, CV-Buchsen unter den Reglern; alte Patches laden, Buchsen sitzen neu).
  WOGE/PULS/GLAS/FLUT: Presets neu ausgesteuert (Spitzen unter 5 V statt bis 8 V; einige PULS-Presets leiser).
- Offen für WOGE, PULS, GLAS, FLUT: Schutz gegen Knacken beim Preset-Wechsel wie in WELLE/NEBEL, LEVEL-Regler
  (fehlt bei WOGE, PULS, GLAS), schmale Panels mit CV-Buchsen unter den Reglern (Panel-Entwurf zuerst zeigen).
- Warteliste PULS: Clock-Multiplikator (×2 … ×64 bis in den Ton, taktfest).
- Offen: NEBEL nach dem Laniakea Pro gebaut – Bastian prüft, wie nah es dran ist (eigene Module gehören hierher).

## GENRA KEHLE (eigenes Test-Plugin in `genra-kehle/`, seit 20. Nov. 2026)
Neon-Vocoder KEHLE (24 HP, NEON-TERRAIN). Bleibt einzeln, bis er funktioniert; dann Umzug nach GENRA Neon Punk
(`genra-neon/`, Branch `neon-punk`) und im selben Schritt hier raus (Plugin und Workflow `kehle.yml` entfernen).
- 2.0.0 gebaut (Test): in Rack prüfen – Stimme über Rack's Audio-Modul in MOD, Träger extern (SCHMELZ in CAR L/R)
  und eingebaut (V/OCT + GATE, Follow GENRA, ohne Kabel C-Moll), alle acht CHARACTER und ihre Figuren, FORMANT/SHIFT,
  SIBIL, FREEZE, HOLD, Lautstärke der 16 Presets mit echter Stimme (ausgesteuert nur mit einer Test-Stimme), CPU.

## GENRA KRATZ VCV (eigenes Test-Plugin in `genra-kratz/`, seit 24. Nov. 2026)
Scratch-Modul KRATZ (24 HP, Look von GENRA TECHNO). Bleibt einzeln, bis es funktioniert; dann Umzug ins Plugin
GENRA TECHNO (Repo Technogen; dort die eigene Kopie von Follow/Link statt `genra/Follow.cpp`) und im selben Schritt hier
raus (Plugin und Workflow `kratz.yml` entfernen).
- 2.0.0 gebaut (Test): in Rack prüfen – Laden (Root wie SCHLAG), REC, Plattenteller, LVL/SCR mit MIDI-Fadern, alle
  Muster, HOLD/SLIP/LOOP/REV, Cues, MOTION, SYNC mit GENRA (VIN/STR), TUNE KEY/BASS, Tonart-/Tempo-Erkennung, CPU.
- 2.1.0 gebaut (Test): Bewegung ohne Kratzen (Maus/Jog/SCR verteilt), HOLD hält immer, 33 Phrasen (Flex-Beat-Stil,
  LOOP / HOLD / ONE SHOT). In Rack prüfen, ob es jetzt nach Scratch klingt und die Phrasen im Takt bleiben.
- Später (braucht Umbau in GENRA): GENRA richtet sich nach dem Sample; Melodie aus Samples herauslesen (evtl. MOTIV).

