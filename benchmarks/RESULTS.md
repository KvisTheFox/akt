# Speed: akt vs C, C++, Rust, Zig

Best of 3 runs, Windows x64, clang -O3 -march=native, rustc -O, zig -O ReleaseFast.

| test | akt | C | C++ | Rust | Zig | akt/C |
|---|---:|---:|---:|---:|---:|---:|
| fib | 94.2 | 84.4 | 93.4 | 91.1 | 92.6 | 1.12x |
| loop | 147.8 | 141.0 | 145.0 | 228.1 | 149.0 | 1.05x |
| sieve | 105.1 | 98.0 | 97.9 | 95.9 | 110.0 | 1.07x |
| matmul | 81.3 | 87.8 | 85.0 | 80.4 | 96.1 | 0.93x |
| hash | 135.0 | 116.2 | 115.9 | 132.4 | 125.5 | 1.16x |
