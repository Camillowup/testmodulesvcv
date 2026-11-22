//! Kleiner Schaltungsloeser fuer Virtual Analog (wie SPICE, nur winzig):
//! modifizierte Knotenanalyse (MNA), Kondensatoren mit Trapezregel,
//! Dioden und Transistoren (Ebers-Moll) per Newton-Verfahren mit
//! Spannungsbegrenzung, ideale Operationsverstaerker.
//!
//! Knoten 0 ist Masse. Knoten mit fester Spannung (Versorgung, Trigger)
//! werden als "Quelle" angelegt und pro Schritt gesetzt.

const VT: f64 = 0.025_85;
const GMIN: f64 = 1.0e-12;

#[derive(Clone, Copy, PartialEq, Debug)]
pub struct Knoten(pub usize);
pub const MASSE: Knoten = Knoten(0);

#[derive(Clone)]
enum Bauteil {
    R { a: usize, b: usize, g: f64 },
    C { a: usize, b: usize, c: f64, v: f64, i: f64 },
    D { an: usize, ka: usize, is: f64, nvt: f64, vd: f64 },
    Npn { b: usize, c: usize, e: usize, is: f64, bf: f64, br: f64, vbe: f64, vbc: f64 },
    /// sat: 0 = linear, +1/-1 = Ausgang klebt an der Versorgungsgrenze
    Op { plus: usize, minus: usize, aus: usize, zeile: usize, grenze: f64, sat: i8 },
}

pub struct Schaltung {
    teile: Vec<Bauteil>,
    namen: Vec<String>,
    /// Feste Spannung pro Knoten (None = unbekannt)
    fest: Vec<Option<f64>>,
    /// Knoten -> Zeile im Gleichungssystem
    zeile: Vec<Option<usize>>,
    n: usize,
    v: Vec<f64>,
    a: Vec<f64>,
    rhs: Vec<f64>,
    x: Vec<f64>,
    h: f64,
    fertig: bool,
    pub max_iter: usize,
}

impl Schaltung {
    pub fn new() -> Self {
        Self {
            teile: Vec::new(),
            namen: vec!["0".into()],
            fest: vec![Some(0.0)],
            zeile: vec![None],
            n: 0,
            v: vec![0.0],
            a: Vec::new(),
            rhs: Vec::new(),
            x: Vec::new(),
            h: 1.0e-5,
            fertig: false,
            max_iter: 60,
        }
    }

    pub fn knoten(&mut self, name: &str) -> Knoten {
        self.namen.push(name.into());
        self.fest.push(None);
        self.zeile.push(None);
        self.v.push(0.0);
        Knoten(self.namen.len() - 1)
    }

    pub fn quelle(&mut self, name: &str, spannung: f64) -> Knoten {
        let k = self.knoten(name);
        self.fest[k.0] = Some(spannung);
        self.v[k.0] = spannung;
        k
    }

    pub fn setze_quelle(&mut self, k: Knoten, spannung: f64) {
        self.fest[k.0] = Some(spannung);
        self.v[k.0] = spannung;
    }

    pub fn r(&mut self, a: Knoten, b: Knoten, ohm: f64) -> usize {
        self.teile.push(Bauteil::R { a: a.0, b: b.0, g: 1.0 / ohm });
        self.teile.len() - 1
    }

    /// Widerstandswert eines vorhandenen R aendern (Potis).
    pub fn setze_r(&mut self, id: usize, ohm: f64) {
        if let Bauteil::R { g, .. } = &mut self.teile[id] {
            *g = 1.0 / ohm.max(1.0e-3);
        }
    }

    pub fn c(&mut self, a: Knoten, b: Knoten, farad: f64) -> usize {
        self.teile.push(Bauteil::C { a: a.0, b: b.0, c: farad, v: 0.0, i: 0.0 });
        self.teile.len() - 1
    }

    pub fn setze_c(&mut self, id: usize, farad: f64) {
        if let Bauteil::C { c, .. } = &mut self.teile[id] {
            *c = farad;
        }
    }

    pub fn diode(&mut self, anode: Knoten, kathode: Knoten, is: f64, n: f64) {
        self.teile.push(Bauteil::D { an: anode.0, ka: kathode.0, is, nvt: n * VT, vd: 0.0 });
    }

    pub fn npn(&mut self, b: Knoten, c: Knoten, e: Knoten, is: f64, bf: f64, br: f64) {
        self.teile.push(Bauteil::Npn { b: b.0, c: c.0, e: e.0, is, bf, br, vbe: 0.0, vbc: 0.0 });
    }

    /// Idealer Operationsverstaerker (v+ = v-, Ausgang liefert beliebigen Strom).
    pub fn opamp(&mut self, plus: Knoten, minus: Knoten, aus: Knoten) {
        self.teile.push(Bauteil::Op { plus: plus.0, minus: minus.0, aus: aus.0, zeile: 0, grenze: 1.0e9, sat: 0 });
    }

    /// Wie `opamp`, aber der Ausgang kann nur bis +/- `grenze` Volt (z. B. 4558 an +/-15 V: ~13,5 V).
    pub fn opamp_begrenzt(&mut self, plus: Knoten, minus: Knoten, aus: Knoten, grenze: f64) {
        self.teile.push(Bauteil::Op { plus: plus.0, minus: minus.0, aus: aus.0, zeile: 0, grenze, sat: 0 });
    }

    pub fn spannung(&self, k: Knoten) -> f64 {
        self.v[k.0]
    }

    fn vorbereiten(&mut self) {
        let mut n = 0;
        for k in 0..self.namen.len() {
            if self.fest[k].is_none() {
                self.zeile[k] = Some(n);
                n += 1;
            }
        }
        for t in self.teile.iter_mut() {
            if let Bauteil::Op { zeile, .. } = t {
                *zeile = n;
                n += 1;
            }
        }
        self.n = n;
        self.a = vec![0.0; n * n];
        self.rhs = vec![0.0; n];
        self.x = vec![0.0; n];
        self.fertig = true;
    }

    // ---- Stempel-Helfer: Leitwert g zwischen Knoten p und q
    #[inline]
    fn leitwert(&mut self, p: usize, q: usize, g: f64) {
        let (zp, zq) = (self.zeile[p], self.zeile[q]);
        let n = self.n;
        if let Some(i) = zp {
            self.a[i * n + i] += g;
            match zq {
                Some(j) => self.a[i * n + j] -= g,
                None => self.rhs[i] += g * self.v[q],
            }
        }
        if let Some(j) = zq {
            self.a[j * n + j] += g;
            match zp {
                Some(i) => self.a[j * n + i] -= g,
                None => self.rhs[j] += g * self.v[p],
            }
        }
    }

    /// Strom i fliesst von p nach q durch das Bauteil (verlaesst p).
    #[inline]
    fn strom(&mut self, p: usize, q: usize, i: f64) {
        if let Some(r) = self.zeile[p] {
            self.rhs[r] -= i;
        }
        if let Some(r) = self.zeile[q] {
            self.rhs[r] += i;
        }
    }

    /// Steilheit: Strom in Knoten `in_` (verlaesst ihn) haengt ab von v(cp)-v(cn) mit g.
    #[inline]
    fn steil(&mut self, aus: usize, cp: usize, cn: usize, g: f64) {
        let n = self.n;
        if let Some(r) = self.zeile[aus] {
            match self.zeile[cp] {
                Some(j) => self.a[r * n + j] += g,
                None => self.rhs[r] -= g * self.v[cp],
            }
            match self.zeile[cn] {
                Some(j) => self.a[r * n + j] -= g,
                None => self.rhs[r] += g * self.v[cn],
            }
        }
    }

    fn stempeln(&mut self, dc: bool) {
        for x in self.a.iter_mut() {
            *x = 0.0;
        }
        for x in self.rhs.iter_mut() {
            *x = 0.0;
        }
        let h = self.h;
        let teile = std::mem::take(&mut self.teile);
        for t in teile.iter() {
            match *t {
                Bauteil::R { a, b, g } => self.leitwert(a, b, g),
                Bauteil::C { a, b, c, v, i } => {
                    if dc {
                        self.leitwert(a, b, GMIN);
                    } else {
                        let geq = 2.0 * c / h;
                        self.leitwert(a, b, geq);
                        // Strom a->b: i = geq*v_neu - (geq*v + i)
                        self.strom(a, b, -(geq * v + i));
                    }
                }
                Bauteil::D { an, ka, is, nvt, vd } => {
                    let e = (vd / nvt).min(80.0).exp();
                    let id = is * (e - 1.0);
                    let gd = is / nvt * e + GMIN;
                    self.leitwert(an, ka, gd);
                    self.strom(an, ka, id - gd * vd);
                }
                Bauteil::Npn { b, c, e, is, bf, br, vbe, vbc } => {
                    let ef = (vbe / VT).min(80.0).exp();
                    let er = (vbc / VT).min(80.0).exp();
                    let gf = is / VT * ef;
                    let gr = is / VT * er;
                    // Klemmenstroeme (in das Bauteil hinein)
                    let ic = is * (ef - er) - is / br * (er - 1.0);
                    let ib = is / bf * (ef - 1.0) + is / br * (er - 1.0);
                    let dic_be = gf;
                    let dic_bc = -gr - gr / br;
                    let dib_be = gf / bf + GMIN;
                    let dib_bc = gr / br + GMIN;
                    let die_be = -(dic_be + dib_be);
                    let die_bc = -(dic_bc + dib_bc);
                    let ie = -(ic + ib);
                    // I = I0 + d_be*(vbe - vbe0) + d_bc*(vbc - vbc0)
                    for (k, i0, dbe, dbc) in [(c, ic, dic_be, dic_bc), (b, ib, dib_be, dib_bc), (e, ie, die_be, die_bc)] {
                        self.steil(k, b, e, dbe);
                        self.steil(k, b, c, dbc);
                        let konst = i0 - dbe * vbe - dbc * vbc;
                        if let Some(r) = self.zeile[k] {
                            self.rhs[r] -= konst;
                        }
                    }
                }
                Bauteil::Op { plus, minus, aus, zeile, grenze, sat } => {
                    let n = self.n;
                    // Ausgangsstrom j fliesst in den Knoten aus
                    if let Some(r) = self.zeile[aus] {
                        self.a[r * n + zeile] -= 1.0;
                    }
                    if sat != 0 {
                        // Ausgang an der Grenze festhalten: v(aus) = +/-grenze
                        match self.zeile[aus] {
                            Some(j) => self.a[zeile * n + j] += 1.0,
                            None => {}
                        }
                        self.rhs[zeile] += sat as f64 * grenze;
                        continue;
                    }
                    // Bedingung v+ - v- = 0
                    match self.zeile[plus] {
                        Some(j) => self.a[zeile * n + j] += 1.0,
                        None => self.rhs[zeile] -= self.v[plus],
                    }
                    match self.zeile[minus] {
                        Some(j) => self.a[zeile * n + j] -= 1.0,
                        None => self.rhs[zeile] += self.v[minus],
                    }
                }
            }
        }
        self.teile = teile;
    }

    fn loesen(&mut self) -> bool {
        let n = self.n;
        let a = &mut self.a;
        let b = &mut self.rhs;
        for k in 0..n {
            let mut p = k;
            let mut m = a[k * n + k].abs();
            for i in k + 1..n {
                let w = a[i * n + k].abs();
                if w > m {
                    m = w;
                    p = i;
                }
            }
            if m < 1.0e-30 {
                return false;
            }
            if p != k {
                for j in 0..n {
                    a.swap(k * n + j, p * n + j);
                }
                b.swap(k, p);
            }
            let piv = a[k * n + k];
            for i in k + 1..n {
                let f = a[i * n + k] / piv;
                if f != 0.0 {
                    for j in k..n {
                        a[i * n + j] -= f * a[k * n + j];
                    }
                    b[i] -= f * b[k];
                }
            }
        }
        for k in (0..n).rev() {
            let mut s = b[k];
            for j in k + 1..n {
                s -= a[k * n + j] * self.x[j];
            }
            self.x[k] = s / a[k * n + k];
        }
        true
    }

    /// SPICE-Spannungsbegrenzung fuer pn-Uebergaenge (Konvergenz).
    fn pnjlim(neu: f64, alt: f64, nvt: f64, vkrit: f64) -> f64 {
        if neu > vkrit && (neu - alt).abs() > 2.0 * nvt {
            if alt > 0.0 {
                let arg = 1.0 + (neu - alt) / nvt;
                if arg > 0.0 {
                    alt + nvt * arg.ln()
                } else {
                    vkrit
                }
            } else {
                nvt * (neu / nvt).ln()
            }
        } else {
            neu
        }
    }

    fn newton(&mut self, dc: bool) -> bool {
        if !self.fertig {
            self.vorbereiten();
        }
        let iter = if dc { 500 } else { self.max_iter };
        for _ in 0..iter {
            self.stempeln(dc);
            if !self.loesen() {
                return false;
            }
            let mut fehler: f64 = 0.0;
            for k in 0..self.namen.len() {
                if let Some(r) = self.zeile[k] {
                    fehler = fehler.max((self.x[r] - self.v[k]).abs());
                    self.v[k] = self.x[r];
                }
            }
            // Operationsverstaerker: in die Saettigung bzw. wieder heraus
            let mut umgeschaltet = false;
            {
                let v = &self.v;
                let x = &self.x;
                for t in self.teile.iter_mut() {
                    if let Bauteil::Op { plus, minus, aus, zeile, grenze, sat } = t {
                        if *sat == 0 {
                            if v[*aus] > *grenze {
                                *sat = 1;
                                umgeschaltet = true;
                            } else if v[*aus] < -*grenze {
                                *sat = -1;
                                umgeschaltet = true;
                            }
                        } else {
                            // Differenzspannung zeigt wieder nach innen -> linear
                            let _ = zeile;
                            let diff = v[*plus] - v[*minus];
                            if diff * (*sat as f64) < 0.0 {
                                *sat = 0;
                                umgeschaltet = true;
                            }
                        }
                        let _ = x;
                    }
                }
            }
            // Arbeitspunkte der pn-Uebergaenge nachfuehren (mit Begrenzung)
            let mut begrenzt = umgeschaltet;
            let v = &self.v;
            for t in self.teile.iter_mut() {
                match t {
                    Bauteil::D { an, ka, is, nvt, vd } => {
                        let neu = v[*an] - v[*ka];
                        let vk = *nvt * (*nvt / (std::f64::consts::SQRT_2 * *is)).ln();
                        let lim = Self::pnjlim(neu, *vd, *nvt, vk);
                        begrenzt |= (lim - neu).abs() > 1.0e-9;
                        *vd = lim;
                    }
                    Bauteil::Npn { b, c, e, is, vbe, vbc, .. } => {
                        let vk = VT * (VT / (std::f64::consts::SQRT_2 * *is)).ln();
                        let nbe = v[*b] - v[*e];
                        let nbc = v[*b] - v[*c];
                        let lbe = Self::pnjlim(nbe, *vbe, VT, vk);
                        let lbc = Self::pnjlim(nbc, *vbc, VT, vk);
                        begrenzt |= (lbe - nbe).abs() > 1.0e-9 || (lbc - nbc).abs() > 1.0e-9;
                        *vbe = lbe;
                        *vbc = lbc;
                    }
                    _ => {}
                }
            }
            if fehler < 1.0e-7 && !begrenzt {
                return true;
            }
        }
        false
    }

    /// Gleichstrom-Arbeitspunkt (Kondensatoren offen), setzt Kondensator-Zustaende.
    pub fn arbeitspunkt(&mut self) -> bool {
        let ok = self.newton(true);
        let v = self.v.clone();
        for t in self.teile.iter_mut() {
            if let Bauteil::C { a, b, v: vc, i, .. } = t {
                *vc = v[*a] - v[*b];
                *i = 0.0;
            }
        }
        ok
    }

    pub fn set_schritt(&mut self, h: f64) {
        self.h = h;
    }

    /// Einen Zeitschritt h rechnen.
    pub fn schritt(&mut self) -> bool {
        let ok = self.newton(false);
        let h = self.h;
        let v = &self.v;
        for t in self.teile.iter_mut() {
            if let Bauteil::C { a, b, c, v: vc, i } = t {
                let neu = v[*a] - v[*b];
                let geq = 2.0 * *c / h;
                *i = geq * (neu - *vc) - *i;
                *vc = neu;
            }
        }
        ok
    }
}

impl Default for Schaltung {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rc_tiefpass_laedt_richtig() {
        // 10k / 1uF, tau = 10 ms; nach 10 ms ~63 %
        let mut s = Schaltung::new();
        let q = s.quelle("q", 0.0);
        let k = s.knoten("k");
        s.r(q, k, 10_000.0);
        s.c(k, MASSE, 1.0e-6);
        s.arbeitspunkt();
        s.setze_quelle(q, 1.0);
        s.set_schritt(1.0e-5);
        for _ in 0..1000 {
            s.schritt();
        }
        assert!((s.spannung(k) - 0.632).abs() < 0.005, "{}", s.spannung(k));
    }

    #[test]
    fn invertierender_verstaerker() {
        let mut s = Schaltung::new();
        let q = s.quelle("q", 0.5);
        let m = s.knoten("minus");
        let o = s.knoten("aus");
        s.r(q, m, 10_000.0);
        s.r(m, o, 47_000.0);
        s.opamp(MASSE, m, o);
        s.arbeitspunkt();
        assert!((s.spannung(o) + 2.35).abs() < 1.0e-6);
    }

    #[test]
    fn transistor_schaltet() {
        // NPN mit 100k an der Basis, 22k am Kollektor an 15 V: gesaettigt
        let mut s = Schaltung::new();
        let vcc = s.quelle("vcc", 15.0);
        let ein = s.quelle("ein", 5.0);
        let b = s.knoten("b");
        let c = s.knoten("c");
        s.r(ein, b, 100_000.0);
        s.r(vcc, c, 22_000.0);
        s.npn(b, c, MASSE, 1.0e-14, 200.0, 5.0);
        assert!(s.arbeitspunkt());
        assert!(s.spannung(c) < 0.3, "Uce = {}", s.spannung(c));
        assert!((s.spannung(b) - 0.65).abs() < 0.15, "Ube = {}", s.spannung(b));
    }
}
