use drumkern::va808_bd::Bd808Va;
use std::time::Instant;
fn main() {
    let fs = 48_000.0f32;
    for (decay, akz) in [(0.0f32, 0.0f32), (0.5, 0.0), (1.0, 0.0), (0.5, 1.0)] {
        let mut bd = Bd808Va::new(fs);
        bd.pegel = 1.0;
        bd.set_decay(decay);
        bd.set_tone(0.5);
        // einschwingen (Arbeitspunkt nach Potiaenderung)
        for _ in 0..4800 { bd.process(); }
        bd.ausloesen(akz);
        let t0 = Instant::now();
        let v: Vec<f32> = (0..(fs * 2.0) as usize).map(|_| bd.process()).collect();
        let dt = t0.elapsed().as_secs_f32();
        let spitze = v.iter().fold(0f32, |a, b| a.max(b.abs()));
        let maxv = v.iter().cloned().fold(f32::MIN, f32::max);
        let minv = v.iter().cloned().fold(f32::MAX, f32::min);
        let letzt = v.iter().rposition(|x| x.abs() >= spitze / 10.0).unwrap();
        // Frequenz 50..250 ms
        let (a, b) = ((0.05 * fs) as usize, (0.25 * fs) as usize);
        let mut z = vec![];
        for i in a..b { if v[i-1] < 0.0 && v[i] >= 0.0 { z.push(i); } }
        let f = if z.len() > 1 { (z.len() - 1) as f32 / ((z[z.len()-1] - z[0]) as f32 / fs) } else { 0.0 };
        // Frequenz 0..6 ms (Attack)
        println!("decay {:.1} akzent {:.0}: Vpp {:.2} V, t10 {:.0} ms, f {:.1} Hz, Rechenzeit {:.2} s fuer 2 s Audio",
            decay, akz, (maxv - minv) * 10.0, letzt as f32 / fs * 1000.0, f, dt);
    }
}
