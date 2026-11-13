const D: usize = 256;
fn main() {
    let mut a = vec![0f64; D * D];
    let mut b = vec![0f64; D * D];
    let mut c = vec![0f64; D * D];
    for i in 0..D * D {
        a[i] = (i % 13) as f64;
        b[i] = (i % 7) as f64;
    }
    for r in 0..D {
        for k in 0..D {
            let av = a[r * D + k];
            for col in 0..D { c[r * D + col] += av * b[k * D + col]; }
        }
    }
    let sum: f64 = c.iter().sum();
    println!("matmul {:.2}", sum);
}
