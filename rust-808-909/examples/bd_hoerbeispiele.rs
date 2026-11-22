use drumkern::{DrumKern, Modell, Param, Stimme};
use std::io::Write;
fn wav(p: &str, d: &[f32]) {
    let mut f = std::fs::File::create(p).unwrap();
    let n = d.len() as u32;
    for (a, b) in [(b"RIFF", 36 + n * 2)] { f.write_all(a).unwrap(); f.write_all(&b.to_le_bytes()).unwrap(); }
    f.write_all(b"WAVEfmt ").unwrap();
    for x in [16u32] { f.write_all(&x.to_le_bytes()).unwrap(); }
    for x in [1u16, 1] { f.write_all(&x.to_le_bytes()).unwrap(); }
    for x in [48000u32, 96000] { f.write_all(&x.to_le_bytes()).unwrap(); }
    for x in [2u16, 16] { f.write_all(&x.to_le_bytes()).unwrap(); }
    f.write_all(b"data").unwrap(); f.write_all(&(n * 2).to_le_bytes()).unwrap();
    for s in d { f.write_all(&((s.clamp(-1.0, 1.0) * 32767.0) as i16).to_le_bytes()).unwrap(); }
}
fn main() {
    let ziel = std::env::args().nth(1).unwrap();
    std::fs::create_dir_all(&ziel).unwrap();
    let fs = 48_000.0;
    // Decay links / Mitte / rechts, jeweils normal und mit Akzent
    let mut out = Vec::new();
    for d in [0.0, 0.5, 1.0] {
        let mut k = DrumKern::new(fs);
        k.set_modell(Modell::Tr808);
        k.set_param(Stimme::Bassdrum, Param::Decay, d);
        for _ in 0..4800 { k.naechstes_sample(); }
        for akz in [false, true] {
            k.ausloesen(Stimme::Bassdrum, akz);
            for _ in 0..(fs * 1.2) as usize { out.push(k.naechstes_sample()); }
        }
    }
    wav(&format!("{}/808_BD_VA_decay_links_mitte_rechts.wav", ziel), &out);
    // Tone links / Mitte / rechts
    let mut out = Vec::new();
    for t in [0.0, 0.5, 1.0] {
        let mut k = DrumKern::new(fs);
        k.set_param(Stimme::Bassdrum, Param::Ton, t);
        for _ in 0..4800 { k.naechstes_sample(); }
        k.ausloesen(Stimme::Bassdrum, false);
        for _ in 0..(fs * 0.8) as usize { out.push(k.naechstes_sample()); }
    }
    wav(&format!("{}/808_BD_VA_tone_links_mitte_rechts.wav", ziel), &out);
    // Bassdrum-Pattern mit Akzenten, 125 BPM
    let mut k = DrumKern::new(fs);
    let schritt = (fs * 60.0 / 125.0 / 4.0) as usize;
    let mut out = Vec::new();
    for takt in 0..4 {
        for st in 0..16 {
            if st % 4 == 0 { k.ausloesen(Stimme::Bassdrum, st == 0); }
            if takt % 2 == 1 && st == 14 { k.ausloesen(Stimme::Bassdrum, false); }
            if st % 2 == 1 { k.ausloesen(Stimme::HiHatZu, false); }
            if st == 4 || st == 12 { k.ausloesen(Stimme::Clap, false); }
            for _ in 0..schritt { out.push(k.naechstes_sample()); }
        }
    }
    for _ in 0..(fs as usize) { out.push(k.naechstes_sample()); }
    wav(&format!("{}/808_groove_mit_VA_bassdrum.wav", ziel), &out);
}
