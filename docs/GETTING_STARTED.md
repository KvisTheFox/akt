# Getting started with akt

akt is a compiled systems language with C-like syntax, predictable memory
(no GC), `defer`, arenas, and first-class C interoperability.

## Build

Windows:

```bash
xmake f -c --toolchain=clang-cl -m debug
xmake build
```

Linux/macOS:

```bash
xmake f -c -m release
xmake build
```

Or CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Binaries are placed in `bin/`.

## Hello

```akt
fn main() int {
    println("Hello, World!")
    return 0
}
```

```bash
bin/aktc run hello.akt
```

## Variables

```akt
int age = 42
f64 price = 19.99
string name = "akt"
bool ok = true
auto inferred = 10
```

## Control flow

```akt
if (x > 0) {
    println("positive")
} else {
    println("non-positive")
}

for (i in 0..5) {
    print(f"{i} ")
}

while (n < 3) {
    n += 1
}
```

## Functions

```akt
fn add(int a, int b) int {
    return a + b
}
```

## Structs

```akt
struct Point {
    f64 x
    f64 y

    fn length() f64 {
        return sqrt(self.x * self.x + self.y * self.y)
    }

    static fn origin() Point {
        return Point { x: 0.0, y: 0.0 }
    }
}
```

## Arrays and vectors

```akt
[i32; 3] fixed = [1, 2, 3]
Vector<string> names = []
names.push("Alice")
println(f"{names.len()} {names[0]}")
```

## Errors

```akt
fn divide(int a, int b) Result<int, string> {
    if (b == 0) { return Err("division by zero") }
    return Ok(a / b)
}

fn main() Result<void, string> {
    int x = divide(10, 2)?
    println(f"{x}")
    return Ok(())
}
```

## Modules

```akt
// src/math.akt
pub fn add(int a, int b) int { return a + b }

// src/main.akt
use math

fn main() int {
    println(f"{math.add(2, 3)}")
    return 0
}
```

## Project

```bash
katek new hello
cd hello
katek run
katek test
katek doctor
```

## Next

- [Language guide](LANGUAGE.md)
- [Standard library](STDLIB.md)
- [CLI](CLI.md)
- [Roadmap](ROADMAP.md)
