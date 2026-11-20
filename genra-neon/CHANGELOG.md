# GENRA NEON – Versionsgeschichte

### 2.1.2 (20. Nov. 2026, Test-Build, kein Release)
Reparaturen:
- Hall NEBULA (Shimmer) schaukelte sich bei langer Hallzeit auf: nach 20–30 s hing der Ausgang an 8 V (FLUT, NEBEL).
  Shimmer-Rückführung begrenzt, Hall-Tank gesättigt, DC-Filter im Hall.
- FLUT: GRIT-Knistern ohne Schlagseite, DC-Filter an L, R und SUB; Presets neu ausgesteuert.
- NEBEL: Textur FOLD erzeugte Gleichspannung (bis 0,9 V, auch in der Stille) – DC-Filter an den Ausgängen.
- PULS: im Modus FREE konnte Gleichspannung entstehen – DC-Filter an den Ausgängen.
- Hall: beim Preset-Wechsel werden jetzt auch Shimmer-Puffer und DC-Filter geleert (kein Nachklang des alten Sounds).
- WELLE LOAD WAV: WAV-Dateien mit zu grosser Framegrösse im Header lasen über das Dateiende (Absturzgefahr);
  kaputte Werte in Float-WAVs werden abgefangen.
- PULS: mit CV an DENS/ROT wurden beim Laden eines Patches die gespeicherten Schritte überschrieben.

Panels WELLE und NEBEL:
- Grössere Schrift wie bei FUNKE/SCHMELZ: Beschriftungen 1,4–1,65 mm (vorher 1,05–1,3 mm), Sektionsnamen 1,7 mm;
  Sektionsreiter passen sich der Textlänge an. (Entwürfe WOGE/PULS/GLAS/FLUT in docs/panels/ ebenso.)

### 2.1.1 (20. Nov. 2026, Test-Build, kein Release)
WELLE und NEBEL:
- Kein Knacken mehr beim Preset-Wechsel (auch per Mausrad): kurz stumm (4 ms), Filter und Effekt-Fahnen starten sauber,
  gehaltene Noten werden mit dem neuen Klang neu angeschlagen, dann Einblenden (30 ms). Gleiches beim Wechsel von
  Filtertyp, Tabelle (WELLE) und Generator, Textur, Raum (NEBEL).
- Presets neu ausgesteuert: kein Preset erreicht im normalen Spiel den Begrenzer (Spitzen unter 5 V statt bis 8 V).
- LEVEL-Regler (Ausgangspegel) in der PLAY-Sektion beider Module; nicht Teil der Presets.
- Schmalere Panels: CV-Buchsen direkt unter ihren Reglern, Auswahlfelder neben den Sektionstiteln.
  WELLE 34 HP (vorher 44), NEBEL 35 HP (vorher 44); unten nur noch GENRA-Leiste, CLOCK/RESET, Audio, Ausgänge.
- NEBEL: STRUM liess Stimmen verstummen (behoben); MODAL und STRING lauter, damit die Presets weniger Verstärkung brauchen.

WOGE, PULS, GLAS, FLUT:
- Presets neu ausgesteuert: Spitzen unter 5 V statt bis 8 V (einige rhythmische PULS-Presets dadurch leiser).

### 2.1.0 (20. Nov. 2026, Test-Build, kein Release)
- Vier neue eigene Module im NEON-TERRAIN-Look (Entwürfe vom 19. Nov. 2026):
  - **WOGE**: Scanned Synthesis – Saite aus 24 Massen (MASS, TENSION, DAMP, CENTER), Anregung HAMMER/PLUCK/NOISE/BOW/
    AUDIO mit EXC-Eingang, PATH/WIDTH/DRIFT, Filter, POLY 4/MONO/LEGATO, STRIKE, FREEZE, SHAPE-Ausgang, 18 Presets.
  - **PULS**: Pulsar-Synthese – Modi CLOCK, FREE, NOTE; zwei Formanten, Pulsaret-Welle und -Fenster, DUTY, SKEW,
    Maske mit 16 klickbaren Schritten (EUCLID, RANDOM, BURST, ALL, EDIT), SPREAD/JITTER/TAIL, PULSE-Ausgang, 16 Presets.
  - **GLAS**: ANS-Synthesizer – mit der Maus auf die Glasplatte malen (PEN, SPRAY, LINE, ERASE; rechte Maustaste
    radiert), SNAP-Taster, SCALE mit GENRA KEY und GENRA CHORD, Bilder-Bank, NEW/CLEAR/REC (Spektrum des Eingangs),
    BRIGHT-Ausgang, 12 Presets.
  - **FLUT**: Gezeiten-Drone – bis 6 Stimmen auf GENRAs Akkord, DRIFT, MORPH, SUB −1/−2 Oktaven, TIDE SYNC/FREE mit
    TIME, Texturen, BLOOM/HAZE, TIDE-, SUB- und POLY-Ausgang, 14 Presets.
- Alle vier: Follow GENRA, MIDI-MAP auf allen Reglern, Feldern, Tastern und dem Preset-Feld, viele CV-Eingänge ohne
  Abschwächer, keine eingebauten LFOs, UNDO/INIT, Presets per Test ausgesteuert.

### 2.0.0 (19. Nov. 2026, Test-Build, kein Release)
- Neues Plugin im NEON-TERRAIN-Look mit zwei Modulen:
  - **WELLE**: Wavetable-Synth, 2 Oszillatoren, 32 eigene Tabellen in 5 Ordnern, eigene WAV-Wavetables (Hauptordner per
    Windows-Fenster), TABLE bleibt am Ordnerende stehen, 6 Warps, Filter (GENRAs 8 Modelle), MOTION, Unison,
    POLY/MONO/LEGATO, MUTATE/FREEZE/UNDO/INIT, 22 CV-Eingänge plus FM, MOTION-Ausgang, 32 Presets.
  - **NEBEL**: Cluster-Universum, 5 Generatoren, 14 Akkorde, Akkorde von GENRA (CHORD GENRA), STRUM, 5 Texturen,
    3 Reverbs (NEBULA, HALL, VOID), ORBIT, BIG BANG/FREEZE/UNDO/INIT, 21 CV-Eingänge plus IN L/R, ORBIT-Ausgang,
    24 Presets.
- Beide: Follow GENRA (GENRA und GENRA TECHNO), MIDI-MAP auf allen Reglern, Tastern, Feldern und dem Preset-Feld,
  Presets per Test ausgesteuert.
