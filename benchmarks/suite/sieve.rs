const N: usize = 5000000;
fn main() {
    let mut sieve = vec![1u8; N];
    sieve[0] = 0; sieve[1] = 0;
    let mut i = 2usize;
    while i * i < N {
        if sieve[i] == 1 {
            let mut j = i * i;
            while j < N { sieve[j] = 0; j += i; }
        }
        i += 1;
    }
    let count: i64 = sieve.iter().map(|&v| v as i64).sum();
    println!("sieve {}", count);
}
