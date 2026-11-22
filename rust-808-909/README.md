# Rust-Klangkern 808/909

Synthetischer Klangkern für die Stimmen der TR-808 und TR-909, **ohne Samples**, mit Umschalter zwischen den beiden
Maschinen. Nur der Kern (Rust-Bibliothek), noch ohne Panel, Plugin oder App drumherum.

## Stimmen (11 Plätze)

| Platz | 808 | 909 |
|---|---|---|
| BD | angestossener Resonator (Bridged-T), kleiner Pitch-Drop, Klick | Sinus mit schnellem Pitch-Sweep, Sättigung, Attack-Klick |
| SD | zwei Resonatoren (180/330 Hz) + Rauschen | zwei Dreiecke mit Pitch-Drop + Rauschen |
| LT/MT/HT | Resonator + etwas Rauschen | Dreieck mit Pitch-Sweep + Rauschen |
| RS | zwei kurze Resonatoren | drei kurze Resonatoren |
| CP | gefiltertes Rauschen, 3 Stösse + Hallfahne | dasselbe, 4 Stösse |
| CH/OH | 6 Rechteck-Oszillatoren der 808, CH würgt OH ab | synthetisch nachempfunden (Metall + Rauschen), 6-Bit-Körnung |
| CY / CRASH | 808-Cymbal (zwei Bänder) | Crash, synthetisch nachempfunden |
| CB / RIDE | 808-Cowbell (540/800 Hz) | Ride, synthetisch nachempfunden |

Hinweis: Hi-Hats, Crash und Ride der echten 909 sind 6-Bit-Samples. Hier werden sie nachempfunden, nicht kopiert.

## Regler pro Stimme (alle 0..1)

`Pegel`, `Tune`, `Decay`, `Ton`, `Extra`. Was `Ton` und `Extra` bei welcher Stimme tun (Snappy, Attack, Pitch-Drop,
6-Bit …), liefert `Param::bedeutung()`. Jedes Modell merkt sich seine eigenen Einstellungen.

## Benutzung

```rust
use drumkern::{DrumKern, Modell, Stimme, Param};

let mut kern = DrumKern::new(48_000.0);
kern.set_modell(Modell::Tr909);            // der Umschalter (oder kern.umschalten())
kern.set_param(Stimme::Snare, Param::Extra, 0.8); // Snappy
kern.ausloesen(Stimme::Bassdrum, true);    // true = Akzent
let summe = kern.naechstes_sample();
let einzeln = kern.einzelausgaenge();      // 11 Einzelausgänge
```

Beim Umschalten klingt, was gerade läuft, natürlich aus. Jeder Schlag hat eine winzige Zufallsabweichung (Tonhöhe,
Pegel), damit es lebendig klingt.

## Testen und Hörbeispiele

```
cargo test --release
cargo run --release --bin demo -- hoerbeispiele
```

Das Demo schreibt alle Einzelschläge (normal + Akzent) beider Modelle und drei Grooves als WAV.

Keine Abhängigkeiten, nur Rust-Standardbibliothek. Gedacht als Grundlage für ein späteres VST/CLAP-Plugin,
VCV-Rack-Modul (über C-Schnittstelle) oder die Android-App.
