# GENRA KEHLE – Versionsgeschichte

### 2.1.0 (20. Nov. 2026, Test-Build, kein Release)
- **Kein Klirren mehr beim Singen:** Die Hüllkurven der Bänder werden nachgeglättet (zwei Pole, 30 Hz) und die Attack
  beginnt bei 2 ms statt 0,5 ms. Die Schwingung der Stimme selbst landet nicht mehr auf dem Träger (Flattern der Bänder
  von 21 % auf 1–2 %, Anteile über 6 kHz beim Singen rund 30 dB leiser).
- **SIBIL nur noch bei echten Zischlauten:** öffnet erst, wenn die Höhen der Stimme lauter sind als ihr Körper (S, T, F),
  nicht mehr bei gesungenen Vokalen; leiser beigemischt.
- Weniger Höhenbetonung der Analyse-Bänder, sanfter Tiefpass (~11 kHz) am Ausgang, leichtes Noise-Gate an MOD.
- **CRUSH entfernt** (kreischte): Klangart, Pixel-Figur und die Presets Speech Chip und Lo-Fi Bot. Es bleiben sieben
  CHARACTER. Alte Patches mit TALKBOX zeigen jetzt die richtige Klangart nur nach neuem Auswählen (Test-Plugin).
- Neue Presets **Vintage Strings** und **Velvet Pad** (CLASSIC mit STRINGS-Träger). Presets neu ausgesteuert.
- Test: `kehle_test` prüft jetzt auch Flattern, Höhen-Fizz und SIBIL (Vokal zu, S auf).

### 2.0.0 (20. Nov. 2026, Test-Build, kein Release)
- Neues Test-Plugin `GenraKehle` (Ordner `genra-kehle/`, Workflow `kehle.yml`) mit dem Vocoder **KEHLE** (24 HP,
  NEON-TERRAIN-Look). Bleibt bis nach dem Test einzeln; danach Umzug nach GENRA Neon Punk (hier im selben Schritt raus).
- Kanal-Vocoder mit 8–32 Bändern; Stimme in MOD, Träger in CAR L/R oder eingebauter Träger (SAW CHORD, PULSE, STRINGS,
  NOISE) auf V/OCT + GATE, Follow GENRA. Acht CHARACTER: CLASSIC, WHISPER, CHOIR, METAL, GLASS, SHIFT, CRUSH, TALKBOX.
- FORMANT, RESO, SIBIL, OCT, ENSEMBLE, ATTACK, RELEASE, DRY, LEVEL; FREEZE, HOLD, UNDO, INIT; ENV-Ausgang.
- Display: Bänder als Neon-Landschaft, pro CHARACTER eine animierte Figur (Roboter, Geist, Chor, Blechbüchse, Kristall,
  Monster/Maus, Pixelgesicht, Talkbox-Typ), der Mund geht mit der Stimme.
- 16 Werks-Presets nach Klang (ROBOT, CHOIR, WHISPER, FX), mit `test/kehle_test.cpp` ausgesteuert (Spitzen unter 5 V).
