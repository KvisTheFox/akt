fn main() {
    let mut sum: i64 = 0;
    for i in 0..100_000_000i64 { sum += (i * 3 + 1) % 7; }
    println!("loop {}", sum);
}
