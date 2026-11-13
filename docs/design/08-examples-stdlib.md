# akt — примеры стандартной библиотеки

Модули `std.*` не встроены в компилятор: их нужно импортировать.
Часть модулей включается feature-флагами в `katek.toml`.

Все примеры предполагают, что соответствующий feature уже включён.

---

## 1. `std.io` — файлы и потоки

```akt
use std.io
use std.error

fn main() Result<void, std.error.Error> {
    io.stdout.write_string("hello\n")?
    io.stderr.write_string("warning\n")?

    auto content = io.read_to_string("data.txt")?
    println(f"bytes = {content.len()}")

    io.write_to_string("out.txt", "hello\nworld\n")?

    auto f = io.open("out.txt")?
    defer f.close()

    for (line in f.lines()) {
        println(f"line: {line}")
    }

    auto bytes = io.read_bytes("out.txt", 1024)?
    println(f"read {bytes.len()} bytes")
    return Ok(())
}
```

```toml
# katek.toml — ничего дополнительно не нужно, std.io в Tier 0
```

Важно: `?` допустим только в функции, возвращающей `Result` с
совместимым типом ошибки. Для `fn main() int` используйте
`switch` по `Result`.

---

## 2. `std.fs` и `std.path`

```akt
use std.fs
use std.path
use std.error

fn main() Result<void, std.error.Error> {
    if (fs.exists("data.txt")) {
        auto meta = fs.metadata("data.txt")?
        println(f"size = {meta.size}")
        println(f"is_file = {meta.is_file}")
    }

    auto entries = fs.list_dir(".")?
    for (entry in entries) {
        println(entry.path)
    }

    auto files = fs.walk("src")?
    for (entry in files) {
        if (entry.is_file) {
            println(f"file: {entry.path}")
        }
    }

    println(path.join("src", "http", "server.akt"))
    println(path.basename("/home/user/file.txt"))
    println(path.dirname("/home/user/file.txt"))
    println(path.extension("file.txt"))
    println(path.absolute("file.txt"))
    return Ok(())
}
```

`path.join` использует разделитель текущей платформы. Для
переносимых путей не склеивайте строки вручную.

---

## 3. `std.env` и `std.process`

```akt
use std.env
use std.process

fn main() int {
    println(f"HOME = {env.get("HOME") ?? "unknown"}")
    println(f"cwd = {env.cwd()}")
    println(f"pid = {process.pid()}")

    auto args = env.args()
    for (i, arg in args) {
        println(f"arg[{i}] = {arg}")
    }

    if (args.len() > 1 && args[1] == "--version") {
        println("myapp 1.0.0")
        process.exit(0)
    }
    return 0
}
```

`env.get` возвращает `Option<string>`, поэтому используется `??`.
`process.exit` не возвращается и не требует `return`.

---

## 4. `std.collections` — `Vector`, `Map`, `Set`, `Deque`

```akt
use std.collections

fn main() int {
    // Vector
    Vector<i32> v = [1, 2, 3]
    v.push(4)
    v.insert(0, 0)
    v.remove_at(2)
    v.sort()
    v.reverse()
    println(f"v = {v}")
    println(f"len = {v.len()}")
    println(f"contains = {v.contains(3)}")

    auto evens = v.filter(|x| x % 2 == 0)
    auto doubled = v.map(|x| x * 2)
    auto total = v.reduce(0, |acc, x| acc + x)
    println(f"evens={evens} doubled={doubled} total={total}")

    // Map
    Map<string, i32> ages = {}
    ages.set("alice", 30)
    ages.set("bob", 25)
    println(f"alice = {ages.get("alice") ?? -1}")
    for (name, age in ages) {
        println(f"{name} = {age}")
    }

    // Set
    Set<i32> s = {1, 2, 3, 2, 1}
    println(f"set len = {s.len()}")
    println(f"has 2 = {s.contains(2)}")

    // Deque
    Deque<i32> dq = []
    dq.push_back(1)
    dq.push_front(0)
    println(f"front = {dq.front() ?? -1}")
    println(f"back = {dq.back() ?? -1}")
    return 0
}
```

Литералы:

- `[]`, `[a, b, c]` — `Vector<T>` или `Deque<T>` по контексту;
- `{}` — `Map<K, V>`;
- `{a, b, c}` — `Set<T>`.

`Vector` владеет элементами; при выходе из региона буфер
освобождается целиком.

---

## 5. `std.string`

```akt
use std.string
use std.error

fn main() Result<void, std.error.Error> {
    println("  Hello  ".trim())
    println("hello".to_upper())
    println("HELLO".to_lower())
    println("a,b,c".split(","))
    println("hello".replace("l", "L"))
    println("hello".contains("ell"))
    println("hello".starts_with("he"))
    println("hello".find("ll"))
    println("ab".repeat(3))
    println("5".pad_left(3, '0'))
    println("hello".reverse())
    println("hello".capitalize())
    println("hello"[0])
    println("hello"[1..3])
    println("a\nb".lines())
    println("a b c".words())

    auto n = "123".parse_int[i32]()?
    auto x = "3.14".parse_float[f64]()?
    println(f"n={n} x={x:.2}")
    return Ok(())
}
```

- `len()` — длина в байтах.
- `char_len()` — длина в Unicode code points.
- `chars()` — итератор по code points.
- `bytes()` — итератор по `u8`.

`string` индексируется по байтам; для Unicode используйте
`chars()`.

---

## 6. `std.fmt`

```akt
use std.fmt
use std.error

fn main() Result<void, std.error.Error> {
    auto n = fmt.parse_int[i32]("42")?
    auto x = fmt.parse_float[f64]("3.1415")?
    println(f"n={n} x={x:.2}")

    // Форматирование через f-строки — основной способ.
    println(f"n = {n}")
    println(f"x = {x:.3}")
    return Ok(())
}
```

`std.fmt` не заменяет f-строки: `f"..."` — синтаксис языка,
`fmt.parse_*` — разбор. Спецификаторы формата в f-строках:
`:.N` (точность), `:0N` (ширина с ведущими нулями), `:>N`/`:<N`
(выравнивание).

---

## 7. `std.math`

```akt
use std.math

fn main() {
    println(f"sqrt(2) = {sqrt(2.0)}")
    println(f"sin(pi/2) = {sin(PI / 2.0)}")
    println(f"pow(2,10) = {pow(2.0, 10.0)}")
    println(f"abs(-5) = {abs(-5)}")
    println(f"min(3,7) = {min(3, 7)}")
    println(f"clamp(15,0,10) = {clamp(15, 0, 10)}")
    println(f"lerp(0,10,0.5) = {lerp(0.0, 10.0, 0.5)}")
    println(f"gcd(12,18) = {gcd(12, 18)}")
    println(f"factorial(5) = {factorial(5)}")
}
```

Константы: `PI`, `E`, `TAU`, `PHI`, `INF`, `NAN`.

Статистика:

```akt
use std.math.statistics
use std.collections

fn main() {
    Vector<f64> xs = [2.5, 3.1, 4.8, 1.2, 5.5]
    println(f"mean = {mean(xs)}")
    println(f"median = {median(xs)}")
    println(f"stddev = {stddev(xs)}")
    println(f"p90 = {percentile(xs, 90)}")
}
```

Для воспроизводимых вычислений не полагайтесь на порядок
`Vector` после сортировки без явного компаратора.

---

## 8. `std.time`

```akt
use std.time

fn main() {
    i64 t = now_ms()
    sleep_ms(10)
    i64 elapsed = now_ms() - t
    println(f"elapsed = {elapsed} ms")

    auto d = Duration::from_ms(1500)
    println(f"{d.as_secs()} s")

    auto start = Instant::now()
    sleep_ms(5)
    println(f"instant = {start.elapsed_ms()} ms")
}
```

`now_ms()` — монотонное время в миллисекундах. Для календарного
времени используйте `std.time.wall` (Tier 0, но зависит от
платформы).

---

## 9. `std.json` (feature `json`)

```akt
use std.json
use std.error

struct User {
    string name
    i32 age
}

fn main() Result<void, std.error.Error> {
    auto data = json.parse("{\"name\":\"Alice\",\"age\":30}")?

    println(f"name = {data["name"]}")
    println(f"age  = {data["age"]}")

    auto pretty = json.stringify_pretty(data)
    println(pretty)

    auto user = json.parse_as[User]("{\"name\":\"Bob\",\"age\":25}")?
    println(f"user: {user.name}, {user.age}")
    return Ok(())
}
```

```toml
[std]
features = ["json"]
```

`Json` — enum: `Null`, `Bool`, `Number`, `String`, `Array`,
`Object`. `parse_as[T]` требует, чтобы поля `T` были публичными и
сериализуемыми.

---

## 10. `std.regex` (feature `regex`)

```akt
use std.regex
use std.error

fn main() Result<void, std.error.Error> {
    auto re = Regex::new(r"\d+")?

    for (m in re.find_all("abc123def456")) {
        println(f"match: {m}")
    }

    println(f"replace: {re.replace("abc123", "X")}")
    return Ok(())
}
```

`Regex::new` возвращает `Result`: некорректный шаблон — это
ожидаемая ошибка, а не паника.

---

## 11. `std.crypto`, `std.hash`, `std.compress`

```akt
use std.crypto.hash
use std.crypto.random

fn main() {
    println(f"md5    = {md5("hello").hex()}")
    println(f"sha1   = {sha1("hello").hex()}")
    println(f"sha256 = {sha256("hello").hex()}")
    println(f"blake3 = {blake3("hello").hex()}")

    auto salt = random.bytes(16)
    println(f"salt = {salt.hex()}")
}
```

```akt
use std.compress
use std.error

fn main() Result<void, std.error.Error> {
    auto data = "hello world hello world hello world"
    auto gz = compress.gzip(data)?
    println(f"compressed: {gz.len()} bytes")

    auto restored = compress.gunzip(gz)?
    println(f"restored: {restored}")
    return Ok(())
}
```

```toml
[std]
features = ["crypto", "compress"]
```

`std.crypto` — обёртка над проверенными C-библиотеками. Не
реализуйте криптографию самостоятельно.

---

## 12. `std.net` (feature `net`)

```akt
use std.net
use std.error

async fn main() Result<void, std.error.Error> {
    auto stream = await net.TcpStream::connect("example.com", 80)?
    await stream.write_string("GET / HTTP/1.0\r\nHost: example.com\r\n\r\n")?
    auto response = await stream.read_to_string()?
    println(response)

    auto listener = net.TcpListener::bind("127.0.0.1", 9000)?
    loop {
        auto (client, addr) = await listener.accept()?
        println(f"connection from {addr}")
        auto data = await client.read_to_string()?
        await client.write_string("echo: " + data)?
    }
    return Ok(())
}
```

```toml
[std]
features = ["net"]
```

`loop` здесь бесконечный; выход — по ошибке или сигналу. Для
production-сервера используйте `std.http.server`, а не сырой TCP.

---

## 13. `std.http` (feature `http`)

Клиент:

```akt
use std.http
use std.json
use std.error

async fn main() Result<void, std.error.Error> {
    auto client = http.Client::new()
    auto resp = await client.get("https://httpbin.org/json")?

    println(f"status = {resp.status}")
    println(resp.body)

    auto data = json.parse(resp.body)?
    println(json.stringify_pretty(data))
    return Ok(())
}
```

Сервер:

```akt
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

```toml
[std]
features = ["http", "http-server", "tls", "json"]
```

`http.Response::json` сериализует значение через `std.json`.
Обработчики выполняются в executor; блокирующие вызовы выносите в
`std.thread`.

---

## 14. `std.db.sqlite` (feature `sqlite`)

```akt
use std.db.sqlite
use std.error

fn main() Result<void, std.error.Error> {
    auto conn = sqlite.open(":memory:")?

    conn.exec("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)")?
    conn.insert("INSERT INTO users (name) VALUES (?)", "Alice")?
    conn.insert("INSERT INTO users (name) VALUES (?)", "Bob")?

    auto rows = conn.query("SELECT * FROM users")?
    for (row in rows) {
        println(f"{row["id"]}: {row["name"]}")
    }

    conn.begin()?
    conn.exec("INSERT INTO users (name) VALUES (?)", "Carol")?
    conn.commit()?
    return Ok(())
}
```

```toml
[std]
features = ["sqlite"]

[native.dependencies]
sqlite3 = { provider = "xrepo", version = "3.45.0" }
```

`sqlite.open` возвращает `Result`: отсутствие библиотеки или
права на файл — обычная ошибка.

---

## 15. `std.thread`, `std.sync`, `std.atomic`

```akt
use std.thread
use std.sync
use std.atomic
use std.collections
use std.error

fn main() Result<void, std.error.Error> {
    // Mutex + shared-счётчик
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

    // Atomic
    auto atomic = Atomic<i32>::new(0)
    atomic.fetch_add(1)
    println(f"atomic = {atomic.load()}")

    // Channel
    auto (tx, rx) = thread.channel.bounded[i32](10)
    thread.spawn(move || {
        tx.send(42)
    })
    println(f"recv = {rx.recv()}")
    return Ok(())
}
```

`shared` делает атомарным только refcount. Для данных нужны
`Mutex`, `RwLock`, `Condvar` или `Atomic<T>`. Гонки данных
компилятор не доказывает.

---

## 16. `std.async`

```akt
use std.async
use std.http
use std.error

async fn fetch(string url) Result<string, std.error.Error> {
    auto resp = await http.get(url)?
    return Ok(resp.body)
}

async fn main() Result<void, std.error.Error> {
    auto a = await fetch("https://example.com/a")?
    auto b = await fetch("https://example.com/b")?
    println(f"{a.len()} {b.len()}")

    auto results = await async.all([
        fetch("https://example.com/1"),
        fetch("https://example.com/2"),
        fetch("https://example.com/3"),
    ])?
    println(f"results = {results.len()}")

    auto first = await async.select([
        fetch("https://a.example.com"),
        fetch("https://b.example.com"),
    ])?
    println(f"first = {first.len()}")

    auto result = await async.timeout(5000, fetch("https://slow.example.com"))?
    println(f"timeout result = {result.len()}")
    return Ok(())
}
```

`async.all` ждёт все futures и возвращает `Vector<Result<...>>`.
`select` — первый завершившийся. `timeout` возвращает ошибку по
истечении срока.

---

## 17. `std.log`, `std.term`, `std.cli`

```akt
use std.log
use std.term

fn main() {
    log.info("server started")
    log.warn("cache miss")
    log.error("connection failed")
    log.debug("value x=42")

    println(term.red("red text"))
    println(term.green("green text"))
    println(term.yellow("yellow text"))
    println(term.blue("blue text"))
    term.clear_screen()
    term.move_cursor(10, 5)
}
```

CLI-парсер:

```akt
use std.cli
use std.env
use std.error

fn main() Result<void, std.error.Error> {
    auto parser = cli.Parser::new("myapp", "A demo app")
    parser.flag("--verbose", "-v", "verbose output")
    parser.option("--output", "-o", "output file")
    parser.arg("<input>", "input file")

    auto args = parser.parse(env.args())?
    if (args.has("--verbose")) {
        println("verbose mode")
    }
    println(f"input = {args.get("<input>") ?? ""}")
    println(f"output = {args.get("--output") ?? "out.txt"}")
    return Ok(())
}
```

```toml
[std]
features = ["cli", "term"]
```

---

## 18. `std.test`

```akt
use std.test

test "basic math" {
    assert_eq(2 + 2, 4)
    assert_ne(2 + 2, 5)
    assert(2 + 2 == 4)
}

test "with message" {
    assert_eq(2 + 2, 4, "2+2 should be 4")
}

test "expect panic" {
    assert_throws(|| {
        panic("boom")
    })
}
```

`test` — только на уровне модуля. `katek test` запускает все
`tests/**/*.akt` и `*_test.akt`.

---

## 19. Feature-флаги

```toml
# katek.toml
[package]
name = "stdlib-demo"
version = "1.0.0-beta.1"
edition = "2026"

[std]
features = ["json", "regex", "http", "sqlite", "tls"]
```

Правила:

- Модули Tier 0 доступны всегда.
- Модули Tier 1 требуют feature.
- Тяжёлые доменные библиотеки (`akt-ui`, `akt-ml`, `akt-gfx`)
  живут в пакетах, а не в std.
- Неиспользуемые модули не попадают в бинарник.

Полный каталог — в [`05-stdlib.md`](05-stdlib.md).
