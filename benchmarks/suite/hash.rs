const N: usize = 16777216;
fn main() {
    let mut data = vec![0u8; N];
    for (i, v) in data.iter_mut().enumerate() { *v = ((i as u64 * 31 + 7) % 251) as u8; }
    let mut h: u64 = 1469598103934665603;
    for &v in &data {
        h ^= v as u64;
        h = h.wrapping_mul(1099511628211);
    }
    println!("hash {}", h % 1000000007);
}
