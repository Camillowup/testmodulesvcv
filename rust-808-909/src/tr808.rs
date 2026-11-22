//! TR-808: alle Stimmen analog nachgebaut (angestossene Resonatoren,
//! Rauschen, sechs Rechteck-Oszillatoren fuer die Becken).

use crate::dsp::*;
use crate::gemeinsam::Clap;
use crate::{Params, Stimme, ANZAHL_STIMMEN, DECAY, EXTRA, PEGEL, STANDARD, TON, TUNE};

/// Frequenzen der sechs Rechteck-Oszillatoren der echten 808.
const METALL_808: [f32; 6] = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0];

// ---------------------------------------------------------------- Bassdrum
struct Bd {
    fs: f32,
    res: Resonator,
    p_env: Huellkurve,
    k_env: Huellkurve,
    k_lp: OnePole,
    basis: f32,
    tiefe: f32,
    klick: f32,
    pegel: f32,
    vel: f32,
    drift: f32,
}

impl Bd {
    fn new(fs: f32) -> Self {
        Self {
            fs,
            res: Resonator::new(fs),
            p_env: Huellkurve::new(fs, 0.008),
            k_env: Huellkurve::new(fs, 0.001),
            k_lp: OnePole::new(1000.0, fs),
            basis: 55.0,
            tiefe: 0.2,
            klick: 0.4,
            pegel: 0.8,
            vel: 1.0,
            drift: 1.0,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.basis = exp_map(45.0, 68.0, p[TUNE]);
        self.res.set_tau(exp_map(0.05, 0.7, p[DECAY]));
        self.res.set_freq(self.basis * self.drift);
        self.k_lp.set(exp_map(300.0, 6000.0, p[TON]), self.fs);
        self.klick = 0.15 + 0.6 * p[TON];
        self.tiefe = 0.5 * p[EXTRA];
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        self.drift = drift;
        self.res.set_freq(self.basis * drift * (1.0 + self.tiefe));
        self.res.anstossen(1.0);
        self.p_env.ausloesen(1.0);
        self.k_env.ausloesen(1.0);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.p_env.wert() > 1.0e-4 {
            let pe = self.p_env.process();
            self.res.set_freq(self.basis * self.drift * (1.0 + self.tiefe * pe));
        }
        let b = self.res.process();
        let k = self.k_lp.lp(self.k_env.process()) * self.klick;
        soft_clip((b + k) * 1.2 * self.vel) * self.pegel
    }
}

// ---------------------------------------------------------------- Snare
struct Sd {
    fs: f32,
    r1: Resonator,
    r2: Resonator,
    n_env: Huellkurve,
    n_hp: Svf,
    n_lp: OnePole,
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
            fs,
            r1: Resonator::new(fs),
            r2: Resonator::new(fs),
            n_env: Huellkurve::new(fs, 0.1),
            n_hp: Svf::new(1500.0, 0.7, fs),
            n_lp: OnePole::new(9000.0, fs),
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
        self.tune = exp_map(0.8, 1.25, p[TUNE]);
        let koerper = lin_map(0.7, 1.4, p[DECAY]);
        self.r1.set_tau(0.035 * koerper);
        self.r2.set_tau(0.022 * koerper);
        self.r1.set_freq(180.0 * self.tune);
        self.r2.set_freq(330.0 * self.tune);
        self.n_env.set_tau(exp_map(0.04, 0.25, p[DECAY]));
        self.ton = p[TON];
        self.snappy = p[EXTRA];
        let _ = self.fs;
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        self.r1.set_freq(180.0 * self.tune * drift);
        self.r2.set_freq(330.0 * self.tune * drift);
        self.r1.anstossen(1.0);
        self.r2.anstossen(1.0);
        self.n_env.ausloesen(1.0);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        let body = self.r1.process() * (1.0 - 0.6 * self.ton) + self.r2.process() * (0.4 + 0.6 * self.ton);
        let n = self.n_hp.process(self.rng.bipolar()).hoch;
        let noise = self.n_lp.lp(n) * self.n_env.process() * self.snappy;
        soft_clip((body * 0.9 + noise * 1.1) * self.vel) * self.pegel
    }
}

// ---------------------------------------------------------------- Toms
struct Tom {
    slot: f32,
    res: Resonator,
    p_env: Huellkurve,
    n_env: Huellkurve,
    n_lp: OnePole,
    rng: Rauschen,
    freq: f32,
    tiefe: f32,
    n_menge: f32,
    pegel: f32,
    vel: f32,
    drift: f32,
}

impl Tom {
    fn new(fs: f32, slot: f32, seed: u32) -> Self {
        Self {
            slot,
            res: Resonator::new(fs),
            p_env: Huellkurve::new(fs, 0.04),
            n_env: Huellkurve::new(fs, 0.03),
            n_lp: OnePole::new(1200.0, fs),
            rng: Rauschen::new(seed),
            freq: slot,
            tiefe: 0.1,
            n_menge: 0.15,
            pegel: 0.8,
            vel: 1.0,
            drift: 1.0,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.freq = self.slot * exp_map(0.75, 1.33, p[TUNE]);
        self.res.set_tau(exp_map(0.1, 0.6, p[DECAY]));
        self.res.set_freq(self.freq * self.drift);
        self.tiefe = 0.25 * p[EXTRA];
        self.n_menge = 0.3 * p[TON];
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        self.drift = drift;
        self.res.set_freq(self.freq * drift * (1.0 + self.tiefe));
        self.res.anstossen(1.0);
        self.p_env.ausloesen(1.0);
        self.n_env.ausloesen(1.0);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.p_env.wert() > 1.0e-4 {
            let pe = self.p_env.process();
            self.res.set_freq(self.freq * self.drift * (1.0 + self.tiefe * pe));
        }
        let body = self.res.process();
        let noise = self.n_lp.lp(self.rng.bipolar()) * self.n_env.process() * self.n_menge * 2.0;
        soft_clip((body + noise) * self.vel) * self.pegel
    }
}

// ---------------------------------------------------------------- Rimshot
struct Rs {
    r1: Resonator,
    r2: Resonator,
    hp: OnePole,
    tune: f32,
    ton: f32,
    pegel: f32,
    vel: f32,
}

impl Rs {
    fn new(fs: f32) -> Self {
        Self {
            r1: Resonator::new(fs),
            r2: Resonator::new(fs),
            hp: OnePole::new(250.0, fs),
            tune: 1.0,
            ton: 0.5,
            pegel: 0.8,
            vel: 1.0,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.tune = exp_map(0.8, 1.25, p[TUNE]);
        let d = exp_map(0.5, 2.0, p[DECAY]);
        self.r1.set_tau(0.006 * d);
        self.r2.set_tau(0.005 * d);
        self.r1.set_freq(455.0 * self.tune);
        self.r2.set_freq(1667.0 * self.tune);
        self.ton = p[TON];
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        self.r1.set_freq(455.0 * self.tune * drift);
        self.r2.set_freq(1667.0 * self.tune * drift);
        self.r1.anstossen(1.0);
        self.r2.anstossen(1.0);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        let x = self.r1.process() * (1.0 - 0.5 * self.ton) * 0.8 + self.r2.process() * (0.3 + 0.7 * self.ton);
        soft_clip(self.hp.hp(x) * 2.5 * self.vel) * 0.7 * self.pegel
    }
}

// ---------------------------------------------------------------- Hi-Hats
struct Hat {
    fs: f32,
    offen: bool,
    metall: Metall,
    bp: Svf,
    hp: OnePole,
    env: Huellkurve,
    tau: f32,
    pegel: f32,
}

impl Hat {
    fn new(fs: f32, offen: bool, seed: u32) -> Self {
        Self {
            fs,
            offen,
            metall: Metall::new(METALL_808, fs, seed),
            bp: Svf::new(7100.0, 1.2, fs),
            hp: OnePole::new(6000.0, fs),
            env: Huellkurve::new(fs, 0.02),
            tau: 0.02,
            pegel: 0.8,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.metall.set_tune(exp_map(0.8, 1.25, p[TUNE]));
        self.tau = if self.offen {
            exp_map(0.06, 0.5, p[DECAY])
        } else {
            exp_map(0.008, 0.05, p[DECAY])
        };
        self.env.set_tau(self.tau);
        self.bp.set(exp_map(5000.0, 10000.0, p[TON]), 1.2, self.fs);
    }
    fn ausloesen(&mut self, vel: f32) {
        self.env.set_tau(self.tau);
        self.env.ausloesen(vel);
    }
    /// Die geschlossene Hi-Hat wuergt die offene ab (wie im Original).
    fn abwuergen(&mut self) {
        self.env.set_tau(0.004);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.env.wert() == 0.0 {
            return 0.0;
        }
        let m = self.metall.process();
        let y = self.hp.hp(self.bp.process(m).band);
        soft_clip(y * self.env.process() * 4.0) * self.pegel
    }
}

// ---------------------------------------------------------------- Cymbal
struct Cym {
    metall: Metall,
    bp_lo: Svf,
    bp_hi: Svf,
    hp: OnePole,
    env_lo: Huellkurve,
    env_hi: Huellkurve,
    ton: f32,
    pegel: f32,
}

impl Cym {
    fn new(fs: f32) -> Self {
        Self {
            metall: Metall::new(METALL_808, fs, 0xc7a1),
            bp_lo: Svf::new(3440.0, 1.0, fs),
            bp_hi: Svf::new(7100.0, 1.0, fs),
            hp: OnePole::new(1500.0, fs),
            env_lo: Huellkurve::new(fs, 0.6),
            env_hi: Huellkurve::new(fs, 0.3),
            ton: 0.5,
            pegel: 0.8,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.metall.set_tune(exp_map(0.8, 1.25, p[TUNE]));
        let tau = exp_map(0.25, 1.5, p[DECAY]);
        self.env_lo.set_tau(tau);
        self.env_hi.set_tau(tau * 0.45);
        self.ton = p[TON];
    }
    fn ausloesen(&mut self, vel: f32) {
        self.env_lo.ausloesen(vel);
        self.env_hi.ausloesen(vel);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.env_lo.wert() == 0.0 {
            return 0.0;
        }
        let m = self.metall.process();
        let lo = self.bp_lo.process(m).band * self.env_lo.process();
        let hi = self.bp_hi.process(m).band * self.env_hi.process();
        let y = self.hp.hp(lo * (1.0 - 0.7 * self.ton) + hi * (0.3 + 0.7 * self.ton));
        soft_clip(y * 3.0) * self.pegel
    }
}

// ---------------------------------------------------------------- Cowbell
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
            env_a: Huellkurve::new(fs, 0.005),
            env_b: Huellkurve::new(fs, 0.15),
            pegel: 0.8,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        let m = exp_map(0.85, 1.18, p[TUNE]);
        self.o1.set_freq(540.0 * m, self.fs);
        self.o2.set_freq(800.0 * m, self.fs);
        self.bp.set(exp_map(700.0, 2600.0, p[TON]), 1.3, self.fs);
        self.env_b.set_tau(exp_map(0.05, 0.4, p[DECAY]));
    }
    fn ausloesen(&mut self, vel: f32) {
        self.env_a.ausloesen(0.7 * vel);
        self.env_b.ausloesen(0.3 * vel);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.env_b.wert() == 0.0 {
            return 0.0;
        }
        let x = (self.o1.process() + self.o2.process()) * 0.5;
        let y = self.bp.process(x).band;
        let env = self.env_a.process() + self.env_b.process();
        soft_clip(y * env * 2.0) * self.pegel
    }
}

// ---------------------------------------------------------------- Maschine
pub struct Engine808 {
    rng: Rauschen,
    params: [Params; ANZAHL_STIMMEN],
    bd: Bd,
    sd: Sd,
    toms: [Tom; 3],
    rs: Rs,
    cp: Clap,
    ch: Hat,
    oh: Hat,
    cy: Cym,
    cb: Cowbell,
}

impl Engine808 {
    pub fn new(fs: f32) -> Self {
        let mut e = Self {
            rng: Rauschen::new(0x808),
            params: [STANDARD; ANZAHL_STIMMEN],
            bd: Bd::new(fs),
            sd: Sd::new(fs),
            toms: [
                Tom::new(fs, 90.0, 0x71),
                Tom::new(fs, 130.0, 0x72),
                Tom::new(fs, 190.0, 0x73),
            ],
            rs: Rs::new(fs),
            cp: Clap::new(fs, 3, 700.0, 1600.0, 1.4, 0xc1a9),
            ch: Hat::new(fs, false, 0xa1),
            oh: Hat::new(fs, true, 0xa2),
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

    fn update(&mut self, s: Stimme) {
        let p = self.params[s as usize];
        match s {
            Stimme::Bassdrum => self.bd.update(&p),
            Stimme::Snare => self.sd.update(&p),
            Stimme::TomTief => self.toms[0].update(&p),
            Stimme::TomMittel => self.toms[1].update(&p),
            Stimme::TomHoch => self.toms[2].update(&p),
            Stimme::Rimshot => self.rs.update(&p),
            Stimme::Clap => self.cp.update(&p),
            Stimme::HiHatZu => self.ch.update(&p),
            Stimme::HiHatOffen => self.oh.update(&p),
            Stimme::Becken => self.cy.update(&p),
            Stimme::Extra => self.cb.update(&p),
        }
    }

    pub fn ausloesen(&mut self, s: Stimme, vel: f32) {
        // Kleine Zufallsabweichung pro Schlag = analoges Leben.
        let drift = 1.0 + 0.004 * self.rng.bipolar();
        let v = vel * (1.0 + 0.03 * self.rng.bipolar());
        match s {
            Stimme::Bassdrum => self.bd.ausloesen(v, drift),
            Stimme::Snare => self.sd.ausloesen(v, drift),
            Stimme::TomTief => self.toms[0].ausloesen(v, drift),
            Stimme::TomMittel => self.toms[1].ausloesen(v, drift),
            Stimme::TomHoch => self.toms[2].ausloesen(v, drift),
            Stimme::Rimshot => self.rs.ausloesen(v, drift),
            Stimme::Clap => self.cp.ausloesen(v),
            Stimme::HiHatZu => {
                self.oh.abwuergen();
                self.ch.ausloesen(v);
            }
            Stimme::HiHatOffen => self.oh.ausloesen(v),
            Stimme::Becken => self.cy.ausloesen(v),
            Stimme::Extra => self.cb.ausloesen(v),
        }
    }

    /// Addiert ein Sample jeder Stimme auf `aus`.
    #[inline]
    pub fn process(&mut self, aus: &mut [f32; ANZAHL_STIMMEN]) {
        aus[0] += self.bd.process();
        aus[1] += self.sd.process();
        aus[2] += self.toms[0].process();
        aus[3] += self.toms[1].process();
        aus[4] += self.toms[2].process();
        aus[5] += self.rs.process();
        aus[6] += self.cp.process();
        aus[7] += self.ch.process();
        aus[8] += self.oh.process();
        aus[9] += self.cy.process();
        aus[10] += self.cb.process();
    }
}
