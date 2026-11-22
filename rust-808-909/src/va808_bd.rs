//! TR-808 Bassdrum als Schaltungssimulation (Virtual Analog).
//! Netzliste Bauteil fuer Bauteil aus den Roland Service Notes (1981),
//! Schaltplan Voicing/Main Board S. 9 ("BASS DRUM"), Funktion laut S. 5.
//!
//! Nicht simuliert: Q39/Q40 (das Tor, das den gemeinsamen Trigger zur
//! Bassdrum durchschaltet) -> ersetzt durch die Triggerspannung selbst
//! (1 ms, 4 V ohne Akzent, bis 14 V mit Akzent, laut S. 4).
//! Q44 (Emitterfolger am Ausgang) -> Ausgang wird am Level-Poti abgenommen.

use crate::schaltung::{Knoten, Schaltung, MASSE};

// Bauteilmodelle (Kleinsignal-Typen 2SC945 / 1S2473)
const NPN_IS: f64 = 1.0e-14;
const NPN_BF: f64 = 250.0;
const NPN_BR: f64 = 5.0;
const D_IS: f64 = 1.0e-12;
/// uPC4558 an +/-15 V: Ausgang bis etwa +/-13,5 V
const OP_GRENZE: f64 = 13.5;

pub struct Bd808Va {
    s: Schaltung,
    trig: Knoten,
    tone_wiper: Knoten,
    pegel_knoten: Knoten,
    r_vr6: usize,
    r_vr5: usize,
    c41: usize,
    c42: usize,
    trig_rest: u32,
    trig_v: f64,
    os: u32,
    fs_intern: f64,
    pub pegel: f32,
}

impl Bd808Va {
    /// `fs`: Abtastrate nach aussen. Intern wird mit `os`-facher Rate gerechnet.
    pub fn new(fs: f32) -> Self {
        let os = 2;
        let mut s = Schaltung::new();
        let b2 = s.quelle("B2 +15V", 15.0);
        let trig = s.quelle("TRIG", 0.0);

        // --- Huellkurven-/Schaltteil: Q41, Q42, Q43
        let b41 = s.knoten("Q41 Basis");
        let c38n = s.knoten("C38 / Q42 Basis");
        let x2 = s.knoten("Q42 Kollektor");
        let b43 = s.knoten("Q43 Basis");
        let m = s.knoten("Q43 Kollektor (R165/R166)");
        let rk = s.knoten("C39/D52/R161");
        s.r(trig, b41, 8_200.0); // R157
        s.r(b41, MASSE, 2_700.0); // R158
        s.npn(b41, c38n, MASSE, NPN_IS, NPN_BF, NPN_BR); // Q41
        s.r(b2, c38n, 1.0e6); // R156
        s.c(c38n, MASSE, 0.1e-6); // C38
        s.npn(c38n, x2, MASSE, NPN_IS, NPN_BF, NPN_BR); // Q42
        s.r(b2, x2, 22_000.0); // R160
        s.r(x2, b43, 100_000.0); // R159
        s.npn(b43, m, MASSE, NPN_IS, NPN_BF, NPN_BR); // Q43
        s.c(x2, rk, 0.033e-6); // C39
        s.diode(rk, MASSE, D_IS, 1.0); // D52

        // --- Impulsformer: C40 || R163, R162, D53
        let p = s.knoten("IC12a + (Impuls)");
        s.c(trig, p, 0.015e-6); // C40
        s.r(trig, p, 100_000.0); // R163
        s.r(p, MASSE, 4_700.0); // R162
        s.diode(MASSE, p, D_IS, 1.0); // D53

        // --- Bridged-T-Resonator um IC12a
        let n = s.knoten("IC12a -");
        let x = s.knoten("Bridged-T Mitte");
        let o = s.knoten("IC12a Ausgang");
        let c41 = s.c(n, x, 0.015e-6); // C41
        let c42 = s.c(x, o, 0.015e-6); // C42
        s.r(x, m, 6_800.0); // R166
        s.r(m, MASSE, 47_000.0); // R165
        s.r(n, o, 1.0e6); // R167
        s.r(rk, x, 1.0e6); // R161
        s.opamp_begrenzt(p, n, o, OP_GRENZE); // IC12a (uPC4558)

        // --- Rueckkopplungspuffer IC12b mit Decay-Poti VR6
        let fn_ = s.knoten("IC12b -");
        let fm = s.knoten("VR6/C43");
        let f = s.knoten("IC12b Ausgang");
        s.r(o, fn_, 47_000.0); // R164
        s.r(fn_, f, 47_000.0); // R169
        let r_vr6 = s.r(fn_, fm, 250_000.0); // VR6 500k (B)
        s.c(fm, f, 33.0e-6); // C43
        s.opamp_begrenzt(MASSE, fn_, f, OP_GRENZE); // IC12b (+ ueber R168 an Masse)
        s.r(f, x, 470_000.0); // R170

        // --- Tone, Level
        let t1 = s.knoten("nach R171");
        let l = s.knoten("C45");
        let w = s.knoten("Level");
        s.r(o, t1, 220.0); // R171
        let r_vr5 = s.r(t1, l, 2_500.0); // VR5 10k (C) ...
        s.r(t1, l, 10_000.0); // ... parallel R172
        s.c(l, MASSE, 0.1e-6); // C45
        s.c(l, w, 0.47e-6); // C47
        s.r(w, MASSE, 10_000.0); // VR4 10k (ganz aufgedreht; Pegel digital)
        let q44 = s.knoten("Q44 Basis");
        let c49m = s.knoten("C49/R177");
        s.c(w, c49m, 0.47e-6); // C49
        s.r(c49m, q44, 82_000.0); // R177
        s.r(q44, MASSE, 100_000.0); // R176 (Q44 = Emitterfolger, Verstaerkung ~1)

        let fs_intern = fs as f64 * os as f64;
        s.set_schritt(1.0 / fs_intern);
        let mut bd = Self {
            s,
            trig,
            tone_wiper: l,
            pegel_knoten: q44,
            r_vr6,
            r_vr5,
            c41,
            c42,
            trig_rest: 0,
            trig_v: 4.0,
            os,
            fs_intern,
            pegel: 0.8,
        };
        bd.s.arbeitspunkt();
        bd
    }

    /// Decay 0..1 -> VR6 (500k linear). Links = kurz, rechts = lang.
    pub fn set_decay(&mut self, d: f32) {
        self.s.setze_r(self.r_vr6, 1_000.0 + 499_000.0 * d.clamp(0.0, 1.0) as f64);
    }

    /// Tone 0..1 -> VR5 (10k, C-Kurve). Rechts = hell (kleiner Widerstand).
    pub fn set_tone(&mut self, t: f32) {
        let k = (1.0 - t.clamp(0.0, 1.0)) as f64;
        let r = 10_000.0 * ((3.0 * k).exp() - 1.0) / (3.0f64.exp() - 1.0);
        self.s.setze_r(self.r_vr5, r.max(1.0));
    }

    /// Kein Regler im Original: "Mod" ueber C41/C42 (Tonhoehe ~ 1/C).
    /// 0.5 = Originalwerte 0,015 uF.
    pub fn set_tune(&mut self, t: f32) {
        let fak = (1.4f64).powf(1.0 - 2.0 * t.clamp(0.0, 1.0) as f64);
        self.s.setze_c(self.c41, 0.015e-6 * fak);
        self.s.setze_c(self.c42, 0.015e-6 * fak);
    }

    /// Trigger: 1 ms. `akzent` 0..1 -> 4 V (kein Akzent) .. 14 V.
    pub fn ausloesen(&mut self, akzent: f32) {
        self.trig_v = 4.0 + 10.0 * akzent.clamp(0.0, 1.0) as f64;
        self.trig_rest = (0.001 * self.fs_intern) as u32;
    }

    /// Ein Ausgabesample (Volt / 5 am Ausgang; 3,5 Vpp -> +/-0,35).
    #[inline]
    pub fn process(&mut self) -> f32 {
        let mut summe = 0.0;
        for _ in 0..self.os {
            let v = if self.trig_rest > 0 {
                self.trig_rest -= 1;
                self.trig_v
            } else {
                0.0
            };
            self.s.setze_quelle(self.trig, v);
            self.s.schritt();
            summe += self.s.spannung(self.pegel_knoten);
        }
        let _ = self.tone_wiper;
        (summe / self.os as f64 / 5.0) as f32 * self.pegel
    }
}
