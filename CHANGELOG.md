# GENRA Test – Versionsgeschichte

Was in welcher Version dazukam (neueste zuerst). Offenes steht in `docs/MERKLISTE.md`.

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
