//! Stimmen, die 808 und 909 nach dem gleichen Prinzip bauen.

use crate::dsp::*;
use crate::{Params, DECAY, EXTRA, PEGEL, TON, TUNE};

/// Handclap (beide Maschinen, laut Service Manual): Rauschen durch ein
/// Bandpassfilter speist zwei parallele VCAs. Einer bekommt die
/// Saegezahn-Huellkurve (mehrere schnelle Stoesse), der andere eine lange
/// Huellkurve = der "Hall".
pub struct Clap {
    fs: f32,
    n: u32,
    aktiv: bool,
    rng: Rauschen,
    filter: Svf,
    abstand: f32,
    bursts: u32,
    burst_tau: f32,
    hall_tau: f32,
    hall_pegel: f32,
    hall: (f32, f32, f32),
    abstand_bereich: (f32, f32, f32),
    pegel: f32,
    vel: f32,
    f: (f32, f32, f32),
    q: f32,
}

pub struct ClapConfig {
    pub bursts: u32,
    /// Bandpass links / Mitte / rechts (Hz)
    pub f: (f32, f32, f32),
    pub q: f32,
    /// Abstand der Stoesse links / Mitte / rechts (s)
    pub abstand: (f32, f32, f32),
    /// Hall-Zeitkonstante links / Mitte / rechts (s)
    pub hall: (f32, f32, f32),
    pub hall_pegel: f32,
    pub seed: u32,
}

impl Clap {
    pub fn new(fs: f32, c: ClapConfig) -> Self {
        Self {
            fs,
            n: 0,
            aktiv: false,
            rng: Rauschen::new(c.seed),
            filter: Svf::new(1000.0, c.q, fs),
            abstand: 0.01,
            bursts: c.bursts,
            burst_tau: 0.0035,
            hall_tau: c.hall.1,
            hall_pegel: c.hall_pegel,
            hall: c.hall,
            abstand_bereich: c.abstand,
            pegel: 0.8,
            vel: 1.0,
            f: c.f,
            q: c.q,
        }
    }

    pub fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.hall_tau = drei_punkt(self.hall.0, self.hall.1, self.hall.2, p[DECAY]);
        let f = drei_punkt(self.f.0, self.f.1, self.f.2, p[TON]) * exp_map(0.85, 1.18, p[TUNE]);
        self.filter.set(f, self.q, self.fs);
        let a = self.abstand_bereich;
        self.abstand = drei_punkt(a.0, a.1, a.2, p[EXTRA]);
    }

    pub fn ausloesen(&mut self, vel: f32) {
        self.n = 0;
        self.aktiv = true;
        self.vel = vel;
    }

    #[inline]
    pub fn process(&mut self) -> f32 {
        if !self.aktiv {
            return 0.0;
        }
        let t = self.n as f32 / self.fs;
        self.n += 1;
        let ende_bursts = self.abstand * self.bursts as f32;
        let saege = if t < ende_bursts {
            (-(t % self.abstand) / self.burst_tau).exp()
        } else {
            0.0
        };
        let hall = self.hall_pegel * (-t / self.hall_tau).exp();
        if t > ende_bursts + 12.0 * self.hall_tau {
            self.aktiv = false;
        }
        let x = self.filter.process(self.rng.bipolar()).band;
        ausgang(x * (saege + hall) * 2.2, self.vel, self.pegel)
    }
}
