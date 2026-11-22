//! Stimmen, die 808 und 909 nach dem gleichen Prinzip bauen.

use crate::dsp::*;
use crate::{Params, DECAY, EXTRA, PEGEL, TON, TUNE};

/// Handclap: gefiltertes Rauschen, mehrmals schnell angestossen, dann eine
/// kurze Hallfahne. Beide Maschinen machen das analog.
pub struct Clap {
    fs: f32,
    n: u32,
    aktiv: bool,
    rng: Rauschen,
    filter: Svf,
    abstand: f32,
    bursts: u32,
    burst_tau: f32,
    tail_tau: f32,
    tail_pegel: f32,
    pegel: f32,
    vel: f32,
    f_min: f32,
    f_max: f32,
    q: f32,
}

impl Clap {
    pub fn new(fs: f32, bursts: u32, f_min: f32, f_max: f32, q: f32, seed: u32) -> Self {
        Self {
            fs,
            n: 0,
            aktiv: false,
            rng: Rauschen::new(seed),
            filter: Svf::new(1000.0, q, fs),
            abstand: 0.01,
            bursts,
            burst_tau: 0.0035,
            tail_tau: 0.15,
            tail_pegel: 0.55,
            pegel: 0.8,
            vel: 1.0,
            f_min,
            f_max,
            q,
        }
    }

    pub fn update(&mut self, p: &Params) {
        self.pegel = p[PEGEL];
        self.tail_tau = exp_map(0.04, 0.4, p[DECAY]);
        let f = exp_map(self.f_min, self.f_max, p[TON]) * exp_map(0.85, 1.18, p[TUNE]);
        self.filter.set(f, self.q, self.fs);
        self.abstand = exp_map(0.006, 0.016, p[EXTRA]);
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
        let t_letzt = self.abstand * (self.bursts - 1) as f32;
        let burst = if t < t_letzt + self.abstand {
            (-(t % self.abstand) / self.burst_tau).exp()
        } else {
            0.0
        };
        let tail = if t >= t_letzt {
            self.tail_pegel * (-(t - t_letzt) / self.tail_tau).exp()
        } else {
            0.0
        };
        if t > t_letzt + 12.0 * self.tail_tau {
            self.aktiv = false;
        }
        let env = burst.max(tail);
        let x = self.filter.process(self.rng.bipolar()).band;
        soft_clip(x * env * 3.0 * self.vel) * self.pegel
    }
}
