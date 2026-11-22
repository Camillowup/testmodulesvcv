# Rust-Klangkern 808/909

Synthetischer Klangkern für die Stimmen der TR-808 und TR-909, **ohne Samples**, mit Umschalter zwischen den beiden
Maschinen. Nur der Kern (Rust-Bibliothek), noch ohne Panel, Plugin oder App drumherum.

## Grundlage

- **TR-808 Service Notes (Roland, 1981):** Messtabelle S. 14 (Frequenz und Ausklingzeit jeder Stimme bei Reglern
  auf 12 Uhr), Schaltplan Voicing Board S. 9/13, Schaltungsbeschreibung S. 5/6.
- **TR-909 Service Notes (Roland, 1984):** Blockschaltbild S. 4, Schaltungsbeschreibung S. 5/6, Schaltplan
  Voicing Board S. 11 (Bauteilwerte abgelesen und Zeiten/Frequenzen daraus berechnet), Oszillogramme S. 9.
- Werner, Abel, Smith (CCRMA 2014): Analysen der 808-Bassdrum und des 808-Cymbals.

Die 808-Werte werden per Test gegen Rolands Messtabelle geprüft (`tr808_passt_zur_roland_messtabelle`).

## Virtual Analog (Schaltungssimulation)

Stimme für Stimme wird die Originalschaltung simuliert: Bauteile aus dem Schaltplan, gelöst wie in SPICE
(`schaltung.rs`: Knotenanalyse, Trapezregel, Newton, Dioden, Transistoren nach Ebers-Moll, Operationsverstärker
mit ±13,5 V Begrenzung).

**Fertig: 808-Bassdrum** (`va808_bd.rs`, Schaltplan S. 9, alle Bauteile Q41–Q43, C38–C47, R156–R177, D52/D53,
IC12a/b). Nicht simuliert: das Trigger-Tor Q39/Q40 (ersetzt durch die Triggerspannung 4 V bzw. bis 14 V mit
Akzent, 1 ms) und der Emitterfolger Q44 (Verstärkung ~1). Ergebnis gegen Rolands Messtabelle:

| | Roland (S. 14) | Simulation |
|---|---|---|
| Decay links / Mitte / rechts | 50 / 300 / 800 ms | 39 / 275 / 821 ms |
| Amplitude normal / Akzent | 3,5 / 10 Vpp | 3,5–4,1 / 11,4 Vpp |
| Frequenz | 56 Hz („typisch und variabel“) | 50–52 Hz (Stanford-Analyse derselben Schaltung: 49,5 Hz) |

Attack-Sprung (~110 Hz für die erste Halbwelle, ~4 ms) und der Tonhöhen-„Seufzer“ entstehen von selbst.
Rechenlast derzeit ca. 30 % eines CPU-Kerns für diese eine Stimme (noch nicht optimiert).

Die übrigen Stimmen sind noch die vereinfachten Modelle unten und folgen nach demselben Verfahren.

## Stimmen (11 Plätze)

| Platz | 808 | 909 |
|---|---|---|
| BD | Bridged-T 56 Hz, Decay 50/300/800 ms, Attack-Sprung (~4 ms), leichter Pitch-Seufzer, Tone-Tiefpass 160 Hz–7 kHz | Dreieck-VCO + Diodenformer, Sweep ~370 → 110 Hz, Tune = Sweep-Länge, Decay 15–345 ms, Attack = Impuls + Rauschen (~340 Hz) |
| SD | 238 + 476 Hz (Tabelle) oder 175 + 335 Hz (spätere Änderung), Snappy-Rauschen über Hochpass ~2,7 kHz | zwei VCOs 1 : 1,47, sinusartig, Pitch-Bend; Tone = Länge des Rauschens, Snappy = Anteil |
| LT/MT/HT | 90 / 135 / 185 Hz, 200 / 130 / 100 ms, Pitch fällt mit der Lautstärke, rosa Rauschen nur bei LT; umschaltbar auf **Congas** (185 / 280 / 400 Hz) | je drei VCOs (1 : 1,5 : 2,75), Sweep, Tom-Rauschen |
| RS | 455 + 1667 Hz, 10 ms; umschaltbar auf **Claves** (2500 Hz, 25 ms) | drei Bandpässe ~238 / 538 / 1360 Hz → Clipper → VCA → Hochpass |
| CP | Bandpass ~1 kHz, 3 Sägezahn-Stösse + paralleler Hall, 100 ms; umschaltbar auf **Maracas** | dasselbe Prinzip, 4 Stösse, enger |
| CH / OH | gemeinsame 6 Oszillatoren, Band ~7,1 kHz, Swing-VCA; 50 ms / 90–600 ms; CH würgt OH ab | synthetisch → 30-kHz-Abtastung → 6 Bit → VCA → Tiefpass, endet nach ROM-Länge |
| CY / CRASH | drei Bänder (hoch kurz, mittel regelbar, tief), 350/800/1200 ms | wie Hi-Hat-Kette; Tune ändert den ROM-Takt |
| CB / RIDE | 540 + 800 Hz, 50 ms, steiler Abfall am Anfang | wie Crash |

BD, SD und Toms der 909 teilen sich einen Rauschgenerator (wie im Original). Hi-Hat, Crash und Ride der echten 909
sind 6-Bit-Samples; hier werden sie nachempfunden, nicht kopiert.

## Regler pro Stimme (alle 0..1, Mitte = Originalwert)

`Pegel`, `Tune`, `Decay`, `Ton`, `Extra`. Was ein Regler bei welcher Stimme tut, liefert `Param::bedeutung()`
(`None` = ohne Wirkung). Jedes Modell merkt sich seine eigenen Einstellungen. Das endgültige Panel folgt später.

Akzent: Trommeln bis etwa dreimal so laut (laut Tabelle), Becken/Cowbell etwa doppelt.

## Benutzung

```rust
use drumkern::{DrumKern, Modell, Stimme, Param};

let mut kern = DrumKern::new(48_000.0);
kern.set_modell(Modell::Tr909);            // der Umschalter (oder kern.umschalten())
kern.set_param(Stimme::Snare, Param::Extra, 0.8); // Snappy
kern.ausloesen(Stimme::Bassdrum, true);    // true = Akzent
let summe = kern.naechstes_sample();
let einzeln = kern.einzelausgaenge();      // 11 Einzelausgänge

kern.set_808_congas(true);                 // 808-Varianten
kern.set_808_claves(true);
kern.set_808_maracas(true);
kern.set_808_snare_spaet(true);
```

## Testen und Hörbeispiele

```
cargo test --release
cargo run --release --bin demo -- hoerbeispiele
```

Keine Abhängigkeiten, nur Rust-Standardbibliothek.
