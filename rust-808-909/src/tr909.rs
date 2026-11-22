//! TR-909: Bassdrum, Snare, Toms, Rimshot und Clap wie im Original analog
//! (Oszillatoren mit Tonhoehen-Sweep, Rauschen). Hi-Hats, Crash und Ride
//! waren im Original 6-Bit-Samples; hier sind sie synthetisch nachempfunden
//! (Rechteck-Metall + Rauschen), mit zuschaltbarer 6-Bit-Koernung.

use crate::dsp::*;
use crate::gemeinsam::Clap;
use crate::{Params, Stimme, ANZAHL_STIMMEN, DECAY, EXTRA, PEGEL, STANDARD, TON, TUNE};
use std::f32::consts::PI;

// Eigene, unharmonische Frequenzsaetze (Naeherung, keine Originalwerte).
const METALL_HAT: [f32; 6] = [263.0, 400.0, 421.0, 474.0, 587.0, 845.0];
const METALL_CRASH: [f32; 6] = [245.0, 370.0, 434.0, 580.0, 711.0, 920.0];
const METALL_RIDE: [f32; 6] = [310.0, 465.0, 512.0, 703.0, 828.0, 1203.0];

// ---------------------------------------------------------------- Bassdrum
struct Bd {
    fs: f32,
    phase: f32,
    a_env: Huellkurve,
    p_env: Huellkurve,
    k_env: Huellkurve,
    n_env: Huellkurve,
    n_lp: OnePole,
    rng: Rauschen,
    basis: f32,
    tiefe: f32,
    attack: f32,
    drive: f32,
    pegel: f32,
    vel: f32,
    drift: f32,
}

impl Bd {
    fn new(fs: f32) -> Self {
        Self {
            fs,
            phase: 0.0,
            a_env: Huellkurve::new(fs, 0.25),
            p_env: Huellkurve::new(fs, 0.012),
            k_env: Huellkurve::new(fs, 0.0006),
            n_env: Huellkurve::new(fs, 0.002),
            n_lp: OnePole::new(4000.0, fs),
            rng: Rauschen::new(0x909_bd),
            basis: 50.0,
            tiefe: 2.5,
            attack: 0.5,
            drive: 2.0,
            pegel: 0.8,
            vel: 1.0,
            drift: 1.0,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.basis = exp_map(44.0, 60.0, p[TUNE]);
        self.tiefe = lin_map(1.5, 3.5, p[TUNE]);
        self.a_env.set_tau(exp_map(0.08, 0.6, p[DECAY]));
        self.drive = lin_map(1.0, 3.0, p[TON]);
        self.attack = p[EXTRA];
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        self.drift = drift;
        self.phase = 0.0;
        self.a_env.ausloesen(1.0);
        self.p_env.ausloesen(1.0);
        self.k_env.ausloesen(1.0);
        self.n_env.ausloesen(1.0);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.a_env.wert() == 0.0 {
            return 0.0;
        }
        let pe = self.p_env.process();
        let f = self.basis * self.drift * (1.0 + self.tiefe * pe);
        phase_weiter(&mut self.phase, f, self.fs);
        let s = (2.0 * PI * self.phase).sin();
        let amp = self.a_env.process();
        // Akzent faehrt die Stufe staerker an = mehr Punch.
        let d = self.drive * (0.8 + 0.3 * self.vel);
        let body = soft_clip(s * amp * d * self.vel) / soft_clip(d);
        let klick = (self.k_env.process() * 0.8
            + self.n_lp.lp(self.rng.bipolar()) * self.n_env.process() * 0.8)
            * self.attack;
        (body * 0.9 + klick) * self.pegel
    }
}

// ---------------------------------------------------------------- Snare
struct Sd {
    fs: f32,
    ph1: f32,
    ph2: f32,
    a1: Huellkurve,
    a2: Huellkurve,
    p_env: Huellkurve,
    n_env: Huellkurve,
    n_lp: OnePole,
    n_hp: OnePole,
    rng: Rauschen,
    tune: f32,
    snappy: f32,
    pegel: f32,
    vel: f32,
    drift: f32,
}

impl Sd {
    fn new(fs: f32) -> Self {
        Self {
            fs,
            ph1: 0.25,
            ph2: 0.25,
            a1: Huellkurve::new(fs, 0.035),
            a2: Huellkurve::new(fs, 0.025),
            p_env: Huellkurve::new(fs, 0.012),
            n_env: Huellkurve::new(fs, 0.12),
            n_lp: OnePole::new(6000.0, fs),
            n_hp: OnePole::new(400.0, fs),
            rng: Rauschen::new(0x909_5d),
            tune: 1.0,
            snappy: 0.5,
            pegel: 0.8,
            vel: 1.0,
            drift: 1.0,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.tune = exp_map(0.8, 1.25, p[TUNE]);
        let k = lin_map(0.8, 1.3, p[DECAY]);
        self.a1.set_tau(0.035 * k);
        self.a2.set_tau(0.025 * k);
        self.n_env.set_tau(exp_map(0.04, 0.3, p[DECAY]));
        self.n_lp.set(exp_map(2500.0, 10000.0, p[TON]), self.fs);
        self.snappy = p[EXTRA];
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        self.drift = drift;
        self.ph1 = 0.25;
        self.ph2 = 0.25;
        self.a1.ausloesen(1.0);
        self.a2.ausloesen(1.0);
        self.p_env.ausloesen(1.0);
        self.n_env.ausloesen(1.0);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.n_env.wert() == 0.0 && self.a1.wert() == 0.0 {
            return 0.0;
        }
        let pe = 1.0 + 0.25 * self.p_env.process();
        let m = self.tune * self.drift * pe;
        phase_weiter(&mut self.ph1, 185.0 * m, self.fs);
        phase_weiter(&mut self.ph2, 330.0 * m, self.fs);
        let body = dreieck(self.ph1) * self.a1.process() * 0.6 + dreieck(self.ph2) * self.a2.process() * 0.45;
        let n = self.n_hp.hp(self.n_lp.lp(self.rng.bipolar()));
        let noise = n * self.n_env.process() * self.snappy * 1.4;
        soft_clip((body + noise) * self.vel * 1.1) * self.pegel
    }
}

// ---------------------------------------------------------------- Toms
struct Tom {
    fs: f32,
    slot: f32,
    phase: f32,
    a_env: Huellkurve,
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
            fs,
            slot,
            phase: 0.25,
            a_env: Huellkurve::new(fs, 0.25),
            p_env: Huellkurve::new(fs, 0.03),
            n_env: Huellkurve::new(fs, 0.012),
            n_lp: OnePole::new(1500.0, fs),
            rng: Rauschen::new(seed),
            freq: slot,
            tiefe: 0.4,
            n_menge: 0.15,
            pegel: 0.8,
            vel: 1.0,
            drift: 1.0,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.freq = self.slot * exp_map(0.75, 1.33, p[TUNE]);
        self.a_env.set_tau(exp_map(0.1, 0.5, p[DECAY]));
        self.tiefe = lin_map(0.1, 0.7, p[EXTRA]);
        self.n_menge = 0.35 * p[TON];
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        self.drift = drift;
        self.phase = 0.25;
        self.a_env.ausloesen(1.0);
        self.p_env.ausloesen(1.0);
        self.n_env.ausloesen(1.0);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.a_env.wert() == 0.0 {
            return 0.0;
        }
        let f = self.freq * self.drift * (1.0 + self.tiefe * self.p_env.process());
        phase_weiter(&mut self.phase, f, self.fs);
        let body = dreieck(self.phase) * self.a_env.process();
        let noise = self.n_lp.lp(self.rng.bipolar()) * self.n_env.process() * self.n_menge * 2.0;
        soft_clip((body + noise) * 1.5 * self.vel) / soft_clip(1.5) * self.pegel
    }
}

// ---------------------------------------------------------------- Rimshot
struct Rs {
    r: [Resonator; 3],
    hp: OnePole,
    tune: f32,
    ton: f32,
    pegel: f32,
    vel: f32,
}

const RS_FREQ: [f32; 3] = [480.0, 1150.0, 1720.0];
const RS_TAU: [f32; 3] = [0.005, 0.004, 0.003];

impl Rs {
    fn new(fs: f32) -> Self {
        Self {
            r: [Resonator::new(fs), Resonator::new(fs), Resonator::new(fs)],
            hp: OnePole::new(400.0, fs),
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
        for i in 0..3 {
            self.r[i].set_tau(RS_TAU[i] * d);
            self.r[i].set_freq(RS_FREQ[i] * self.tune);
        }
        self.ton = p[TON];
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        for i in 0..3 {
            self.r[i].set_freq(RS_FREQ[i] * self.tune * drift);
            self.r[i].anstossen(1.0);
        }
    }
    #[inline]
    fn process(&mut self) -> f32 {
        let a = self.r[0].process();
        let b = self.r[1].process();
        let c = self.r[2].process();
        let x = a * (1.0 - 0.5 * self.ton) * 0.7 + b * 0.5 + c * (0.2 + 0.6 * self.ton);
        soft_clip(self.hp.hp(x) * 2.5 * self.vel) * 0.7 * self.pegel
    }
}

// ---------------------------------------------------------------- Hi-Hats
struct Hat {
    fs: f32,
    offen: bool,
    metall: Metall,
    rng: Rauschen,
    bp: Svf,
    hp: OnePole,
    env: Huellkurve,
    tau: f32,
    koernung: f32,
    pegel: f32,
}

impl Hat {
    fn new(fs: f32, offen: bool, seed: u32) -> Self {
        Self {
            fs,
            offen,
            metall: Metall::new(METALL_HAT, fs, seed),
            rng: Rauschen::new(seed ^ 0x5a5a),
            bp: Svf::new(9000.0, 0.9, fs),
            hp: OnePole::new(7000.0, fs),
            env: Huellkurve::new(fs, 0.03),
            tau: 0.03,
            koernung: 0.5,
            pegel: 0.8,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.metall.set_tune(exp_map(0.8, 1.25, p[TUNE]));
        self.tau = if self.offen {
            exp_map(0.08, 0.6, p[DECAY])
        } else {
            exp_map(0.01, 0.06, p[DECAY])
        };
        self.env.set_tau(self.tau);
        self.bp.set(exp_map(6500.0, 12000.0, p[TON]), 0.9, self.fs);
        self.koernung = p[EXTRA];
    }
    fn ausloesen(&mut self, vel: f32) {
        self.env.set_tau(self.tau);
        self.env.ausloesen(vel);
    }
    fn abwuergen(&mut self) {
        self.env.set_tau(0.004);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.env.wert() == 0.0 {
            return 0.0;
        }
        let roh = self.metall.process() * 0.6 + self.rng.bipolar() * 0.4;
        // Quelle koernen, DANN Huellkurve - wie Sample-ROM -> VCA im Original.
        let src = roh + (sechs_bit(roh) - roh) * self.koernung;
        let y = self.hp.hp(self.bp.process(src).band);
        soft_clip(y * self.env.process() * 4.0) * self.pegel
    }
}

// ---------------------------------------------------------------- Crash
struct Crash {
    fs: f32,
    metall: Metall,
    rng: Rauschen,
    hp: Svf,
    env: Huellkurve,
    koernung: f32,
    pegel: f32,
}

impl Crash {
    fn new(fs: f32) -> Self {
        Self {
            fs,
            metall: Metall::new(METALL_CRASH, fs, 0xc4a5),
            rng: Rauschen::new(0xc4a6),
            hp: Svf::new(3500.0, 0.7, fs),
            env: Huellkurve::new(fs, 0.9),
            koernung: 0.5,
            pegel: 0.8,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.metall.set_tune(exp_map(0.8, 1.25, p[TUNE]));
        self.env.set_tau(exp_map(0.4, 2.0, p[DECAY]));
        self.hp.set(exp_map(2000.0, 6000.0, p[TON]), 0.7, self.fs);
        self.koernung = p[EXTRA];
    }
    fn ausloesen(&mut self, vel: f32) {
        self.env.ausloesen(vel);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.env.wert() == 0.0 {
            return 0.0;
        }
        let roh = self.rng.bipolar() * 0.6 + self.metall.process() * 0.5;
        let src = roh + (sechs_bit(roh) - roh) * self.koernung;
        let y = self.hp.process(src).hoch;
        soft_clip(y * self.env.process() * 1.8) * self.pegel
    }
}

// ---------------------------------------------------------------- Ride
struct Ride {
    fs: f32,
    metall: Metall,
    rng: Rauschen,
    bp: Svf,
    hp: OnePole,
    env: Huellkurve,
    koernung: f32,
    pegel: f32,
}

impl Ride {
    fn new(fs: f32) -> Self {
        Self {
            fs,
            metall: Metall::new(METALL_RIDE, fs, 0x41de),
            rng: Rauschen::new(0x41df),
            bp: Svf::new(5500.0, 0.8, fs),
            hp: OnePole::new(2500.0, fs),
            env: Huellkurve::new(fs, 0.7),
            koernung: 0.5,
            pegel: 0.8,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.metall.set_tune(exp_map(0.8, 1.25, p[TUNE]));
        self.env.set_tau(exp_map(0.3, 1.6, p[DECAY]));
        self.bp.set(exp_map(3500.0, 8000.0, p[TON]), 0.8, self.fs);
        self.koernung = p[EXTRA];
    }
    fn ausloesen(&mut self, vel: f32) {
        self.env.ausloesen(vel);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.env.wert() == 0.0 {
            return 0.0;
        }
        let roh = self.metall.process() * 0.8 + self.rng.bipolar() * 0.15;
        let src = roh + (sechs_bit(roh) - roh) * self.koernung;
        let y = self.hp.hp(self.bp.process(src).band);
        soft_clip(y * self.env.process() * 3.0) * self.pegel
    }
}

// ---------------------------------------------------------------- Maschine
pub struct Engine909 {
    rng: Rauschen,
    params: [Params; ANZAHL_STIMMEN],
    bd: Bd,
    sd: Sd,
    toms: [Tom; 3],
    rs: Rs,
    cp: Clap,
    ch: Hat,
    oh: Hat,
    crash: Crash,
    ride: Ride,
}

impl Engine909 {
    pub fn new(fs: f32) -> Self {
        let mut e = Self {
            rng: Rauschen::new(0x909),
            params: [STANDARD; ANZAHL_STIMMEN],
            bd: Bd::new(fs),
            sd: Sd::new(fs),
            toms: [
                Tom::new(fs, 85.0, 0x91),
                Tom::new(fs, 125.0, 0x92),
                Tom::new(fs, 175.0, 0x93),
            ],
            rs: Rs::new(fs),
            cp: Clap::new(fs, 4, 800.0, 1800.0, 1.2, 0xc1a0),
            ch: Hat::new(fs, false, 0xb1),
            oh: Hat::new(fs, true, 0xb2),
            crash: Crash::new(fs),
            ride: Ride::new(fs),
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
            Stimme::Becken => self.crash.update(&p),
            Stimme::Extra => self.ride.update(&p),
        }
    }

    pub fn ausloesen(&mut self, s: Stimme, vel: f32) {
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
            Stimme::Becken => self.crash.ausloesen(v),
            Stimme::Extra => self.ride.ausloesen(v),
        }
    }

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
        aus[9] += self.crash.process();
        aus[10] += self.ride.process();
    }
}
