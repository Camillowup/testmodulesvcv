# GENRA Test – Versionsgeschichte

Was in welcher Version dazukam (neueste zuerst). Offenes steht in `docs/MERKLISTE.md`.

### GENRA CHOP 2.4.0 (eigenes Plugin in `genra-chop/`)
- **GENRA CHOP:**
  - **WARP** (Leiste unter dem Wellenform-Fenster): das Sample ans Tempo des Beats anpassen, Modus AUTO / BEATS /
    TONE, Tempo-Anzeige „87 > 92 BPM“ mit x2 und /2, oben rechts im Wellenform-Fenster RUBBER | SIGNAL (Rubber Band R3
    oder Signalsmith Stretch). Nur das Sample, nicht der Break; ein neues BPM warpt nach kurzer Pause neu.
  - **SAMPLE** spielt das ganze Sample (nochmal = Stopp), mit Laufzeile im Wellenform-Fenster.
  - **CLEAR pro Spur** neben NEW (Akkorde bleiben).
  - **Pads in MPC-Reihenfolge** (1–4 unten); lange Akkordnamen werden kleiner.
  - **Drums:** Reihenfolge Kick, Snare, CH, OH, Clap, Rim, Tom, Perc (Ausgänge und GRID); DENS bringt stiltypische
    Extras (Rim-Ghosts, Tamburin/Shaker, offene Hats, Clap-Layer, Tom-Fill in Takt 4).
  - **NEW ALL** schneidet frisch: CHOPS auf 16, MOVE zurück, neuer Schnitt im eingestellten MODE.
- **GENRA CHOP GRID:** ohne eigenen MIDI-Eingang nimmt REC den von GENRA CHOP.
- **GENRA CHOP FX** neu auf **36 HP**: acht Quellen einzeln rein (IN L/R, KICK, SNR, CH, OH, BASS, MEL L/R, PADS L/R)
  mit Wähler DRY / 1 / 2 / 1+2, jede einzeln wieder raus, dazu RET 1 L/R, RET 2 L/R und MIX L/R (für WING-LINK am X).
  OLED-Displays über den Reglern, CV darunter, Buchsen in zwei Reihen; acht DJ-Pads (halten = wirkt auf alle
  Ausgänge, Rechtsklick: jeder Effekt oder *Copy settings from BUS*), DJ FILTER, acht Schnelltasten; neue Effekte
  LOW CUT und HIGH CUT (39). 2.3.0-Patches mit GENRA CHOP FX laden die Kabel nicht mehr richtig.
- **GENRA CHOP ABLAUF:** die Hinweiszeile unter den Pads passt in ihr Feld; Pads per Drag & Drop in die Song-Liste
  (rote Linie zeigt die Stelle), Zeilen per Ziehen verschieben, Scrollbalken und Auto-Scroll am Rand.
- **Fremder Code:** Rubber Band Library 4.0.0 (GPL-2.0-or-later) und Signalsmith Stretch / Linear (MIT) in `dep/`,
  Herkunft und Lizenzen in `licenses/`.

### GENRA CHOP 2.3.0 (eigenes Plugin in `genra-chop/`)
- **GENRA CHOP FX** (neu, 22 HP): zwei Effekt-Busse zum Live-Spielen nach Vorbild SP-404, 37 Effekte in sechs Gruppen
  (TEXTURE, FILTER, DRIVE, SPACE, MOD, PERFORM), CTRL 1–3 pro Bus, FX ON (antippen = an/aus, halten = solange
  gehalten), sechs frei belegbare Schnelltasten, 20 Presets, ROUTE 1>2 / 2>1 / parallel, Tempo von einer 16tel-Clock,
  CV, MIDI learn. Kabelloses Verbinden ist vorbereitet (noch nicht drin). Engine-Test `test/fx_test.cpp` im Build.
- **GENRA CHOP:** DUST ist weg, an seiner Stelle **CHOPS** (4 bis 64 Chops, ruht der Regler, wird neu geschnitten);
  Pads in **Banks A–D** (64 Chops, MIDI C1 = Pad 1 bis Pad 64); MODE nur noch Transient / Equal; **STOP und PLAY
  grösser** (14,6 × 8,4 mm). Alte 2.2.0-Patches laden (CHOPS startet dann bei 16).
- **Taster-Schrift** passt sich in allen drei bisherigen Modulen automatisch an (LOAD SAMPLE ragte hinaus).
- **GENRA CHOP ABLAUF:** VARIATION RENEWS weg (jeder Druck auf einen Teil = neues Pattern; Bass, Melodie, Pads neu,
  Drums und Chop vom Original); darunter **CLEAR** (dann ein Pad) und **CLEAR ALL** (zweimal), UNDO holt zurück.
- **GENRA CHOP GRID:** schwarze Tasten links schwarz; das Chop-Raster hat so viele Zeilen wie Chops (mind. 16).

### GENRA CHOP 2.2.0 (eigenes Plugin in `genra-chop/`)
- **Umbenannt:** GENRA HipHop heisst **GENRA CHOP** (Slug `GenraChop`, Ordner `genra-chop/`), der Grid-Editor
  **GENRA CHOP GRID**. Alte Test-Patches laden nicht mehr.
- **MOVE** (Startpunkt des zuletzt angetippten Chops), **CLEAR** (ganzes Chop-Pattern; halten + Pad = nur dieses Pad),
  **LOAD SAMPLE** gross und gelb neben dem Wellenform-Fenster, der NEW der Chop-Spur ist weg.
- **MIDI learn** für alle Regler, Taster und Felder; pro Controller und Computer gespeichert, bis zu drei Controller,
  Encoder-Erkennung, Export/Import.
- **GENRA CHOP ABLAUF** (neu, 26 HP): 64 Pattern-Pads, Varianten INTRO bis OUTRO, Song-Liste, MAKE SONG in sieben
  Hip-Hop-Formen, Übergänge nach Stil, Song-MIDI-Export.
- Fehler: KEY von Hand würfelte nur den Bass neu statt Bass, Melodie und Pads.

### 2.7.0 – Durchsicht aller Module: Fehler behoben, weniger CPU
Alle Parameter und Buchsen sind gleich geblieben, alte Patches laden unverändert. Der Klang ist gleich, ausser wo es
unten steht (FUMIs Lautstärke; KESSELs Rauschen läuft nach einer Pause anders weiter).

**Fehler**
- **FUMI: Ausgang begrenzt, alle Instrumente gleich laut.** Akkorde kamen mit 6 bis über 20 V heraus (kein Limiter).
  Jetzt sitzt am Ausgang derselbe Limiter wie bei KESSEL (bis 6 V unverändert, weich bis 8 V), und jedes der sechs
  Instrumente hat einen eigenen Pegel (`src/fumi/FumiLevels.hpp`, gemessen mit `tools/fumi_levels.c`): ein Dreiklang
  (Velocity 100, LEVEL 80) kommt bei 6 V an, erst darüber greift der Limiter.
- **LIED singt auch ohne Panel** (Rack headless, Plugin-Host mit geschlossenem Fenster): bisher bat nur das Panel um
  die Zeilen; jetzt schaut der Sprecher-Thread selbst nach Text und Stimme, wenn kein Panel da ist.
- **Note erst zwei Samples nach der Gate-Flanke** (MATH, SAURUS, FUMI, MIAOU): V/OCT und VEL werden dann gelesen. Kam
  die Tonhöhe einen Sample nach dem Gate (z. B. durch einen Quantizer), klang die Note erst falsch und wurde gleich
  neu angeschlagen. Eine neue Tonhöhe unter gehaltenem Gate behält bei MATH und SAURUS die Velocity der Note (vorher
  fest 100). **MIAOU: das Banjo folgt V/OCT** unter gehaltenem Gate (die Saite bekommt die neue Länge).
- **KESSEL, Speicherzugriffe hinter Tabellenenden** (im LXR-Code, `kessel/SNAPSHOT.txt`): RAT-Verzerrer bei
  Vollaussteuerung und die interpolierten Wellentabellen der Oszillatoren lasen ein Feld zu weit. **DELAY → PING
  PONG** las und schrieb beim Umschalten einmal bis zu 64 kB hinter den Effektspeicher (behoben in `kesselcore.c`).
- **Kein Aussetzer mehr beim Einfügen eines Moduls:** die Engine-Bibliothek wird im Konstruktor geladen, nicht mehr
  in `onAdd` (dort wartet Racks Audio-Thread).
- **CANTA:** der Audio-Thread wartet nie mehr auf ein Schloss und legt keinen Speicher mehr an, wenn Text oder Preset
  wechseln (feste Puffer, `try_lock`). Texte über 4095 Zeichen werden dort abgeschnitten.
- **LIED:** ein Wort, das länger gesprochen wird als der Puffer fasst, kann keinen ungültigen Ausschnitt mehr ergeben;
  ein Presetwechsel am Panel steht jetzt in Racks Undo-Verlauf.

**Weniger CPU**
- **MIAOU:** Delay und Reverb ruhen, solange ihr Send auf 0 steht und der Ausklang verstummt ist (unter −120 dB, eine
  Sekunde lang); der Chorus rechnet bei 0 nur seinen Speicher weiter. Vorher kosteten die Effekte auch bei 0 etwa
  zehnmal so viel wie der Klang selbst. Solange ein Send offen ist, ist der Klang Bit für Bit gleich; nach einer Ruhe
  steht nur die langsame Schwebung des Reverbs an einer anderen Stelle.
- **KESSEL:** Stimmen, die zwei Sekunden lang nur Stille geliefert haben, werden bis zum nächsten Schlag (oder einer
  Änderung an ihnen) nicht mehr berechnet (Leerlauf des Kerns 13 → 0,1 µs je Block). Der Ratenwandler ist geteilt:
  Mix und Effekt-Return immer, die sieben Einzelausgänge nur, solange einer gesteckt ist; beide ruhen bei Stille.
  Der Eingang des Expanders wird nur gewandelt, wenn dort ein Kabel steckt. Die Regler der Spur einmal je Block.
- **Überall:** Anzeigewerte werden nur noch geschrieben, wenn sie sich ändern; Anfragen vom Panel kosten im
  Audio-Thread nichts mehr, solange keine da ist; die Kanal-Schleifen laufen nur über die benutzten Kanäle; FUMI fragt
  die Tonhöhe nur noch bei einer Änderung ab; SWARM und LIED rechnen feste Koeffizienten nicht mehr jedes Sample
  (LIED Bit für Bit gleich, geprüft).
- Tipp: bei 44,1 kHz in Rack entfällt die Ratenwandlung aller Module mit eigenem Kern ganz.

**Nicht geändert:** MATH und SWARM zeichnen ihr Bild weiter im Audio-Thread (30 Mal je Sekunde, nur solange es zu
sehen ist): die Zeichnung der Firmware liest den laufenden Zustand der Engine und lässt sich nicht gefahrlos in einen
anderen Thread legen.

### 2.6.0 – KESSEL und KESSEL FX
- Neu: KESSEL (die Drum-Engine der LXR-Firmware) mit dem Expander KESSEL FX.
- Alle Module im Look der Vorlagen (Panels aus `tools/gpanel.py`), Sektionen bündig 2 mm über den Buchsen.
