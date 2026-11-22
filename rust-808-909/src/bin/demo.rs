//! Rendert Hoerbeispiele als WAV: jede Stimme einzeln (normal + Akzent)
//! fuer beide Modelle, dazu Grooves und einen Groove mit Umschalter.
//!
//! Aufruf: cargo run --release --bin demo -- [Zielordner]

use drumkern::{DrumKern, Modell, Stimme};
use std::fs;
use std::io::Write;
use std::path::Path;

const FS: f32 = 48_000.0;

fn wav_schreiben(pfad: &Path, daten: &[f32]) {
    let mut f = fs::File::create(pfad).expect("WAV anlegen");
    let n = daten.len() as u32;
    let bytes = n * 2;
    let sr = FS as u32;
    f.write_all(b"RIFF").unwrap();
    f.write_all(&(36 + bytes).to_le_bytes()).unwrap();
    f.write_all(b"WAVEfmt ").unwrap();
    f.write_all(&16u32.to_le_bytes()).unwrap();
    f.write_all(&1u16.to_le_bytes()).unwrap(); // PCM
    f.write_all(&1u16.to_le_bytes()).unwrap(); // mono
    f.write_all(&sr.to_le_bytes()).unwrap();
    f.write_all(&(sr * 2).to_le_bytes()).unwrap();
    f.write_all(&2u16.to_le_bytes()).unwrap();
    f.write_all(&16u16.to_le_bytes()).unwrap();
    f.write_all(b"data").unwrap();
    f.write_all(&bytes.to_le_bytes()).unwrap();
    for s in daten {
        let v = (s.clamp(-1.0, 1.0) * 32767.0) as i16;
        f.write_all(&v.to_le_bytes()).unwrap();
    }
}

fn einzelschlag(m: Modell, s: Stimme) -> Vec<f32> {
    einzelschlag_mit(m, s, |_| {})
}

fn einzelschlag_mit(m: Modell, s: Stimme, vorbereiten: impl Fn(&mut DrumKern)) -> Vec<f32> {
    let mut k = DrumKern::new(FS);
    k.set_modell(m);
    vorbereiten(&mut k);
    let mut out = Vec::new();
    // normal, nach 1.2 s mit Akzent, dann ausklingen lassen
    k.ausloesen(s, false);
    for i in 0..(FS * 3.0) as usize {
        if i == (FS * 1.2) as usize {
            k.ausloesen(s, true);
        }
        out.push(k.naechstes_sample());
    }
    out
}

/// 16 Schritte; Eintrag: (Schritt, Stimme, Akzent)
fn muster(takt: usize) -> Vec<(usize, Stimme, bool)> {
    use Stimme::*;
    let mut m = Vec::new();
    for s in [0, 4, 8, 12] {
        m.push((s, Bassdrum, s == 0));
    }
    for s in [4, 12] {
        m.push((s, Clap, false));
    }
    for s in [2, 6, 10, 14] {
        m.push((s, HiHatOffen, false));
    }
    for s in [0, 1, 3, 4, 5, 7, 8, 9, 11, 12, 13, 15] {
        m.push((s, HiHatZu, s % 4 == 0));
    }
    m.push((3, Rimshot, false));
    m.push((11, Rimshot, false));
    m.push((7, Extra, false));
    m.push((15, Extra, false));
    if takt % 4 == 0 {
        m.push((0, Becken, true));
    }
    if takt % 4 == 3 {
        m.push((12, Snare, true));
        m.push((13, TomHoch, false));
        m.push((14, TomMittel, false));
        m.push((15, TomTief, false));
    } else {
        m.push((12, Snare, false));
    }
    m
}

/// `modell_pro_takt` waehlt fuer jeden Takt die Maschine (der Umschalter).
fn groove(takte: usize, modell_pro_takt: impl Fn(usize) -> Modell) -> Vec<f32> {
    let bpm = 125.0;
    let schritt = (FS * 60.0 / bpm / 4.0) as usize;
    let mut k = DrumKern::new(FS);
    let mut out = Vec::new();
    for takt in 0..takte {
        k.set_modell(modell_pro_takt(takt));
        let mut m = muster(takt);
        m.sort_by_key(|e| e.0);
        for st in 0..16 {
            for (_, s, a) in m.iter().filter(|e| e.0 == st) {
                k.ausloesen(*s, *a);
            }
            for _ in 0..schritt {
                out.push(k.naechstes_sample());
            }
        }
    }
    for _ in 0..(FS * 1.5) as usize {
        out.push(k.naechstes_sample());
    }
    out
}

fn main() {
    let ziel = std::env::args().nth(1).unwrap_or_else(|| "hoerbeispiele".to_string());
    let ziel = Path::new(&ziel);
    for m in [Modell::Tr808, Modell::Tr909] {
        let ordner = ziel.join(format!("einzeln_{}", m.name()));
        fs::create_dir_all(&ordner).unwrap();
        for (i, s) in Stimme::ALLE.iter().enumerate() {
            let daten = einzelschlag(m, *s);
            let spitze = daten.iter().fold(0.0f32, |a, b| a.max(b.abs()));
            let name = format!("{:02}_{}.wav", i + 1, s.kurzname(m));
            wav_schreiben(&ordner.join(&name), &daten);
            println!("{} {:<10} Spitze {:.2}", m.name(), name, spitze);
        }
    }
    // 808-Varianten (Schalter wie im Original)
    let v = ziel.join("varianten_808");
    fs::create_dir_all(&v).unwrap();
    wav_schreiben(&v.join("SD_tabelle_238_476Hz.wav"), &einzelschlag(Modell::Tr808, Stimme::Snare));
    wav_schreiben(
        &v.join("SD_spaet_175_335Hz.wav"),
        &einzelschlag_mit(Modell::Tr808, Stimme::Snare, |k| k.set_808_snare_spaet(true)),
    );
    for (s, n) in [(Stimme::TomTief, "LC"), (Stimme::TomMittel, "MC"), (Stimme::TomHoch, "HC")] {
        wav_schreiben(&v.join(format!("{}_conga.wav", n)), &einzelschlag_mit(Modell::Tr808, s, |k| k.set_808_congas(true)));
    }
    wav_schreiben(&v.join("CL_claves.wav"), &einzelschlag_mit(Modell::Tr808, Stimme::Rimshot, |k| k.set_808_claves(true)));
    wav_schreiben(&v.join("MA_maracas.wav"), &einzelschlag_mit(Modell::Tr808, Stimme::Clap, |k| k.set_808_maracas(true)));

    wav_schreiben(&ziel.join("groove_808.wav"), &groove(8, |_| Modell::Tr808));
    wav_schreiben(&ziel.join("groove_909.wav"), &groove(8, |_| Modell::Tr909));
    wav_schreiben(
        &ziel.join("groove_umschalter.wav"),
        &groove(8, |t| if (t / 2) % 2 == 0 { Modell::Tr808 } else { Modell::Tr909 }),
    );
    println!("fertig: {}", ziel.display());
}
