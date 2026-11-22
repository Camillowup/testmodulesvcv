//! Kleine DSP-Bausteine: Rauschen, Huellkurven, Filter, Resonator, Oszillatoren.

use std::f32::consts::PI;

/// Sehr kleine Werte auf 0 setzen (verhindert Denormals = CPU-Spitzen).
#[inline]
pub fn flush(x: f32) -> f32 {
    if x.abs() < 1.0e-20 {
        0.0
    } else {
        x
    }
}

/// Weiche Saettigung wie bei einer analogen Stufe.
#[inline]
pub fn soft_clip(x: f32) -> f32 {
    x.tanh()
}

/// 0..1 exponentiell auf min..max abbilden (fuer Frequenzen und Zeiten).
#[inline]
pub fn exp_map(min: f32, max: f32, t: f32) -> f32 {
    min * (max / min).powf(t.clamp(0.0, 1.0))
}

/// 0..1 linear auf min..max abbilden.
#[inline]
pub fn lin_map(min: f32, max: f32, t: f32) -> f32 {
    min + (max - min) * t.clamp(0.0, 1.0)
}

/// Dreipunkt-Abbildung (links, Mitte = 12 Uhr, rechts), exponentiell.
/// So liegen die Werte aus Rolands Messtabelle genau auf den Reglerstellungen.
#[inline]
pub fn drei_punkt(links: f32, mitte: f32, rechts: f32, t: f32) -> f32 {
    let t = t.clamp(0.0, 1.0);
    if t < 0.5 {
        exp_map(links, mitte, t * 2.0)
    } else {
        exp_map(mitte, rechts, (t - 0.5) * 2.0)
    }
}

/// Zeit bis 1/10 der Amplitude (so misst Roland "Decay") -> Zeitkonstante.
#[inline]
pub fn t10_zu_tau(t10: f32) -> f32 {
    t10 / std::f32::consts::LN_10
}

/// Gemeinsame Ausgangsstufe: etwas Headroom, Akzent faehrt sie in die Saettigung.
#[inline]
pub fn ausgang(x: f32, vel: f32, pegel: f32) -> f32 {
    soft_clip(x * 0.5 * vel) * 1.6 * pegel
}

/// Auf 6 Bit quantisieren (wie die Becken-Samples der echten 909).
#[inline]
pub fn sechs_bit(x: f32) -> f32 {
    (x.clamp(-1.0, 1.0) * 31.5).round() / 31.5
}

/// Weisses Rauschen (xorshift, schnell und reproduzierbar).
#[derive(Clone)]
pub struct Rauschen {
    s: u32,
}

impl Rauschen {
    pub fn new(seed: u32) -> Self {
        Self { s: seed.max(1) }
    }
    #[inline]
    fn next_u32(&mut self) -> u32 {
        let mut x = self.s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        self.s = x;
        x
    }
    /// 0..1
    #[inline]
    pub fn uni(&mut self) -> f32 {
        (self.next_u32() >> 8) as f32 / 16_777_216.0
    }
    /// -1..1
    #[inline]
    pub fn bipolar(&mut self) -> f32 {
        self.uni() * 2.0 - 1.0
    }
}

/// Exponentiell abfallende Huellkurve (wie ein entladender Kondensator).
#[derive(Clone)]
pub struct Huellkurve {
    wert: f32,
    koeff: f32,
    fs: f32,
}

impl Huellkurve {
    pub fn new(fs: f32, tau: f32) -> Self {
        let mut h = Self { wert: 0.0, koeff: 0.0, fs };
        h.set_tau(tau);
        h
    }
    /// tau = Zeitkonstante in Sekunden (nach tau ist der Wert auf ~37 % gefallen).
    pub fn set_tau(&mut self, tau: f32) {
        self.koeff = (-1.0 / (tau.max(1.0e-5) * self.fs)).exp();
    }
    pub fn ausloesen(&mut self, pegel: f32) {
        self.wert = pegel;
    }
    #[inline]
    pub fn process(&mut self) -> f32 {
        let v = self.wert;
        self.wert = flush(self.wert * self.koeff);
        v
    }
    #[inline]
    pub fn wert(&self) -> f32 {
        self.wert
    }
}

/// Einpoliges Filter (Tief- oder Hochpass, 6 dB/Oktave).
#[derive(Clone, Default)]
pub struct OnePole {
    z: f32,
    a: f32,
}

impl OnePole {
    pub fn new(fc: f32, fs: f32) -> Self {
        let mut f = Self::default();
        f.set(fc, fs);
        f
    }
    pub fn set(&mut self, fc: f32, fs: f32) {
        self.a = 1.0 - (-2.0 * PI * fc.min(0.45 * fs) / fs).exp();
    }
    #[inline]
    pub fn lp(&mut self, x: f32) -> f32 {
        self.z = flush(self.z + self.a * (x - self.z));
        self.z
    }
    #[inline]
    pub fn hp(&mut self, x: f32) -> f32 {
        x - self.lp(x)
    }
}

/// Zustandsvariables Filter (TPT/Cytomic-Bauweise), stabil auch bei Modulation.
#[derive(Clone, Default)]
pub struct Svf {
    ic1: f32,
    ic2: f32,
    a1: f32,
    a2: f32,
    a3: f32,
    k: f32,
}

#[allow(dead_code)]
pub struct SvfAus {
    pub tief: f32,
    /// Bandpass, auf Spitzenverstaerkung 1 normiert.
    pub band: f32,
    pub hoch: f32,
}

impl Svf {
    pub fn new(fc: f32, q: f32, fs: f32) -> Self {
        let mut f = Self::default();
        f.set(fc, q, fs);
        f
    }
    pub fn set(&mut self, fc: f32, q: f32, fs: f32) {
        let fc = fc.clamp(10.0, 0.45 * fs);
        let g = (PI * fc / fs).tan();
        self.k = 1.0 / q.max(0.05);
        self.a1 = 1.0 / (1.0 + g * (g + self.k));
        self.a2 = g * self.a1;
        self.a3 = g * self.a2;
    }
    #[inline]
    pub fn process(&mut self, x: f32) -> SvfAus {
        let v3 = x - self.ic2;
        let v1 = self.a1 * self.ic1 + self.a2 * v3;
        let v2 = self.ic2 + self.a2 * self.ic1 + self.a3 * v3;
        self.ic1 = flush(2.0 * v1 - self.ic1);
        self.ic2 = flush(2.0 * v2 - self.ic2);
        SvfAus {
            tief: v2,
            band: v1 * self.k,
            hoch: x - self.k * v1 - v2,
        }
    }
}

/// Angestossener Schwinger auf Basis eines Zustandsvariablen-Filters.
/// Wie das Bridged-T-Netzwerk: ein Impuls bringt ihn zum Klingen, er schwingt
/// mit Frequenz f aus und faellt mit Zeitkonstante tau ab. Frequenz und Tau
/// duerfen sich waehrend des Klingens aendern (Attack-Sprung, Pitch-Drop).
#[derive(Clone)]
pub struct Schwinger {
    ic1: f32,
    ic2: f32,
    g: f32,
    k: f32,
    fs: f32,
    stoss: f32,
}

impl Schwinger {
    pub fn new(fs: f32) -> Self {
        let mut s = Self {
            ic1: 0.0,
            ic2: 0.0,
            g: 0.0,
            k: 0.1,
            fs,
            stoss: 0.0,
        };
        s.set(100.0, 0.1);
        s
    }
    pub fn set(&mut self, f: f32, tau: f32) {
        let f = f.clamp(5.0, 0.45 * self.fs);
        self.g = (PI * f / self.fs).tan();
        let w = 2.0 * PI * f;
        self.k = (2.0 / (tau.max(1.0e-4) * w)).clamp(1.0e-5, 1.9);
    }
    /// Anstoss mit Amplitude ~ menge.
    pub fn anstossen(&mut self, menge: f32) {
        self.stoss += menge / (2.0 * self.g);
    }
    #[inline]
    pub fn process(&mut self) -> f32 {
        let x = self.stoss;
        self.stoss = 0.0;
        let a1 = 1.0 / (1.0 + self.g * (self.g + self.k));
        let a2 = self.g * a1;
        let a3 = self.g * a2;
        let v3 = x - self.ic2;
        let v1 = a1 * self.ic1 + a2 * v3;
        let v2 = self.ic2 + a2 * self.ic1 + a3 * v3;
        self.ic1 = flush(2.0 * v1 - self.ic1);
        self.ic2 = flush(2.0 * v2 - self.ic2);
        v1
    }
}

/// Abtast-Halte-Glied: simuliert eine niedrigere Abtastrate (z. B. 909-ROM-Takt).
#[derive(Clone, Default)]
pub struct Abtaster {
    phase: f32,
    inc: f32,
    wert: f32,
}

impl Abtaster {
    pub fn set_rate(&mut self, rate: f32, fs: f32) {
        self.inc = (rate / fs).clamp(0.0, 1.0);
    }
    #[inline]
    pub fn process(&mut self, x: f32) -> f32 {
        self.phase += self.inc;
        if self.phase >= 1.0 {
            self.phase -= 1.0;
            self.wert = x;
        }
        self.wert
    }
}

#[inline]
fn blep(mut t: f32, dt: f32) -> f32 {
    if t < dt {
        t /= dt;
        t + t - t * t - 1.0
    } else if t > 1.0 - dt {
        t = (t - 1.0) / dt;
        t * t + t + t + 1.0
    } else {
        0.0
    }
}

/// Rechteck-Oszillator mit PolyBLEP gegen digitales Zischen (Aliasing).
#[derive(Clone, Default)]
pub struct Rechteck {
    phase: f32,
    inc: f32,
}

impl Rechteck {
    pub fn set_freq(&mut self, f: f32, fs: f32) {
        self.inc = (f / fs).clamp(0.0, 0.45);
    }
    pub fn set_phase(&mut self, p: f32) {
        self.phase = p.fract();
    }
    #[inline]
    pub fn process(&mut self) -> f32 {
        let t = self.phase;
        let dt = self.inc;
        let mut y = if t < 0.5 { 1.0 } else { -1.0 };
        y += blep(t, dt);
        let mut t2 = t + 0.5;
        if t2 >= 1.0 {
            t2 -= 1.0;
        }
        y -= blep(t2, dt);
        self.phase += dt;
        if self.phase >= 1.0 {
            self.phase -= 1.0;
        }
        y
    }
}

/// Sechs Rechteck-Oszillatoren mit "schiefen" Frequenzen = metallischer Klang.
#[derive(Clone)]
pub struct Metall {
    osz: [Rechteck; 6],
    basis: [f32; 6],
    fs: f32,
}

impl Metall {
    pub fn new(basis: [f32; 6], fs: f32, seed: u32) -> Self {
        let mut rng = Rauschen::new(seed);
        let mut osz: [Rechteck; 6] = Default::default();
        for (o, f) in osz.iter_mut().zip(basis.iter()) {
            o.set_freq(*f, fs);
            o.set_phase(rng.uni());
        }
        Self { osz, basis, fs }
    }
    pub fn set_tune(&mut self, mult: f32) {
        for (o, f) in self.osz.iter_mut().zip(self.basis.iter()) {
            o.set_freq(*f * mult, self.fs);
        }
    }
    #[inline]
    pub fn process(&mut self) -> f32 {
        let mut s = 0.0;
        for o in self.osz.iter_mut() {
            s += o.process();
        }
        s / 6.0
    }
}

/// Dreieck aus einer Phase 0..1 (Startphase 0.25 = Nulldurchgang).
#[inline]
pub fn dreieck(phase: f32) -> f32 {
    1.0 - 4.0 * (phase - 0.5).abs()
}

/// Phase weiterdrehen.
#[inline]
pub fn phase_weiter(phase: &mut f32, f: f32, fs: f32) {
    *phase += f / fs;
    if *phase >= 1.0 {
        *phase -= phase.floor();
    }
}
