# akt language guide

This guide describes the implemented subset. The full design specification
is in [`../design/01-syntax.md`](../design/01-syntax.md).

## Types

| Type | C representation |
|---|---|
| `int` | `int` |
| `i8..i64` | `intN_t` |
| `u8..u64` | `uintN_t` |
| `f32`, `f64` | `float`, `double` |
| `bool` | `bool` |
| `string` | `AktString` |
| `char` | `uint32_t` |
| `[T; N]` | C array |
| `Vector<T>` | `AktVec` |
| `Option<T>` | `AktOption` |
| `Result<T,E>` | `AktResult` |
| `*T`, `^T` | `T*` |
| `struct` | `typedef struct` |
| `enum` | C enum |
| `cstruct`/`cunion` | `typedef struct/union` |
| `cfn` | C function prototype |
| `cstring` | `const char*` |
| `size`/`isize` | `size_t`/`ptrdiff_t` |

`type Name = T` declares an alias; it is substituted during parsing.

`auto` infers the type from the initializer.

Top-level variables need no `static`:

```akt
i32 score = 0
f64 camera_x = 0.0
bool running = true
```

These are ordinary globals, like in C.

## Expressions

- arithmetic: `+ - * / % **`
- comparison: `== != < > <= >=`
- logic: `&& || !`, `and or not`
- ternary: `cond ? a : b`
- option fallback: `value ?? default`
- casts: `x as i32`
- calls, member access, indexing

F-strings:

```akt
println(f"user={user.name} age={user.age}")
```

## Statements

```akt
if (cond) { ... } else { ... }
while (cond) { ... }
loop { ... }
for (i32 i = 0; i < 10; i += 1) { ... }
for (i in 0..10) { ... }
for (i in 0..=10 step 2) { ... }
for (x in collection) { ... }
for (idx, x in collection) { ... }
```

## Functions

```akt
fn add(int a, int b) int { return a + b }
fn greet(string name) { println(f"hello {name}") }
```

`pub` is accepted; cross-module visibility is not enforced yet.

## Structs

```akt
struct User {
    string name
    i32 age

    fn birthday() { self.age += 1 }

    static fn guest() User {
        return User { name: "guest", age: 0 }
    }
}
```

`impl User { ... }` adds methods to an existing struct.

## Enums and switch

```akt
enum Color { Red, Green, Blue }

switch (c) {
    case Color::Red:
        println("red")
    default:
        println("other")
}
```

Payload enums and switch expressions work:

```akt
enum Shape {
    Circle(f64)
    Rect(f64, f64)
}

fn area(Shape s) f64 {
    return switch (s) {
        case Shape::Circle(r): 3.14159 * r * r
        case Shape::Rect(w, h): w * h
    }
}
```

## Option and Result

```akt
fn find(int id) Option<string> {
    if (id == 1) { return Some("one") }
    return None
}

fn divide(int a, int b) Result<int, string> {
    if (b == 0) { return Err("zero") }
    return Ok(a / b)
}

fn plus_one(int a, int b) Result<int, string> {
    int x = divide(a, b)?
    return Ok(x + 1)
}
```

Pattern matching:

```akt
switch (find(1)) {
    case Some(name): println(name)
    case None: println("not found")
}
```

`??` provides a default for `Option`.

## Async

```akt
async fn fetch(int id) int {
    await sleep_ms(1)
    return id * 10
}
```

Async functions compile to a worker plus an `AktTask` executed on a
separate thread; `await` waits and extracts the result.

```akt
auto all = await async.all([fetch(1), fetch(2)])
int first = await async.select([fetch(1), fetch(2)])
```

`async.timeout` is not implemented yet.

## Closures

Closures work in `Vector.filter`, `Vector.map`, `Vector.reduce` and
capture local variables lexically:

```akt
int limit = 3
auto big = nums.filter(|x| x > limit)
auto squares = nums.map(|x| x * x)
int total = nums.reduce(0, |acc, x| acc + x)
```

Closures as standalone values and `thread.spawn` capture are next.

## Managed objects

```akt
managed struct Buffer {
    i32 len
}

fn main() int {
    Buffer b = Buffer::new(4)
    println(f"{b.len}")
    return 0
}
```

`managed` structs are heap objects with an atomic refcount. The compiler
releases them at scope exit and moves them on `return`; copying or
passing them by value is rejected, and managed fields are forbidden so
that cycles (and leaks) cannot happen. No `defer` is required.

## Memory

- Local values are ordinary C values.
- `string` is a pointer + length.
- `Vector` grows on the heap.
- `arena name` creates an arena freed at scope exit.
- `defer` runs LIFO on block exit and `return`.
- `shared T` is an atomic-refcount heap cell, released via `defer`.
- `weak T` is a non-owning reference to a shared cell. `w.upgrade()`
  returns `Option<T>`: `Some` with a snapshot while the cell is alive,
  `None` once all strong references are gone.
- `manual` uses explicit ownership + `defer`.
- Runtime allocations are tracked and freed by `akt_runtime_shutdown()`.

```akt
shared i32 counter = 0
counter += 1

shared Mutex m = Mutex::new()
m.lock()
m.unlock()

weak i32 w = counter
switch (w.upgrade()) {
    case Some(v): println(f"alive = {v}")
    case None: println("expired")
}
```

Returning a reference to a local or one of its fields (`return ^local`,
`return ^local.field`) is rejected by the compiler.

## Modules

`use a.b` loads `a/b.akt` relative to `src/`. Modules share one global
namespace; names must be unique. `module.fn()` resolves to `fn()`.

## C interop

```akt
cinclude <stdio.h>
clink "m"

cfn strlen(*const cchar s) size

cstruct Pair {
    i32 a
    i32 b
}

fn main() int {
    c.printf("hello from C\n")
    Pair p = Pair { a: 1, b: 2 }
    return 0
}
```

`cinclude` adds an include and imports C declarations at compile time:
typedefs, structs/unions, enums, numeric `#define`s and prototypes.
There is no separate bindgen step; generated C and header caches live
in the project's `.kache/` directory. `c.fn(args)` calls any C function;
string arguments are passed as `.data`. `cfn`/`cstruct`/`cunion` remain
as a manual escape hatch when no header exists. `cimport "lib.h"` also
compiles the sibling `lib.c`, for header+source libraries such as
sokol. For manually declared structs that are also in a header the
compiler emits `_Static_assert(sizeof(T) == N, ...)`.

## Limitations

- closures as values are limited to HOFs and `thread.spawn`
- no generics beyond `Vector<T>`
- no full `shared`/`weak`/`manual` lifetime analysis
- no real async executor (tasks are thread-backed)
- no automatic C dependency providers
