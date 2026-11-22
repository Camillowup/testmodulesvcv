//! TR-808 nach Roland Service Notes (1981): Messtabelle S. 14 (Frequenzen,
//! Ausklingzeiten bei Reglern auf 12 Uhr), Schaltung S. 9/13, Beschreibung S. 5/6.
//! Alle Stimmen analog: angestossene Bridged-T-Schwinger, Rauschen,
//! sechs Rechteck-Oszillatoren (gemeinsam fuer Cymbal und Hi-Hats).

use crate::dsp::*;
use crate::gemeinsam::{Clap, ClapConfig};
use crate::{Params, Stimme, ANZAHL_STIMMEN, DECAY, EXTRA, PEGEL, STANDARD, TON, TUNE};

/// Die sechs Schmitt-Trigger-Oszillatoren (Werner et al. 2014; #5/#6 = Cowbell 800/540).
const METALL_808: [f32; 6] = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0];

// ---------------------------------------------------------------- Bassdrum
// Tabelle: 56 Hz, Decay 50 / 300 / 800 ms. Beim Anschlag Resonanz kurz hoeher
// ("halbierte Zeitkonstante", ~4 ms), danach leichter "Seufzer" nach unten.
// Tone = Tiefpass R171+VR5 / C45: ca. 160 Hz .. 7 kHz.
struct Bd {
    fs: f32,
    s: Schwinger,
    basis: f32,
    tau: f32,
    sprung: f32,
    sprung_n: u32,
    klick_n: u32,
    seufzer: Huellkurve,
    tone: OnePole,
    pegel: f32,
    vel: f32,
    drift: f32,
}

impl Bd {
    fn new(fs: f32) -> Self {
        Self {
            fs,
            s: Schwinger::new(fs),
            basis: 56.0,
            tau: 0.13,
            sprung: 2.2,
            sprung_n: 0,
            klick_n: 0,
            seufzer: Huellkurve::new(fs, 0.08),
            tone: OnePole::new(1000.0, fs),
            pegel: 0.8,
            vel: 1.0,
            drift: 1.0,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.basis = drei_punkt(46.0, 56.0, 68.0, p[TUNE]);
        self.tau = t10_zu_tau(drei_punkt(0.05, 0.3, 0.8, p[DECAY]));
        self.tone.set(drei_punkt(160.0, 1000.0, 7000.0, p[TON]), self.fs);
        self.sprung = lin_map(1.4, 3.0, p[EXTRA]);
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        self.drift = drift;
        self.sprung_n = (0.004 * self.fs) as u32;
        self.klick_n = (0.001 * self.fs) as u32;
        self.seufzer.ausloesen(1.0);
        self.s.set(self.basis * drift * self.sprung, self.tau);
        self.s.anstossen(1.0);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        let sprung = if self.sprung_n > 0 {
            self.sprung_n -= 1;
            self.sprung
        } else {
            1.0
        };
        let f = self.basis * self.drift * sprung * (1.0 + 0.1 * self.seufzer.process());
        self.s.set(f, self.tau);
        let body = self.s.process();
        let klick = if self.klick_n > 0 {
            self.klick_n -= 1;
            0.6
        } else {
            0.0
        };
        ausgang(self.tone.lp(body + klick), self.vel, self.pegel)
    }
}

// ---------------------------------------------------------------- Snare
// Zwei Bridged-T: Tabelle 238 + 476 Hz (Decay gesamt 60 ms). Spaetere
// Bauteil-Aenderung (C58/C61) senkt auf ca. 175 + 335 Hz. Snappy-Rauschen
// laeuft durch einen Hochpass R201/R202/C66/C67 = ca. 2,7 kHz.
#[derive(Clone, Copy)]
struct SnareWerte {
    f1: f32,
    f2: f32,
    tau1: f32,
    tau2: f32,
}
const SNARE_TABELLE: SnareWerte = SnareWerte { f1: 238.0, f2: 476.0, tau1: 0.022, tau2: 0.007 };
const SNARE_SPAET: SnareWerte = SnareWerte { f1: 175.0, f2: 335.0, tau1: 0.030, tau2: 0.0094 };

struct Sd {
    s1: Schwinger,
    s2: Schwinger,
    werte: SnareWerte,
    n_env: Huellkurve,
    n_hp: Svf,
    rng: Rauschen,
    tune: f32,
    ton: f32,
    snappy: f32,
    pegel: f32,
    vel: f32,
}

impl Sd {
    fn new(fs: f32) -> Self {
        Self {
            s1: Schwinger::new(fs),
            s2: Schwinger::new(fs),
            werte: SNARE_TABELLE,
            n_env: Huellkurve::new(fs, 0.026),
            n_hp: Svf::new(2750.0, 0.7, fs),
            rng: Rauschen::new(0x808_5d),
            tune: 1.0,
            ton: 0.5,
            snappy: 0.5,
            pegel: 0.8,
            vel: 1.0,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.tune = exp_map(0.85, 1.18, p[TUNE]);
        self.n_env.set_tau(drei_punkt(0.013, 0.026, 0.06, p[DECAY]));
        self.ton = p[TON];
        self.snappy = p[EXTRA];
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        let w = self.werte;
        self.s1.set(w.f1 * self.tune * drift, w.tau1);
        self.s2.set(w.f2 * self.tune * drift, w.tau2);
        self.s1.anstossen(1.0);
        self.s2.anstossen(1.0);
        self.n_env.ausloesen(1.0);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        let body = self.s1.process() * (1.2 - 0.8 * self.ton) + self.s2.process() * (0.4 + 0.8 * self.ton);
        let n = self.n_hp.process(self.rng.bipolar()).hoch;
        let noise = n * self.n_env.process() * self.snappy * 2.0;
        ausgang(body * 0.8 + noise, self.vel, self.pegel)
    }
}

// ---------------------------------------------------------------- Toms / Congas
// Tabelle (Hz bei Tuning links/Mitte/rechts, Decay ms):
// LT 80/90/100, 200 · MT 120/135/160, 130 · HT 165/185/220, 100
// LC 165/185/220, 180 · MC 250/280/310, 100 · HC 370/400/455, 80
// Bei grosser Amplitude leiten D80/D81 -> Frequenz hoeher, sinkt beim Ausklingen.
// Nur Low Tom bekommt rosa Rauschen mit etwas laengerem Ausklingen.
#[derive(Clone, Copy)]
struct TomWerte {
    f: (f32, f32, f32),
    t10: f32,
    rauschen: bool,
}

struct Tom {
    s: Schwinger,
    tom: TomWerte,
    conga: TomWerte,
    ist_conga: bool,
    n_env: Huellkurve,
    pink: OnePole,
    rng: Rauschen,
    freq: f32,
    tau: f32,
    tiefe: f32,
    n_menge: f32,
    amp: f32,
    amp_k: f32,
    fs: f32,
    pegel: f32,
    vel: f32,
    drift: f32,
}

impl Tom {
    fn new(fs: f32, tom: TomWerte, conga: TomWerte, seed: u32) -> Self {
        Self {
            s: Schwinger::new(fs),
            tom,
            conga,
            ist_conga: false,
            n_env: Huellkurve::new(fs, 0.1),
            pink: OnePole::new(900.0, fs),
            rng: Rauschen::new(seed),
            freq: tom.f.1,
            tau: 0.1,
            tiefe: 0.15,
            n_menge: 0.2,
            amp: 0.0,
            amp_k: 0.0,
            fs,
            pegel: 0.8,
            vel: 1.0,
            drift: 1.0,
        }
    }
    fn werte(&self) -> TomWerte {
        if self.ist_conga {
            self.conga
        } else {
            self.tom
        }
    }
    fn update(&mut self, p: &Params) {
        let w = self.werte();
        self.pegel = p[PEGEL];
        self.freq = drei_punkt(w.f.0, w.f.1, w.f.2, p[TUNE]);
        self.tau = t10_zu_tau(w.t10) * exp_map(0.5, 2.0, p[DECAY]);
        self.amp_k = (-1.0 / (self.tau * self.fs)).exp();
        self.n_env.set_tau(self.tau * 1.3);
        self.tiefe = lin_map(0.0, 0.3, p[EXTRA]);
        self.n_menge = if w.rauschen { 0.5 * p[TON] } else { 0.0 };
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        self.drift = drift;
        // Diodenwirkung: lautere Schlaege starten hoeher.
        self.amp = vel.min(2.0) * 0.5;
        self.s.set(self.freq * drift * (1.0 + self.tiefe * self.amp), self.tau);
        self.s.anstossen(1.0);
        self.n_env.ausloesen(1.0);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.amp > 1.0e-3 {
            self.amp *= self.amp_k * self.amp_k;
            self.s.set(self.freq * self.drift * (1.0 + self.tiefe * self.amp), self.tau);
        }
        let body = self.s.process();
        let noise = if self.n_menge > 0.0 {
            self.pink.lp(self.rng.bipolar()) * self.n_env.process() * self.n_menge * 2.5
        } else {
            0.0
        };
        ausgang(body + noise, self.vel, self.pegel)
    }
}

// ---------------------------------------------------------------- Rimshot / Claves
// Tabelle: RS 455 + 1667 Hz, 10 ms · Claves 2500 Hz, 25 ms.
// RS-VCA ist so gebaut, dass er viele Obertoene erzeugt (Saettigung).
struct Rs {
    s1: Schwinger,
    s2: Schwinger,
    hp: OnePole,
    claves: bool,
    tune: f32,
    dk: f32,
    ton: f32,
    pegel: f32,
    vel: f32,
}

impl Rs {
    fn new(fs: f32) -> Self {
        Self {
            s1: Schwinger::new(fs),
            s2: Schwinger::new(fs),
            hp: OnePole::new(250.0, fs),
            claves: false,
            tune: 1.0,
            dk: 1.0,
            ton: 0.5,
            pegel: 0.8,
            vel: 1.0,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.tune = exp_map(0.85, 1.18, p[TUNE]);
        self.dk = exp_map(0.5, 2.0, p[DECAY]);
        self.ton = p[TON];
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        let m = self.tune * drift;
        if self.claves {
            self.s1.set(2500.0 * m, t10_zu_tau(0.025) * self.dk);
            self.s1.anstossen(1.0);
        } else {
            self.s1.set(455.0 * m, t10_zu_tau(0.010) * self.dk);
            self.s2.set(1667.0 * m, t10_zu_tau(0.009) * self.dk);
            self.s1.anstossen(1.0);
            self.s2.anstossen(1.0);
        }
    }
    #[inline]
    fn process(&mut self) -> f32 {
        let a = self.s1.process();
        let b = self.s2.process();
        if self.claves {
            ausgang(a * 0.9, self.vel, self.pegel)
        } else {
            let x = a * (1.0 - 0.5 * self.ton) * 0.8 + b * (0.3 + 0.7 * self.ton);
            ausgang(soft_clip(self.hp.hp(x) * 2.5), self.vel, self.pegel)
        }
    }
}

// ---------------------------------------------------------------- Maracas
// Tabelle: Decay 25..35 ms. Weisses Rauschen, gegatet, Hochpass (Q68).
struct Maracas {
    hp: Svf,
    env: Huellkurve,
    rng: Rauschen,
    pegel: f32,
}

impl Maracas {
    fn new(fs: f32) -> Self {
        Self {
            hp: Svf::new(5000.0, 0.7, fs),
            env: Huellkurve::new(fs, 0.013),
            rng: Rauschen::new(0x3a3a),
            pegel: 0.8,
        }
    }
    fn update(&mut self, p: &Params, fs: f32) {
        self.pegel = p[PEGEL];
        self.env.set_tau(t10_zu_tau(drei_punkt(0.02, 0.03, 0.06, p[DECAY])));
        self.hp.set(drei_punkt(3000.0, 5000.0, 8000.0, p[TON]), 0.7, fs);
    }
    fn ausloesen(&mut self, vel: f32) {
        self.env.ausloesen(vel);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.env.wert() == 0.0 {
            return 0.0;
        }
        let x = self.hp.process(self.rng.bipolar()).hoch;
        ausgang(x * self.env.process() * 1.5, 1.0, self.pegel)
    }
}

// ---------------------------------------------------------------- Hi-Hats
// Tabelle: OH 90 / 450 / 600 ms, CH 50 ms. Quelle = hohes Band (~7,1 kHz)
// der gemeinsamen sechs Oszillatoren, Swing-VCA (saettigt), Hochpass.
struct Hat {
    fs: f32,
    offen: bool,
    bp: Svf,
    hp: Svf,
    env: Huellkurve,
    tau: f32,
    pegel: f32,
}

impl Hat {
    fn new(fs: f32, offen: bool) -> Self {
        Self {
            fs,
            offen,
            bp: Svf::new(7100.0, 1.0, fs),
            hp: Svf::new(6000.0, 0.7, fs),
            env: Huellkurve::new(fs, 0.02),
            tau: 0.02,
            pegel: 0.8,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.tau = if self.offen {
            t10_zu_tau(drei_punkt(0.09, 0.45, 0.6, p[DECAY]))
        } else {
            t10_zu_tau(drei_punkt(0.025, 0.05, 0.1, p[DECAY]))
        };
        self.env.set_tau(self.tau);
        self.hp.set(drei_punkt(4000.0, 6000.0, 9000.0, p[TON]), 0.7, self.fs);
    }
    fn ausloesen(&mut self, vel: f32) {
        self.env.set_tau(self.tau);
        self.env.ausloesen(vel);
    }
    /// CH beendet das Ausklingen der OH (Q23 im Original).
    fn abwuergen(&mut self) {
        self.env.set_tau(0.003);
    }
    #[inline]
    fn process(&mut self, m: f32) -> f32 {
        let band = self.bp.process(m).band;
        if self.env.wert() == 0.0 {
            return 0.0;
        }
        let swing = soft_clip(band * 3.0);
        let y = self.hp.process(swing).hoch;
        ausgang(y * self.env.process() * 2.0, 1.0, self.pegel)
    }
}

// ---------------------------------------------------------------- Cymbal
// Tabelle: 350 / 800 / 1200 ms. Zwei Filter trennen hoch (~7,1 kHz) und tief
// (~3,4 kHz); das hohe Band wird nochmals geteilt: hoechster Anteil kurz,
// etwas tieferer Anteil mit regelbarem Decay. Tone mischt die drei Baender.
struct Cym {
    fs: f32,
    bp_hi: Svf,
    bp_lo: Svf,
    hp_a: Svf,
    hp_b: Svf,
    hp_c: Svf,
    env_a: Huellkurve,
    env_b: Huellkurve,
    env_c: Huellkurve,
    ton: f32,
    pegel: f32,
}

impl Cym {
    fn new(fs: f32) -> Self {
        Self {
            fs,
            bp_hi: Svf::new(7100.0, 1.0, fs),
            bp_lo: Svf::new(3440.0, 1.0, fs),
            hp_a: Svf::new(10500.0, 0.9, fs),
            hp_b: Svf::new(5000.0, 0.7, fs),
            hp_c: Svf::new(2000.0, 0.7, fs),
            env_a: Huellkurve::new(fs, 0.04),
            env_b: Huellkurve::new(fs, 0.35),
            env_c: Huellkurve::new(fs, 0.2),
            ton: 0.5,
            pegel: 0.8,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        let tau = t10_zu_tau(drei_punkt(0.35, 0.8, 1.2, p[DECAY]));
        self.env_a.set_tau(0.04);
        self.env_b.set_tau(tau);
        self.env_c.set_tau(tau * 0.55);
        self.ton = p[TON];
        let _ = self.fs;
    }
    fn ausloesen(&mut self, vel: f32) {
        self.env_a.ausloesen(vel);
        self.env_b.ausloesen(vel);
        self.env_c.ausloesen(vel);
    }
    #[inline]
    fn process(&mut self, m: f32) -> f32 {
        let hi = self.bp_hi.process(m).band;
        let lo = self.bp_lo.process(m).band;
        if self.env_b.wert() == 0.0 {
            return 0.0;
        }
        let a = self.hp_a.process(soft_clip(hi * 3.0)).hoch * self.env_a.process();
        let b = self.hp_b.process(soft_clip(hi * 3.0)).hoch * self.env_b.process();
        let c = self.hp_c.process(soft_clip(lo * 3.0)).hoch * self.env_c.process();
        let y = a * 0.6 + b * 0.7 + c * (1.0 - 0.85 * self.ton);
        ausgang(y * 1.6, 1.0, self.pegel)
    }
}

// ---------------------------------------------------------------- Cowbell
// Tabelle: 540 + 800 Hz, 50 ms. Huellkurve mit steilem Abfall am Anfang.
struct Cowbell {
    fs: f32,
    o1: Rechteck,
    o2: Rechteck,
    bp: Svf,
    env_a: Huellkurve,
    env_b: Huellkurve,
    pegel: f32,
}

impl Cowbell {
    fn new(fs: f32) -> Self {
        Self {
            fs,
            o1: Rechteck::default(),
            o2: Rechteck::default(),
            bp: Svf::new(1200.0, 1.3, fs),
            env_a: Huellkurve::new(fs, 0.004),
            env_b: Huellkurve::new(fs, 0.036),
            pegel: 0.8,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        let m = exp_map(0.9, 1.11, p[TUNE]);
        self.o1.set_freq(540.0 * m, self.fs);
        self.o2.set_freq(800.0 * m, self.fs);
        self.bp.set(drei_punkt(700.0, 1200.0, 2600.0, p[TON]), 1.3, self.fs);
        self.env_b.set_tau(drei_punkt(0.018, 0.036, 0.07, p[DECAY]));
    }
    fn ausloesen(&mut self, vel: f32) {
        self.env_a.ausloesen(0.6 * vel);
        self.env_b.ausloesen(0.4 * vel);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.env_b.wert() == 0.0 {
            return 0.0;
        }
        let x = (self.o1.process() + self.o2.process()) * 0.5;
        let y = self.bp.process(x).band;
        let env = self.env_a.process() + self.env_b.process();
        ausgang(y * env * 2.2, 1.0, self.pegel)
    }
}

// ---------------------------------------------------------------- Maschine
pub struct Engine808 {
    fs: f32,
    rng: Rauschen,
    params: [Params; ANZAHL_STIMMEN],
    metall: Metall,
    bd: Bd,
    sd: Sd,
    toms: [Tom; 3],
    rs: Rs,
    cp: Clap,
    ma: Maracas,
    maracas: bool,
    ch: Hat,
    oh: Hat,
    cy: Cym,
    cb: Cowbell,
}

impl Engine808 {
    pub fn new(fs: f32) -> Self {
        let lt = TomWerte { f: (80.0, 90.0, 100.0), t10: 0.2, rauschen: true };
        let mt = TomWerte { f: (120.0, 135.0, 160.0), t10: 0.13, rauschen: false };
        let ht = TomWerte { f: (165.0, 185.0, 220.0), t10: 0.1, rauschen: false };
        let lc = TomWerte { f: (165.0, 185.0, 220.0), t10: 0.18, rauschen: false };
        let mc = TomWerte { f: (250.0, 280.0, 310.0), t10: 0.1, rauschen: false };
        let hc = TomWerte { f: (370.0, 400.0, 455.0), t10: 0.08, rauschen: false };
        let mut e = Self {
            fs,
            rng: Rauschen::new(0x808),
            params: [STANDARD; ANZAHL_STIMMEN],
            metall: Metall::new(METALL_808, fs, 0x6e7a1),
            bd: Bd::new(fs),
            sd: Sd::new(fs),
            toms: [
                Tom::new(fs, lt, lc, 0x71),
                Tom::new(fs, mt, mc, 0x72),
                Tom::new(fs, ht, hc, 0x73),
            ],
            rs: Rs::new(fs),
            cp: Clap::new(
                fs,
                ClapConfig {
                    bursts: 3,
                    f: (700.0, 1000.0, 1500.0),
                    q: 1.3,
                    abstand: (0.007, 0.010, 0.014),
                    hall: (0.02, 0.043, 0.09),
                    hall_pegel: 0.45,
                    seed: 0xc1a9,
                },
            ),
            ma: Maracas::new(fs),
            maracas: false,
            ch: Hat::new(fs, false),
            oh: Hat::new(fs, true),
            cy: Cym::new(fs),
            cb: Cowbell::new(fs),
        };
        for s in Stimme::ALLE {
            e.update(s);
        }
        e
    }

    pub fn param(&self, s: Stimme, p: usize) -> f32 {
        self.params[s as usize][p]
    }

    pub fn set_param(&mut self, s: Stimme, p: usize, wert: f32) {
        self.params[s as usize][p] = wert.clamp(0.0, 1.0);
        self.update(s);
    }

    pub fn set_snare_spaet(&mut self, spaet: bool) {
        self.sd.werte = if spaet { SNARE_SPAET } else { SNARE_TABELLE };
    }

    pub fn set_congas(&mut self, an: bool) {
        for t in self.toms.iter_mut() {
            t.ist_conga = an;
        }
        self.update(Stimme::TomTief);
        self.update(Stimme::TomMittel);
        self.update(Stimme::TomHoch);
    }

    pub fn set_claves(&mut self, an: bool) {
        self.rs.claves = an;
    }

    pub fn set_maracas(&mut self, an: bool) {
        self.maracas = an;
    }

    fn update(&mut self, s: Stimme) {
        let p = self.params[s as usize];
        match s {
            Stimme::Bassdrum => self.bd.update(&p),
            Stimme::Snare => self.sd.update(&p),
            Stimme::TomTief => self.toms[0].update(&p),
            Stimme::TomMittel => self.toms[1].update(&p),
            Stimme::TomHoch => self.toms[2].update(&p),
            Stimme::Rimshot => self.rs.update(&p),
            Stimme::Clap => {
                self.cp.update(&p);
                self.ma.update(&p, self.fs);
            }
            Stimme::HiHatZu => self.ch.update(&p),
            Stimme::HiHatOffen => self.oh.update(&p),
            Stimme::Becken => self.cy.update(&p),
            Stimme::Extra => self.cb.update(&p),
        }
    }

    /// `akzent`: Akzentmenge 0..1 oder 0 ohne Akzent. Laut Tabelle hebt der
    /// Akzent Trommeln etwa auf das Dreifache; bei CB/CY/OH/CH ist der
    /// Bereich eingeengt (7..14 V statt 4..14 V).
    pub fn ausloesen(&mut self, s: Stimme, akzent: f32) {
        let drift = 1.0 + 0.004 * self.rng.bipolar();
        let streu = 1.0 + 0.03 * self.rng.bipolar();
        let v = (1.0 + 2.0 * akzent) * streu;
        let v_metall = (1.0 + akzent) * streu;
        match s {
            Stimme::Bassdrum => self.bd.ausloesen(v, drift),
            Stimme::Snare => self.sd.ausloesen(v, drift),
            Stimme::TomTief => self.toms[0].ausloesen(v, drift),
            Stimme::TomMittel => self.toms[1].ausloesen(v, drift),
            Stimme::TomHoch => self.toms[2].ausloesen(v, drift),
            Stimme::Rimshot => self.rs.ausloesen(v, drift),
            Stimme::Clap => {
                if self.maracas {
                    self.ma.ausloesen(v)
                } else {
                    self.cp.ausloesen(v)
                }
            }
            Stimme::HiHatZu => {
                self.oh.abwuergen();
                self.ch.ausloesen(v_metall);
            }
            Stimme::HiHatOffen => self.oh.ausloesen(v_metall),
            Stimme::Becken => self.cy.ausloesen(v_metall),
            Stimme::Extra => self.cb.ausloesen(v_metall),
        }
    }

    #[inline]
    pub fn process(&mut self, aus: &mut [f32; ANZAHL_STIMMEN]) {
        let m = self.metall.process();
        aus[0] += self.bd.process();
        aus[1] += self.sd.process();
        aus[2] += self.toms[0].process();
        aus[3] += self.toms[1].process();
        aus[4] += self.toms[2].process();
        aus[5] += self.rs.process();
        aus[6] += self.cp.process() + self.ma.process();
        aus[7] += self.ch.process(m);
        aus[8] += self.oh.process(m);
        aus[9] += self.cy.process(m);
        aus[10] += self.cb.process();
    }
}
