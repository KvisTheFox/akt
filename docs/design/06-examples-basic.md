# akt — базовые примеры

Все примеры — обычные модули `.akt`. Объявления типов и функций
находятся **только на уровне модуля**; внутри `main` допустимы лишь
переменные и инструкции.

Запуск:

```bash
katek run              # проект целиком
katek run examples/basic/hello.akt
```

---

## 1. Hello, World

```akt
// examples/basic/hello.akt
fn main() int {
    println("Hello, World!")
    return 0
}
```

```bash
$ katek run examples/basic/hello.akt
Hello, World!
```

`println` встроен и не требует `use`.

---

## 2. Аргументы командной строки

```akt
// examples/basic/args.akt
use std.env

fn main() int {
    for (i, arg in env.args()) {
        println(f"arg[{i}] = {arg}")
    }
    return 0
}
```

Альтернативная C-совместимая форма точки входа:

```akt
fn main(i32 argc, *const *const cchar argv) int {
    for (i32 i = 0; i < argc; i += 1) {
        println(f"arg[{i}] = {argv[i]}")
    }
    return 0
}
```

Не смешивайте обе формы в одном бинарнике.

---

## 3. Факториал

```akt
// examples/basic/factorial.akt
const fn factorial(int n) int {
    if (n <= 1) {
        return 1
    }
    return n * factorial(n - 1)
}

fn main() int {
    for (i in 1..=10) {
        println(f"{i}! = {factorial(i)}")
    }
    return 0
}
```

`const fn` вычисляется на этапе компиляции для константных
аргументов. Для больших `n` используйте `i64`/`u64`, чтобы не
переполнить `int`.

---

## 4. Числа Фибоначчи

```akt
// examples/basic/fib.akt
fn fib(int n) int {
    if (n < 2) {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

fn main() int {
    for (i in 0..10) {
        print(f"{fib(i)} ")
    }
    println("")
    return 0
}
```

Наивная рекурсия экспоненциальна. Для реального кода используйте
итеративный вариант или мемоизацию.

---

## 5. FizzBuzz

```akt
// examples/basic/fizzbuzz.akt
fn fizzbuzz(int n) string {
    if (n % 15 == 0) { return "FizzBuzz" }
    if (n % 3 == 0)  { return "Fizz" }
    if (n % 5 == 0)  { return "Buzz" }
    return f"{n}"
}

fn main() int {
    for (i in 1..=100) {
        println(fizzbuzz(i))
    }
    return 0
}
```

`f"{n}"` создаёт `string` в текущем регионе; возвращать его из
функции безопасно, пока жив регион вызывающего.

---

## 6. Struct, методы и ассоциированная функция

```akt
// examples/basic/rectangle.akt
struct Rectangle {
    f64 width
    f64 height

    fn area() f64 {
        return self.width * self.height
    }

    fn perimeter() f64 {
        return 2.0 * (self.width + self.height)
    }

    static fn square(f64 side) Rectangle {
        return Rectangle { width: side, height: side }
    }
}

fn main() int {
    Rectangle r = { width: 3.0, height: 4.0 }
    println(f"area = {r.area()}")
    println(f"perimeter = {r.perimeter()}")

    Rectangle s = Rectangle::square(5.0)
    println(f"square area = {s.area()}")
    return 0
}
```

`static fn` не имеет `self`; вызов — через `Type::name()`.
`::` здесь разрешён, потому что это ассоциированная функция.

---

## 7. Enum и `switch`-выражение

```akt
// examples/basic/direction.akt
enum Direction { North, South, East, West }

fn turn_right(Direction d) Direction {
    return switch (d) {
        case Direction::North: Direction::East
        case Direction::East:  Direction::South
        case Direction::South: Direction::West
        case Direction::West:  Direction::North
    }
}

fn main() int {
    Direction d = Direction::North
    for (i in 0..4) {
        println(f"dir = {d}")
        d = turn_right(d)
    }
    return 0
}
```

`switch`-выражение требует покрытия всех вариантов. Fallthrough
отсутствует, `break` не нужен.

---

## 8. `Vector` и функции высшего порядка

```akt
// examples/basic/collections.akt
use std.collections

fn main() int {
    Vector<i32> nums = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]

    auto evens = nums.filter(|x| x % 2 == 0)
    auto squared = nums.map(|x| x * x)
    auto total = nums.reduce(0, |acc, x| acc + x)

    println(f"evens   = {evens}")
    println(f"squared = {squared}")
    println(f"total   = {total}")
    return 0
}
```

`filter`/`map` возвращают новый `Vector`, а не ленивый итератор.
Для ленивых цепочек используйте `iter()` и `std.iter`.

---

## 9. `Option`, `Result` и `?`

```akt
// examples/basic/options.akt
use std.collections

fn find_user(int id) Option<string> {
    if (id == 1) { return Some("Alice") }
    if (id == 2) { return Some("Bob") }
    return None
}

fn divide(f64 a, f64 b) Result<f64, string> {
    if (b == 0.0) { return Err("division by zero") }
    return Ok(a / b)
}

fn average(Vector<f64> xs) Result<f64, string> {
    if (xs.len() == 0) {
        return Err("empty vector")
    }
    f64 sum = 0.0
    for (x in xs) {
        sum += x
    }
    return Ok(sum / (xs.len() as f64))
}

fn main() int {
    switch (find_user(1)) {
        case Some(name): { println(f"found: {name}") }
        case None:       { println("not found") }
    }

    switch (divide(10.0, 2.0)) {
        case Ok(v):  { println(f"result = {v}") }
        case Err(e): { println(f"error = {e}") }
    }

    // ?? работает только для Option
    println(f"user 5 = {find_user(5) ?? "unknown"}")

    switch (average([2.0, 4.0, 6.0])) {
        case Ok(avg): { println(f"avg = {avg}") }
        case Err(e):  { println(f"error = {e}") }
    }
    return 0
}
```

`?` работает только в функции, возвращающей `Result` с совместимым
типом ошибки. В `main`, возвращающем `int`, используйте `switch`
или объявите `fn main() Result<void, std.error.Error>`.

---

## 10. Чтение и запись файлов

```akt
// examples/basic/files.akt
use std.io
use std.error

fn main() Result<void, std.error.Error> {
    io.write_to_string("out.txt", "hello\nworld\n")?

    auto content = io.read_to_string("out.txt")?
    for (line in content.lines()) {
        println(line)
    }

    auto f = io.open("out.txt")?
    defer f.close()
    for (line in f.lines()) {
        println(f"line: {line}")
    }
    return Ok(())
}
```

`Ok(())` — успешное завершение функции с `Result<void, E>`.
`defer` выполняется при любом выходе, включая `?`.

---

## 11. JSON

```akt
// examples/basic/json_demo.akt
use std.json
use std.error

fn main() Result<void, std.error.Error> {
    auto data = json.parse("{\"name\":\"Alice\",\"age\":30}")?

    println(f"name = {data["name"]}")
    println(f"age  = {data["age"]}")

    auto pretty = json.stringify_pretty(data)
    println(pretty)
    return Ok(())
}
```

Модуль доступен при включённом feature-флаге:

```toml
[std]
features = ["json"]
```

Вложенные кавычки внутри `{}` в f-строке допустимы: внешняя строка
и внутренний литерал разбираются независимо.

---

## 12. HTTP-клиент

```akt
// examples/basic/http_client.akt
use std.http
use std.json
use std.error

async fn main() Result<void, std.error.Error> {
    auto client = http.Client::new()
    auto resp = await client.get("https://httpbin.org/json")?

    if (resp.status != 200) {
        println(f"error: status {resp.status}")
        return Ok(())
    }

    auto data = json.parse(resp.body)?
    println(json.stringify_pretty(data))
    return Ok(())
}
```

```toml
[std]
features = ["http", "json", "tls"]
```

`http.Client::new()` — ассоциированная функция; `::` допустим.

---

## 13. HTTP-сервер

```akt
// examples/basic/http_server.akt
use std.http
use std.error

async fn main() Result<void, std.error.Error> {
    auto server = http.Server::new()

    server.get("/", |req| {
        return http.Response::ok("Hello from akt!")
    })

    server.get("/users/{id}", |req| {
        auto id = req.params["id"]
        return http.Response::json({ id: id, name: f"User{id}" })
    })

    println("listening on :8080")
    await server.listen(8080)?
    return Ok(())
}
```

Обработчики не должны блокировать executor. Для тяжёлой работы
используйте `std.thread` или пул задач.

---

## 14. SQLite

```akt
// examples/basic/sqlite_demo.akt
use std.db.sqlite
use std.error

fn main() Result<void, std.error.Error> {
    auto conn = sqlite.open(":memory:")?

    conn.exec("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)")?
    conn.insert("INSERT INTO users (name) VALUES (?)", "Alice")?
    conn.insert("INSERT INTO users (name) VALUES (?)", "Bob")?

    for (row in conn.query("SELECT * FROM users")?) {
        println(f"{row["id"]}: {row["name"]}")
    }
    return Ok(())
}
```

```toml
[std]
features = ["sqlite"]

[native.dependencies]
sqlite3 = { provider = "xrepo", version = "3.45.0" }
```

`std.db.sqlite` — обёртка над C SQLite; линковка и include-пути
приходят из провайдера.

---

## 15. Тесты

```akt
// src/math.akt
pub fn add(int a, int b) int { return a + b }
pub fn mul(int a, int b) int { return a * b }
```

```akt
// tests/math_test.akt
use math
use std.test

test "add works" {
    assert_eq(math.add(2, 2), 4)
    assert_eq(math.add(-1, 1), 0)
}

test "mul works" {
    assert_eq(math.mul(2, 3), 6)
    assert_eq(math.mul(0, 100), 0)
}
```

```bash
$ katek test
running 2 tests from tests/math_test.akt
test add works ... ok (0.01ms)
test mul works ... ok (0.01ms)
test result: ok. 2 passed; 0 failed
```

---

## 16. Арена

```akt
// examples/basic/arena_demo.akt
use std.collections

struct Vec2 {
    f64 x
    f64 y
}

fn main() int {
    arena frame

    Vector<Vec2> frame.points = []
    for (i in 0..1000) {
        frame.points.push(Vec2 { x: i as f64, y: (i * 2) as f64 })
    }

    f64 total = 0.0
    for (p in frame.points) {
        total += p.x + p.y
    }
    println(f"total = {total}")

    // frame освобождается целиком при выходе из области видимости
    return 0
}
```

Не сохраняйте ссылки на данные арены дольше, чем живёт арена.
Компилятор проверяет escape-анализом.

---

## 17. `shared` и `Mutex`

```akt
// examples/basic/threads.akt
use std.thread
use std.sync
use std.collections
use std.error

fn main() Result<void, std.error.Error> {
    shared Mutex mtx = Mutex::new()
    shared i32 counter = 0

    Vector<thread.Handle> handles = []
    for (i in 0..4) {
        handles.push(thread.spawn(shared || {
            for (j in 0..1000) {
                mtx.lock()
                defer mtx.unlock()
                counter += 1
            }
        }))
    }

    for (h in handles) {
        thread.join(h)?
    }

    println(f"counter = {counter}")
    return Ok(())
}
```

`shared` делает **счётчик ссылок** атомарным. Он не защищает
данные от гонок. Для общего изменяемого состояния нужен `Mutex`,
`RwLock` или `Atomic<T>`.

---

## 18. `manual` и `defer`

```akt
// examples/basic/manual_file.akt
use std.io
use std.error

fn main() Result<void, std.error.Error> {
    manual io.File f = io.open("out.txt")?
    defer f.close()

    f.write_string("hello\n")?
    f.write_string("world\n")?

    println("done")
    return Ok(())
    // f.close() вызывается автоматически
}
```

`manual` означает ручное владение: объект не освобождается
регионом. `defer` гарантирует освобождение на всех путях выхода.

---

## 19. Перегрузка и fallback

```akt
// examples/basic/overload.akt
fn describe(int x) string    { return f"int: {x}" }
fn describe(f64 x) string    { return f"f64: {x}" }
fn describe(string x) string { return f"string: {x}" }
fn describe(x)               { return f"other: {x}" }

fn main() int {
    println(describe(42))
    println(describe(3.14))
    println(describe("hello"))
    println(describe(true))
    return 0
}
```

Перегрузка разрешается по точному типу. Неявных повышений
(`i8 → i32`, `f32 → f64`) нет. Fallback — ровно один на имя.

---

## 20. Generics

```akt
// examples/basic/generics.akt
use std.collections

fn max_of<T>(T a, T b) T {
    return a > b ? a : b
}

fn sum_of<T>(Vector<T> items) T {
    T total = items[0]
    for (i in 1..items.len()) {
        total = total + items[i]
    }
    return total
}

fn main() int {
    println(max_of(3, 7))                 // T = i32
    println(max_of(3.14, 2.71))           // T = f64
    println(max_of("abc", "xyz"))         // T = string

    println(sum_of([1, 2, 3, 4, 5]))      // T = i32
    println(sum_of([1.5, 2.5, 3.5]))      // T = f64

    // Явные аргументы типа — в квадратных скобках
    println(max_of[i32](3, 7))
    return 0
}
```

Generics мономорфизируются. Ограничений (`where T: Ord`) нет:
операции проверяются при инстанцировании.

---

## 21. C-интероп

```akt
// examples/basic/c_interop.akt
use std.c

cinclude <stdio.h>
cinclude <stdlib.h>
cinclude <math.h>
cinclude <string.h>

fn main() int {
    c.printf("hello from C\n")

    i32 n = c.atoi("12345")
    f64 x = c.atof("3.14")
    f64 s = c.sin(3.14159 / 2.0)
    size l = c.strlen("hello")

    println(f"n={n}, x={x:.2}, s={s:.4}, l={l}")

    // cstring → string
    cstring cs = c.cstring("hello from akt")
    string version = c.to_string(cs)
    println(f"version string length = {version.len()}")
    return 0
}
```

Строковые литералы NUL-терминированы и передаются в `cstring`
напрямую. Значение `string` требует `s.as_c_str()` или
`std.c.cstring(s)`.

---

## 22. Кортежи и множественный возврат

```akt
// examples/basic/tuples.akt
use std.collections
use std.math

fn divmod(int a, int b) (int, int) {
    return (a / b, a % b)
}

fn stats(Vector<f64> xs) (f64 mean, f64 var, f64 stddev) {
    f64 s = 0.0
    for (x in xs) {
        s += x
    }
    f64 mean = s / (xs.len() as f64)

    f64 sq = 0.0
    for (x in xs) {
        f64 d = x - mean
        sq += d * d
    }
    f64 var = sq / (xs.len() as f64)
    return (mean, var, math.sqrt(var))
}

fn main() int {
    auto (q, r) = divmod(17, 5)
    println(f"17/5 = {q}, remainder {r}")

    auto (mean, var, sd) = stats([2.5, 3.1, 4.8, 1.2, 5.5])
    println(f"mean={mean:.4} var={var:.4} std={sd:.4}")
    return 0
}
```

Имена в типе возврата — документация. Во время выполнения это
обычный кортеж с доступом `.0`, `.1`, `.2`.

---

## 23. Async / await

```akt
// examples/basic/async_demo.akt
use std.async
use std.time

async fn fetch(int id) int {
    await time.sleep_ms(10)
    return id * 10
}

async fn main() int {
    // Последовательно
    auto a = await fetch(1)
    auto b = await fetch(2)
    println(f"a = {a}, b = {b}")

    // Параллельно
    auto results = await async.all([
        fetch(1),
        fetch(2),
        fetch(3),
    ])
    println(f"results = {results}")
    return 0
}
```

`await` допустим только внутри `async fn`. `async fn main`
запускается через встроенный `block_on`.

---

## 24. Скрипт

```akt
// examples/basic/script.akt
use std.json
use std.io
use std.error

fn main() Result<void, std.error.Error> {
    auto content = io.read_to_string("config.json")?
    auto data = json.parse(content)?

    println(f"name = {data["name"]}")
    println(f"version = {data["version"]}")
    return Ok(())
}
```

```bash
$ katek run examples/basic/script.akt
name = myapp
version = 1.0
```

Одиночный файл запускается без манифеста. Shebang
(`#!/usr/bin/env -S katek run`) поддерживается как расширение
оболочки, но не является частью грамматики языка.
