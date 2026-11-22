//! # drumkern
//!
//! Synthetischer Klangkern fuer die Stimmen der Roland TR-808 und TR-909,
//! ohne Samples. Ein Umschalter waehlt die Maschine; beide laufen intern
//! weiter, damit ausklingende Schlaege beim Umschalten nicht abreissen.
//!
//! ```
//! use drumkern::{DrumKern, Modell, Stimme, Param};
//! let mut kern = DrumKern::new(48_000.0);
//! kern.set_modell(Modell::Tr909);
//! kern.set_param(Stimme::Bassdrum, Param::Decay, 0.7);
//! kern.ausloesen(Stimme::Bassdrum, true); // mit Akzent
//! let sample = kern.naechstes_sample();
//! assert!(sample.is_finite());
//! ```
//!
//! Alle Parameter sind 0..1 und werden pro Stimme sinnvoll abgebildet.
//! Jedes Modell merkt sich seine eigenen Einstellungen.

mod dsp;
mod gemeinsam;
mod tr808;
mod tr909;

use tr808::Engine808;
use tr909::Engine909;

pub const ANZAHL_STIMMEN: usize = 11;

pub(crate) type Params = [f32; 5];
pub(crate) const PEGEL: usize = 0;
pub(crate) const TUNE: usize = 1;
pub(crate) const DECAY: usize = 2;
pub(crate) const TON: usize = 3;
pub(crate) const EXTRA: usize = 4;
pub(crate) const STANDARD: Params = [0.8, 0.5, 0.5, 0.5, 0.5];

/// Welche Maschine klingt.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Modell {
    Tr808,
    Tr909,
}

impl Modell {
    pub fn name(self) -> &'static str {
        match self {
            Modell::Tr808 => "808",
            Modell::Tr909 => "909",
        }
    }
}

/// Die elf Stimmen-Plaetze. `Becken` ist bei der 808 das Cymbal, bei der
/// 909 das Crash. `Extra` ist bei der 808 die Cowbell, bei der 909 das Ride.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stimme {
    Bassdrum = 0,
    Snare,
    TomTief,
    TomMittel,
    TomHoch,
    Rimshot,
    Clap,
    HiHatZu,
    HiHatOffen,
    Becken,
    Extra,
}

impl Stimme {
    pub const ALLE: [Stimme; ANZAHL_STIMMEN] = [
        Stimme::Bassdrum,
        Stimme::Snare,
        Stimme::TomTief,
        Stimme::TomMittel,
        Stimme::TomHoch,
        Stimme::Rimshot,
        Stimme::Clap,
        Stimme::HiHatZu,
        Stimme::HiHatOffen,
        Stimme::Becken,
        Stimme::Extra,
    ];

    /// Kurzname wie auf dem Original-Panel.
    pub fn kurzname(self, m: Modell) -> &'static str {
        match (self, m) {
            (Stimme::Bassdrum, _) => "BD",
            (Stimme::Snare, _) => "SD",
            (Stimme::TomTief, _) => "LT",
            (Stimme::TomMittel, _) => "MT",
            (Stimme::TomHoch, _) => "HT",
            (Stimme::Rimshot, _) => "RS",
            (Stimme::Clap, _) => "CP",
            (Stimme::HiHatZu, _) => "CH",
            (Stimme::HiHatOffen, _) => "OH",
            (Stimme::Becken, Modell::Tr808) => "CY",
            (Stimme::Becken, Modell::Tr909) => "CRASH",
            (Stimme::Extra, Modell::Tr808) => "CB",
            (Stimme::Extra, Modell::Tr909) => "RIDE",
        }
    }
}

/// Die fuenf Regler pro Stimme.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Param {
    Pegel = 0,
    Tune,
    Decay,
    Ton,
    Extra,
}

impl Param {
    /// Was der Regler bei dieser Stimme tut (None = ohne Wirkung).
    /// Praktisch fuer Panel-Beschriftungen.
    pub fn bedeutung(self, m: Modell, s: Stimme) -> Option<&'static str> {
        use Stimme::*;
        match self {
            Param::Pegel => Some("Level"),
            Param::Tune => Some("Tune"),
            Param::Decay => Some("Decay"),
            Param::Ton => match (m, s) {
                (Modell::Tr909, Bassdrum) => Some("Drive"),
                (_, Snare) => Some("Tone"),
                (_, TomTief | TomMittel | TomHoch) => Some("Rauschen"),
                (_, Clap) => Some("Filter"),
                _ => Some("Tone"),
            },
            Param::Extra => match (m, s) {
                (Modell::Tr808, Bassdrum) => Some("Pitch-Drop"),
                (Modell::Tr909, Bassdrum) => Some("Attack"),
                (_, Snare) => Some("Snappy"),
                (_, TomTief | TomMittel | TomHoch) => Some("Pitch-Drop"),
                (_, Clap) => Some("Abstand"),
                (Modell::Tr909, HiHatZu | HiHatOffen | Becken | Extra) => Some("6-Bit"),
                _ => None,
            },
        }
    }
}

/// Der Klangkern mit Umschalter 808/909.
pub struct DrumKern {
    modell: Modell,
    e808: Engine808,
    e909: Engine909,
    accent: f32,
    master: f32,
    einzeln: [f32; ANZAHL_STIMMEN],
}

impl DrumKern {
    pub fn new(samplerate: f32) -> Self {
        Self {
            modell: Modell::Tr808,
            e808: Engine808::new(samplerate),
            e909: Engine909::new(samplerate),
            accent: 0.5,
            master: 0.7,
            einzeln: [0.0; ANZAHL_STIMMEN],
        }
    }

    pub fn modell(&self) -> Modell {
        self.modell
    }

    /// Der Umschalter. Wirkt ab dem naechsten Schlag; was gerade klingt,
    /// klingt natuerlich aus.
    pub fn set_modell(&mut self, m: Modell) {
        self.modell = m;
    }

    pub fn umschalten(&mut self) {
        self.modell = match self.modell {
            Modell::Tr808 => Modell::Tr909,
            Modell::Tr909 => Modell::Tr808,
        };
    }

    /// Wie stark ein Akzent-Schlag lauter/kraeftiger wird (0..1).
    pub fn set_accent(&mut self, menge: f32) {
        self.accent = menge.clamp(0.0, 1.0);
    }

    pub fn set_master(&mut self, pegel: f32) {
        self.master = pegel.clamp(0.0, 1.0);
    }

    /// Schlag ausloesen (im aktuell gewaehlten Modell).
    pub fn ausloesen(&mut self, s: Stimme, akzent: bool) {
        let vel = if akzent { 1.0 + self.accent } else { 1.0 };
        match self.modell {
            Modell::Tr808 => self.e808.ausloesen(s, vel),
            Modell::Tr909 => self.e909.ausloesen(s, vel),
        }
    }

    /// Regler im aktuellen Modell setzen (Wert 0..1).
    pub fn set_param(&mut self, s: Stimme, p: Param, wert: f32) {
        let m = self.modell;
        self.set_param_fuer(m, s, p, wert);
    }

    /// Regler in einem bestimmten Modell setzen.
    pub fn set_param_fuer(&mut self, m: Modell, s: Stimme, p: Param, wert: f32) {
        match m {
            Modell::Tr808 => self.e808.set_param(s, p as usize, wert),
            Modell::Tr909 => self.e909.set_param(s, p as usize, wert),
        }
    }

    pub fn param(&self, m: Modell, s: Stimme, p: Param) -> f32 {
        match m {
            Modell::Tr808 => self.e808.param(s, p as usize),
            Modell::Tr909 => self.e909.param(s, p as usize),
        }
    }

    /// Ein Sample berechnen; gibt die Summe zurueck. Einzelausgaenge danach
    /// ueber `einzelausgaenge()`.
    #[inline]
    pub fn naechstes_sample(&mut self) -> f32 {
        self.einzeln = [0.0; ANZAHL_STIMMEN];
        self.e808.process(&mut self.einzeln);
        self.e909.process(&mut self.einzeln);
        let summe: f32 = self.einzeln.iter().sum();
        dsp::soft_clip(summe * self.master)
    }

    /// Die elf Einzelausgaenge des letzten Samples (z. B. fuer VCV-Ausgaenge).
    pub fn einzelausgaenge(&self) -> &[f32; ANZAHL_STIMMEN] {
        &self.einzeln
    }

    /// Einen ganzen Block berechnen (Summe).
    pub fn render(&mut self, aus: &mut [f32]) {
        for s in aus.iter_mut() {
            *s = self.naechstes_sample();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn spitze_und_ende(m: Modell, s: Stimme, sekunden: f32) -> (f32, f32, bool) {
        let fs = 48_000.0;
        let mut k = DrumKern::new(fs);
        k.set_modell(m);
        k.ausloesen(s, true);
        let n = (fs * sekunden) as usize;
        let mut spitze = 0.0f32;
        let mut ende = 0.0f32;
        let mut endlich = true;
        for i in 0..n {
            k.naechstes_sample();
            let v = k.einzelausgaenge()[s as usize];
            endlich &= v.is_finite();
            spitze = spitze.max(v.abs());
            if i > n - 4800 {
                ende = ende.max(v.abs());
            }
        }
        (spitze, ende, endlich)
    }

    #[test]
    fn alle_stimmen_klingen_und_klingen_aus() {
        for m in [Modell::Tr808, Modell::Tr909] {
            for s in Stimme::ALLE {
                let (spitze, ende, endlich) = spitze_und_ende(m, s, 12.0);
                assert!(endlich, "{:?} {:?} nicht endlich", m, s);
                assert!(spitze > 0.05, "{:?} {:?} zu leise: {}", m, s, spitze);
                assert!(spitze < 1.6, "{:?} {:?} zu laut: {}", m, s, spitze);
                assert!(ende < 0.001, "{:?} {:?} klingt nicht aus: {}", m, s, ende);
            }
        }
    }

    #[test]
    fn extremwerte_bleiben_stabil() {
        for m in [Modell::Tr808, Modell::Tr909] {
            for wert in [0.0, 1.0] {
                let mut k = DrumKern::new(44_100.0);
                k.set_modell(m);
                for s in Stimme::ALLE {
                    for p in [Param::Pegel, Param::Tune, Param::Decay, Param::Ton, Param::Extra] {
                        k.set_param(s, p, wert);
                    }
                    k.ausloesen(s, true);
                }
                for _ in 0..44_100 {
                    let v = k.naechstes_sample();
                    assert!(v.is_finite() && v.abs() <= 1.0);
                }
            }
        }
    }

    #[test]
    fn umschalten_laesst_ausklingen() {
        let mut k = DrumKern::new(48_000.0);
        k.ausloesen(Stimme::Bassdrum, false);
        for _ in 0..100 {
            k.naechstes_sample();
        }
        k.umschalten();
        assert_eq!(k.modell(), Modell::Tr909);
        let mut energie = 0.0;
        for _ in 0..2000 {
            energie += k.naechstes_sample().abs();
        }
        assert!(energie > 1.0, "808-Bassdrum soll nach dem Umschalten weiterklingen");
    }

    #[test]
    fn modelle_merken_eigene_einstellungen() {
        let mut k = DrumKern::new(48_000.0);
        k.set_param(Stimme::Snare, Param::Tune, 0.9);
        k.umschalten();
        k.set_param(Stimme::Snare, Param::Tune, 0.1);
        assert!((k.param(Modell::Tr808, Stimme::Snare, Param::Tune) - 0.9).abs() < 1e-6);
        assert!((k.param(Modell::Tr909, Stimme::Snare, Param::Tune) - 0.1).abs() < 1e-6);
    }
}
