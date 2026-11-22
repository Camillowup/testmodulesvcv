use drumkern::va808_bd::Bd808Va;
fn main() {
    let fs = 48_000.0f32;
    let mut bd = Bd808Va::new(fs);
    bd.pegel = 1.0; bd.set_decay(0.5); bd.set_tone(0.5);
    for _ in 0..4800 { bd.process(); }
    bd.ausloesen(0.0);
    for _ in 0..(fs*0.4) as usize { println!("{}", bd.process()); }
}
