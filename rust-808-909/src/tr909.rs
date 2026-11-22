//! TR-909 nach Roland Service Notes (1984): Blockschaltbild S. 4,
//! Schaltungsbeschreibung S. 5/6, Schaltplan Voicing Board S. 11.
//! BD, SD, Toms teilen sich einen Rauschgenerator. Hi-Hat, Crash und Ride
//! waren 6-Bit-ROMs (~30 kHz Takt); hier synthetisch nachempfunden und
//! dann durch dieselbe Kette geschickt: 30 kHz, 6 Bit, VCA, Tiefpass.

use crate::dsp::*;
use crate::gemeinsam::{Clap, ClapConfig};
use crate::{Params, Stimme, ANZAHL_STIMMEN, DECAY, EXTRA, PEGEL, STANDARD, TON, TUNE};

const METALL_HAT: [f32; 6] = [263.0, 400.0, 421.0, 474.0, 587.0, 845.0];
const METALL_CRASH: [f32; 6] = [245.0, 370.0, 434.0, 580.0, 711.0, 920.0];
const METALL_RIDE: [f32; 6] = [310.0, 465.0, 512.0, 703.0, 828.0, 1203.0];

/// ROM-Takt laut Service Notes: ~60 kHz, durch zwei geteilt.
const ROM_TAKT: f32 = 30_000.0;
/// HN61256 = 256 kbit = 32768 Samples pro Instrument.
const ROM_SAMPLES: f32 = 32_768.0;

/// Dreieck -> Diodenformer -> annaehernd Sinus (wie IC13b / D65-D70).
#[inline]
fn diodenformer(tri: f32) -> f32 {
    soft_clip(1.25 * tri) / soft_clip(1.25)
}

// ---------------------------------------------------------------- Bassdrum
// VCO (Dreieck, IC13) + Former, VCA Q12 mit ENV1.
// ENV1 = C8 0,33 uF ueber VR5 1M + R58 47k  -> tau 15 .. 345 ms (DECAY)
// ENV3 = C9 0,33 uF ueber R57 22k + VR2 100k -> tau 7 .. 40 ms  (TUNE = Sweep-Laenge!)
// Attack: Impuls (Q8) + Rauschen ueber R46/C13 (~340 Hz) in VCA Q6, ENV2 ~15 ms.
// Akzent setzt die Startspannung von ENV3 -> tieferer Sweep.
struct Bd {
    fs: f32,
    phase: f32,
    a_env: Huellkurve,
    p_env: Huellkurve,
    n_env: Huellkurve,
    n_lp1: OnePole,
    n_lp2: OnePole,
    puls_n: u32,
    puls_lp: OnePole,
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
            phase: 0.25,
            a_env: Huellkurve::new(fs, 0.07),
            p_env: Huellkurve::new(fs, 0.017),
            n_env: Huellkurve::new(fs, 0.0155),
            n_lp1: OnePole::new(340.0, fs),
            n_lp2: OnePole::new(340.0, fs),
            puls_n: 0,
            puls_lp: OnePole::new(3000.0, fs),
            basis: 55.0,
            tiefe: 6.0,
            attack: 0.5,
            drive: 1.5,
            pegel: 0.8,
            vel: 1.0,
            drift: 1.0,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        // Oszillogramm S. 9: ~370 Hz -> ~200 Hz (20 ms) -> ~110 Hz (45 ms).
        self.p_env.set_tau(drei_punkt(0.010, 0.025, 0.045, p[TUNE]));
        // RC-Werte 15..345 ms; Mitte an das Oszillogramm S. 9 angelehnt.
        self.a_env.set_tau(drei_punkt(0.0155, 0.05, 0.345, p[DECAY]));
        self.drive = exp_map(0.5, 3.0, p[TON]);
        self.attack = p[EXTRA];
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        self.drift = drift;
        self.phase = 0.25;
        self.tiefe = 6.0 * (1.0 + 0.15 * (vel - 1.0));
        self.a_env.ausloesen(1.0);
        self.p_env.ausloesen(1.0);
        self.n_env.ausloesen(1.0);
        self.puls_n = (0.0007 * self.fs) as u32;
    }
    #[inline]
    fn process(&mut self, noise: f32) -> f32 {
        if self.a_env.wert() == 0.0 {
            return 0.0;
        }
        let f = self.basis * self.drift * (1.0 + self.tiefe * self.p_env.process());
        phase_weiter(&mut self.phase, f, self.fs);
        let welle = diodenformer(dreieck(self.phase));
        let amp = self.a_env.process();
        let body = soft_clip(welle * amp * self.drive) / soft_clip(self.drive);
        let puls = if self.puls_n > 0 {
            self.puls_n -= 1;
            1.0
        } else {
            0.0
        };
        let thump = self.n_lp2.lp(self.n_lp1.lp(noise)) * self.n_env.process() * 6.0;
        let klick = (self.puls_lp.lp(puls) * 0.8 + thump) * self.attack;
        ausgang(body * 0.95 + klick, self.vel, self.pegel)
    }
}

// ---------------------------------------------------------------- Snare
// VCO1/VCO2: C69 0,01 uF / C71 0,0068 uF -> Verhaeltnis 1 : 1,47, beide
// Dreieck -> Diodenformer. Reset bei Trigger, Pitch-Bend ~20 ms.
// VCA Q50: C83 0,1 uF * R291 220k = 22 ms · VCA Q51: C82 0,047 uF * R295 220k = 10 ms.
// Rauschen: Tiefpass (~8,5 kHz) -> VCA mit ENV4 (TONE: C67 0,47 uF * 100k..600k
// = 47..280 ms) + Hochpass (~4 kHz) -> VCA ENV5 (kurz) -> SNAPPY regelt den Anteil.
struct Sd {
    fs: f32,
    ph1: f32,
    ph2: f32,
    e1: Huellkurve,
    e2: Huellkurve,
    p_env: Huellkurve,
    e4: Huellkurve,
    e5: Huellkurve,
    lp: Svf,
    hp: Svf,
    tune: f32,
    dk: f32,
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
            e1: Huellkurve::new(fs, 0.022),
            e2: Huellkurve::new(fs, 0.010),
            p_env: Huellkurve::new(fs, 0.006),
            e4: Huellkurve::new(fs, 0.115),
            e5: Huellkurve::new(fs, 0.012),
            lp: Svf::new(8500.0, 0.7, fs),
            hp: Svf::new(4000.0, 0.7, fs),
            tune: 1.0,
            dk: 1.0,
            snappy: 0.5,
            pegel: 0.8,
            vel: 1.0,
            drift: 1.0,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.tune = exp_map(0.8, 1.25, p[TUNE]);
        self.dk = exp_map(0.7, 1.4, p[DECAY]);
        // Oszillogramm S. 9: Koerper nach ~12 ms weitgehend weg.
        self.e1.set_tau(0.010 * self.dk);
        self.e2.set_tau(0.007 * self.dk);
        self.e4.set_tau(exp_map(0.047, 0.28, p[TON]));
        self.snappy = p[EXTRA];
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        self.drift = drift;
        self.ph1 = 0.25;
        self.ph2 = 0.25;
        self.e1.ausloesen(1.0);
        self.e2.ausloesen(1.0);
        self.p_env.ausloesen(1.0);
        self.e4.ausloesen(1.0);
        self.e5.ausloesen(1.0);
    }
    #[inline]
    fn process(&mut self, noise: f32) -> f32 {
        if self.e4.wert() == 0.0 && self.e1.wert() == 0.0 {
            return 0.0;
        }
        let m = self.tune * self.drift * (1.0 + 0.35 * self.p_env.process());
        phase_weiter(&mut self.ph1, 300.0 * m, self.fs);
        phase_weiter(&mut self.ph2, 441.0 * m, self.fs);
        let body = diodenformer(dreieck(self.ph1)) * self.e1.process() * 0.75
            + diodenformer(dreieck(self.ph2)) * self.e2.process() * 0.55;
        let n_lp = self.lp.process(noise).tief;
        let n_hp = self.hp.process(n_lp).hoch;
        let rausch = (n_lp * self.e4.process() * 0.7 + n_hp * self.e5.process() * 0.9) * self.snappy * 1.0;
        ausgang(body + rausch, self.vel, self.pegel)
    }
}

// ---------------------------------------------------------------- Toms
// Jede Tom: drei VCOs (C19 0,033 / C18 0,022 / C20 0,012 uF -> 1 : 1,5 : 2,75),
// eigene Huellkurven, gemeinsamer CV-Sweep (ENV4), DECAY auf der Haupt-Huellkurve.
// Gemeinsamer Tom-Rauschpfad (VCA Q33), betont den Anschlag.
struct Tom {
    fs: f32,
    slot: f32,
    ph: [f32; 3],
    e: [Huellkurve; 3],
    p_env: Huellkurve,
    n_env: Huellkurve,
    n_lp: OnePole,
    freq: f32,
    tiefe: f32,
    n_menge: f32,
    pegel: f32,
    vel: f32,
    drift: f32,
}

const TOM_RATIO: [f32; 3] = [1.0, 1.5, 2.75];
const TOM_MIX: [f32; 3] = [1.0, 0.4, 0.25];

impl Tom {
    fn new(fs: f32, slot: f32) -> Self {
        Self {
            fs,
            slot,
            ph: [0.25; 3],
            e: [
                Huellkurve::new(fs, 0.17),
                Huellkurve::new(fs, 0.1),
                Huellkurve::new(fs, 0.026),
            ],
            p_env: Huellkurve::new(fs, 0.045),
            n_env: Huellkurve::new(fs, 0.012),
            n_lp: OnePole::new(2000.0, fs),
            freq: slot,
            tiefe: 0.35,
            n_menge: 0.2,
            pegel: 0.8,
            vel: 1.0,
            drift: 1.0,
        }
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.freq = self.slot * exp_map(0.75, 1.33, p[TUNE]);
        let tau = drei_punkt(0.04, 0.09, 0.25, p[DECAY]);
        self.e[0].set_tau(tau);
        self.e[1].set_tau(tau.min(0.103));
        self.tiefe = lin_map(0.1, 0.6, p[EXTRA]);
        self.n_menge = 0.5 * p[TON];
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        self.drift = drift;
        self.ph = [0.25; 3];
        for e in self.e.iter_mut() {
            e.ausloesen(1.0);
        }
        self.p_env.ausloesen(1.0);
        self.n_env.ausloesen(1.0);
    }
    #[inline]
    fn process(&mut self, noise: f32) -> f32 {
        if self.e[0].wert() == 0.0 {
            return 0.0;
        }
        let f = self.freq * self.drift * (1.0 + self.tiefe * self.p_env.process());
        let mut body = 0.0;
        for i in 0..3 {
            phase_weiter(&mut self.ph[i], f * TOM_RATIO[i], self.fs);
            let w = if i == 1 {
                dreieck(self.ph[i])
            } else {
                diodenformer(dreieck(self.ph[i]))
            };
            body += w * self.e[i].process() * TOM_MIX[i];
        }
        let n = self.n_lp.lp(noise) * self.n_env.process() * self.n_menge * 3.0;
        ausgang(body * 0.8 + n, self.vel, self.pegel)
    }
}

// ---------------------------------------------------------------- Rimshot
// Drei Bandpaesse (Mehrfach-Gegenkopplung), aus den Bauteilen berechnet:
// F2 ~238 Hz (C116/C115 0,027 uF), F1 ~538 Hz (C113/C112 0,01 uF),
// F3 ~1360 Hz (C118/C117 0,0047 uF, R417 3,3k). Klingzeiten laut Plan 25/20/5 ms.
// Danach Clipper (D91/D92), VCA, Hochpass (~230 Hz).
struct Rs {
    s: [Schwinger; 3],
    env: Huellkurve,
    hp: Svf,
    tune: f32,
    dk: f32,
    ton: f32,
    pegel: f32,
    vel: f32,
}

const RS_FREQ: [f32; 3] = [238.0, 538.0, 1360.0];
const RS_T10: [f32; 3] = [0.025, 0.020, 0.005];

impl Rs {
    fn new(fs: f32) -> Self {
        Self {
            s: [Schwinger::new(fs), Schwinger::new(fs), Schwinger::new(fs)],
            env: Huellkurve::new(fs, 0.008),
            hp: Svf::new(230.0, 0.7, fs),
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
        self.env.set_tau(0.008 * self.dk);
        self.ton = p[TON];
    }
    fn ausloesen(&mut self, vel: f32, drift: f32) {
        self.vel = vel;
        for i in 0..3 {
            self.s[i].set(RS_FREQ[i] * self.tune * drift, t10_zu_tau(RS_T10[i]) * self.dk);
            self.s[i].anstossen(1.0);
        }
        self.env.ausloesen(1.0);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        let a = self.s[0].process();
        let b = self.s[1].process();
        let c = self.s[2].process();
        let x = a * (0.9 - 0.4 * self.ton) + b * 0.7 + c * (0.3 + 0.7 * self.ton);
        let geklippt = soft_clip(x * 3.0);
        let y = self.hp.process(geklippt * self.env.process()).hoch;
        ausgang(y * 1.4, self.vel, self.pegel)
    }
}

// ---------------------------------------------------------------- Becken (ROM-Kette)
#[derive(Clone, Copy, PartialEq)]
enum BeckenArt {
    HatZu,
    HatOffen,
    Crash,
    Ride,
}

/// Synthetische Quelle -> 30-kHz-Abtastung -> 6 Bit -> VCA -> Tiefpass.
/// Endet nach der ROM-Laenge (32768 Samples beim jeweiligen Takt).
struct Becken {
    fs: f32,
    art: BeckenArt,
    metall: Metall,
    rng: Rauschen,
    filt: Svf,
    hp: OnePole,
    abt: Abtaster,
    dac_lp: Svf,
    env: Huellkurve,
    tau: f32,
    rate: f32,
    rest: u32,
    koernung: f32,
    pegel: f32,
}

impl Becken {
    fn new(fs: f32, art: BeckenArt, seed: u32) -> Self {
        let (basis, fc, q) = match art {
            BeckenArt::HatZu | BeckenArt::HatOffen => (METALL_HAT, 9000.0, 0.9),
            BeckenArt::Crash => (METALL_CRASH, 3500.0, 0.7),
            BeckenArt::Ride => (METALL_RIDE, 5500.0, 0.8),
        };
        let mut b = Self {
            fs,
            art,
            metall: Metall::new(basis, fs, seed),
            rng: Rauschen::new(seed ^ 0x5a5a),
            filt: Svf::new(fc, q, fs),
            hp: OnePole::new(2500.0, fs),
            abt: Abtaster::default(),
            dac_lp: Svf::new(12_000.0, 0.7, fs),
            env: Huellkurve::new(fs, 0.03),
            tau: 0.03,
            rate: ROM_TAKT,
            rest: 0,
            koernung: 1.0,
            pegel: 0.8,
        };
        b.abt.set_rate(ROM_TAKT, fs);
        b
    }
    fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        let m = exp_map(0.8, 1.25, p[TUNE]);
        self.metall.set_tune(m);
        self.tau = match self.art {
            BeckenArt::HatZu => drei_punkt(0.008, 0.015, 0.04, p[DECAY]),
            BeckenArt::HatOffen => drei_punkt(0.05, 0.11, 0.35, p[DECAY]),
            BeckenArt::Crash => drei_punkt(0.15, 0.35, 0.8, p[DECAY]),
            BeckenArt::Ride => drei_punkt(0.2, 0.4, 0.9, p[DECAY]),
        };
        self.env.set_tau(self.tau);
        // Crash und Ride: TUNE aendert den ROM-Takt (Tonhoehe und Laenge).
        self.rate = match self.art {
            BeckenArt::Crash | BeckenArt::Ride => ROM_TAKT * m,
            _ => ROM_TAKT,
        };
        self.abt.set_rate(self.rate, self.fs);
        self.dac_lp.set((self.rate * 0.42).min(0.45 * self.fs), 0.7, self.fs);
        let fc = match self.art {
            BeckenArt::HatZu | BeckenArt::HatOffen => drei_punkt(6500.0, 9000.0, 12000.0, p[TON]),
            BeckenArt::Crash => drei_punkt(2000.0, 3500.0, 6000.0, p[TON]),
            BeckenArt::Ride => drei_punkt(3500.0, 5500.0, 8000.0, p[TON]),
        };
        let q = if matches!(self.art, BeckenArt::Crash) { 0.7 } else { 0.9 };
        self.filt.set(fc, q, self.fs);
        self.koernung = p[EXTRA];
    }
    fn ausloesen(&mut self, vel: f32) {
        self.env.set_tau(self.tau);
        self.env.ausloesen(vel);
        self.rest = (ROM_SAMPLES / self.rate * self.fs) as u32;
    }
    fn abwuergen(&mut self) {
        self.env.set_tau(0.004);
    }
    #[inline]
    fn process(&mut self) -> f32 {
        if self.env.wert() == 0.0 {
            return 0.0;
        }
        if self.rest > 0 {
            self.rest -= 1;
        } else {
            self.env.set_tau(0.002); // ROM zu Ende
        }
        let roh = match self.art {
            BeckenArt::HatZu | BeckenArt::HatOffen => self.metall.process() * 0.6 + self.rng.bipolar() * 0.4,
            BeckenArt::Crash => self.rng.bipolar() * 0.6 + self.metall.process() * 0.5,
            BeckenArt::Ride => self.metall.process() * 0.8 + self.rng.bipolar() * 0.15,
        };
        let gef = match self.art {
            BeckenArt::Crash => self.filt.process(roh).hoch,
            _ => self.hp.hp(self.filt.process(roh).band),
        };
        // "ROM-Inhalt": voll ausgesteuert, mit ROM-Takt abgetastet, 6 Bit.
        let voll = (gef * 2.5).clamp(-1.0, 1.0);
        let abgetastet = self.abt.process(voll);
        let rom = abgetastet + (sechs_bit(abgetastet) - abgetastet) * self.koernung;
        let y = self.dac_lp.process(rom * self.env.process()).tief;
        ausgang(y * 1.1, 1.0, self.pegel)
    }
}

// ---------------------------------------------------------------- Maschine
pub struct Engine909 {
    rng: Rauschen,
    rauschen: Rauschen,
    params: [Params; ANZAHL_STIMMEN],
    bd: Bd,
    sd: Sd,
    toms: [Tom; 3],
    rs: Rs,
    cp: Clap,
    ch: Becken,
    oh: Becken,
    crash: Becken,
    ride: Becken,
}

impl Engine909 {
    pub fn new(fs: f32) -> Self {
        let mut params = [STANDARD; ANZAHL_STIMMEN];
        // Die Original-Becken sind voll 6 Bit.
        for s in [Stimme::HiHatZu, Stimme::HiHatOffen, Stimme::Becken, Stimme::Extra] {
            params[s as usize][EXTRA] = 1.0;
        }
        let mut e = Self {
            rng: Rauschen::new(0x909),
            rauschen: Rauschen::new(0x32_5747),
            params,
            bd: Bd::new(fs),
            sd: Sd::new(fs),
            toms: [Tom::new(fs, 85.0), Tom::new(fs, 125.0), Tom::new(fs, 175.0)],
            rs: Rs::new(fs),
            cp: Clap::new(
                fs,
                ClapConfig {
                    bursts: 4,
                    f: (800.0, 1200.0, 1800.0),
                    q: 1.2,
                    abstand: (0.005, 0.007, 0.010),
                    hall: (0.012, 0.022, 0.05),
                    hall_pegel: 0.45,
                    seed: 0xc1a0,
                },
            ),
            ch: Becken::new(fs, BeckenArt::HatZu, 0xb1),
            oh: Becken::new(fs, BeckenArt::HatOffen, 0xb2),
            crash: Becken::new(fs, BeckenArt::Crash, 0xc4a5),
            ride: Becken::new(fs, BeckenArt::Ride, 0x41de),
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
            Stimme::Clap => self.cp.ausloesen(v),
            Stimme::HiHatZu => {
                self.oh.abwuergen();
                self.ch.ausloesen(v_metall);
            }
            Stimme::HiHatOffen => self.oh.ausloesen(v_metall),
            Stimme::Becken => self.crash.ausloesen(v_metall),
            Stimme::Extra => self.ride.ausloesen(v_metall),
        }
    }

    #[inline]
    pub fn process(&mut self, aus: &mut [f32; ANZAHL_STIMMEN]) {
        // Ein gemeinsamer Rauschgenerator fuer BD, SD und Toms (IC31-33).
        let n = self.rauschen.bipolar();
        aus[0] += self.bd.process(n);
        aus[1] += self.sd.process(n);
        aus[2] += self.toms[0].process(n);
        aus[3] += self.toms[1].process(n);
        aus[4] += self.toms[2].process(n);
        aus[5] += self.rs.process();
        aus[6] += self.cp.process();
        aus[7] += self.ch.process();
        aus[8] += self.oh.process();
        aus[9] += self.crash.process();
        aus[10] += self.ride.process();
    }
}

