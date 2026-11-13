# akt — стандартная библиотека `std.*`

## 1. Принципы

1. **Ядро минимально.** В компилятор встроены только типы, память,
   C-интероп, `Option`/`Result`, `print`/`println`/`len`.
2. **`std` — тонкая и предсказуемая.** Только то, что нужно
   большинству проектов. Тяжёлые доменные библиотеки — отдельные
   пакеты экосистемы.
3. **Без GC.** Ни один модуль не требует сборщика мусора.
4. **Память по модели akt.** `auto`, `arena`, `shared`, `weak`,
   `manual`, `defer`.
5. **C-совместимость.** Где разумно, `std` — обёртка над
   проверенными C-библиотеками, но с akt-интерфейсом.
6. **Платишь за то, что используешь.** Неиспользуемые модули не
   попадают в бинарник.
7. **Ошибки — значения.** Все fallible-функции возвращают
   `Result<T, std.error.Error>` или типизированный `Result<T, E>`.

---

## 2. Что встроено в компилятор

Не требует `use`:

```akt
// Печать
print(x)
println(x)

// Длина
len(x)

// Типы
i8..i128, u8..u128, int, size, isize, cchar
f32, f64, bool, char, void
string, dynstr, cstring
[T; N], []T, *T, ^T, (T1, T2)

// Option / Result
Option<T>, Result<T, E>
Some, None, Ok, Err

// Память
arena, shared, weak, manual, defer, arena_free

// C-интероп
cinclude, clink, cfn, cstruct, cunion

// Async-синтаксис
async fn, await

// Встроенные методы string/dynstr
s.len()
s.byte_len()
s.char_len()
s.as_c_str()
```

Всё остальное — в `std` или в пакетах.

---

## 3. Уровни std

### 3.1. Tier 0 — обязательно, без feature-флагов

| Модуль | Содержание |
|---|---|
| `std.error` | `Error`, коды, сообщения, `Result`-хелперы |
| `std.mem` | `alloc`, `free`, `copy`, `set`, `size_of`, `align_of` |
| `std.ptr` | `null`, `is_null`, `offset`, `read`, `write` |
| `std.alloc` | `Allocator`, `ArenaAllocator`, `PoolAllocator` |
| `std.io` | stdin/stdout/stderr, файлы, чтение/запись |
| `std.fs` | файловая система, metadata, walk |
| `std.path` | пути |
| `std.env` | env, args, cwd |
| `std.os` | platform, arch, hostname |
| `std.process` | spawn, exec, exit, pid |
| `std.string` | методы строк, UTF-8 |
| `std.fmt` | форматирование, parse_int/parse_float |
| `std.collections` | `Vector`, `Map`, `Set`, `Deque` |
| `std.math` | базовая математика |
| `std.time` | время, сон, `Duration`, `Instant` |
| `std.test` | `assert`, `assert_eq`, `assert_ne`, `assert_throws` |
| `std.log` | уровни, логгер |
| `std.thread` | `spawn`, `join`, `Handle` |
| `std.sync` | `Mutex`, `RwLock`, `Condvar`, `Once`, `Barrier`, `Semaphore` |
| `std.atomic` | `Atomic<T>`, load/store/fetch_add/CAS |
| `std.async` | базовый executor, `all`, `select`, `timeout` |
| `std.c` | C-строки, указатели, хелперы FFI |
| `std.debug` | breakpoint, backtrace, dump |
| `std.bench` | бенчмарки |

### 3.2. Tier 1 — feature-флаги

| Модуль | Feature | Содержание |
|---|---|---|
| `std.json` | `json` | parse/stringify |
| `std.regex` | `regex` | PCRE-совместимый regex |
| `std.yaml` | `yaml` | YAML |
| `std.toml` | `toml` | TOML |
| `std.csv` | `csv` | CSV |
| `std.url` | `url` | URL/URI |
| `std.uuid` | `uuid` | UUID v1/v4/v7 |
| `std.hash` | `hash` | fnv, crc32, murmur3 |
| `std.crypto` | `crypto` | hash, hmac, cipher, kdf |
| `std.compress` | `compress` | gzip, zlib, zstd, brotli |
| `std.archive` | `archive` | zip, tar |
| `std.net` | `net` | TCP/UDP |
| `std.net.tls` | `tls` | TLS-клиент/сервер |
| `std.http` | `http` | HTTP client/server |
| `std.http.server` | `http-server` | routing, middleware |
| `std.db` | `db` | универсальный интерфейс |
| `std.db.sqlite` | `sqlite` | SQLite |
| `std.term` | `term` | ANSI, cursor |
| `std.cli` | `cli` | парсер аргументов |
| `std.fs.watch` | `fs-watch` | file watcher |
| `std.simd` | `simd` | явные SIMD-типы |
| `std.dynlib` | `dynlib` | `dlopen`/`LoadLibrary` |
| `std.shell` | `shell` | запуск shell-команд |

### 3.3. Не в std, а в пакетах экосистемы

Эти модули слишком тяжёлые или доменные, чтобы жить в std:

| Пакет | Что |
|---|---|
| `akt-ui` | Widget, Column, Row, Button, Window |
| `akt-gfx` | Canvas, Texture, Shader, OpenGL/Vulkan/WebGPU |
| `akt-ml` | Tensor, Layer, Model, train |
| `akt-db-postgres` | PostgreSQL |
| `akt-db-mysql` | MySQL/MariaDB |
| `akt-db-redis` | Redis |
| `akt-db-mongo` | MongoDB |
| `akt-grpc` | gRPC |
| `akt-mqtt` | MQTT |
| `akt-smtp` | SMTP |
| `akt-imap` | IMAP |
| `akt-ssh` | SSH |
| `akt-image` | image load/save/resize |
| `akt-font` | шрифты |
| `akt-audio` | аудио |
| `akt-video` | видео |
| `akt-pdf` | PDF |
| `akt-svg` | SVG |
| `akt-graphql` | GraphQL |
| `akt-protobuf` | protobuf |
| `akt-msgpack` | msgpack |
| `akt-bson` | bson |
| `akt-qr` | QR-коды |
| `akt-barcode` | штрихкоды |
| `akt-chess` | шахматы (пример) |
| `akt-cowsay` | cowsay (пример) |

Правило: если модуль тянет большую C-библиотеку или нужен < 10%
пользователей — это пакет, а не std.

---

## 4. Соглашения

### 4.1. Имена

- Модули: `lowercase`, вложенные через `.`.
- Функции и переменные: `snake_case`.
- Типы: `PascalCase`.
- Константы: `SCREAMING_SNAKE_CASE`.
- Публичные функции: описательные, без сокращений
  (`read_to_string`, не `rds`).
- Внутренние: `_`-префикс или приватный модуль.

### 4.2. Ошибки

```akt
pub struct Error {
    pub code: i32
    pub message: dynstr
}

pub fn ok<T>(T value) Result<T, Error> { return Ok(value) }
pub fn err<T>(i32 code, string message) Result<T, Error> {
    return Err(Error { code: code, message: message.to_dynstr() })
}
```

- По умолчанию fallible-функции возвращают
  `Result<T, std.error.Error>`.
- Модуль может объявить свой `Error`-enum, но тогда `?` требует
  явного преобразования через `.to_error()`.
- `Ok(())` — успех для `void`.
- Паника (`panic`) — только для нарушений инвариантов, не для
  ожидаемых ошибок.

### 4.3. Владение

- Функция, возвращающая `dynstr`/`Vector`/`File`, передаёт
  владение вызывающему.
- Функция, принимающая `^T`, не владеет.
- Функция, принимающая `T`, получает копию.
- `shared` возвращается как `shared T`.
- `manual` возвращается как `manual T` (владение переходит).

---

## 5. Ключевые модули

### 5.1. `std.io`

```akt
use std.io

fn main() Result<void, std.error.Error> {
    // stdout/stderr
    io.stdout.write_string("hello\n")?
    io.stderr.write_string("error\n")?

    // Файлы
    auto f = io.open("data.txt")?
    defer f.close()

    auto content = io.read_to_string("data.txt")?
    io.write_to_string("out.txt", "hello\nworld\n")?

    // Построчно
    auto reader = io.open("data.txt")?
    defer reader.close()
    for (line in reader.lines()) {
        println(line)
    }

    // Байты
    auto bytes = io.read_bytes("file.bin", 1024)?
    return Ok(())
}
```

Типы и конструкторы:

```akt
pub struct File { /* непрозрачный */ }

pub fn open(string path) Result<File, Error>    // открыть существующий
pub fn create(string path) Result<File, Error>  // создать/перезаписать

impl File {
    fn close(self) void
    fn read_line(self) Result<Option<dynstr>, Error>
    fn lines(self) Iterator<dynstr>
    fn write_string(self, string s) Result<void, Error>
    fn write_line(self, string s) Result<void, Error>
}
```

### 5.2. `std.fs`

```akt
use std.fs

fn main() Result<void, std.error.Error> {
    if (fs.exists("file.txt")) {
        auto meta = fs.metadata("file.txt")?
        println(f"size = {meta.size}")
        println(f"is_file = {meta.is_file}")
    }

    for (entry in fs.list_dir(".")?) {
        println(entry.path)
    }

    for (entry in fs.walk("src")?) {
        if (entry.is_file) {
            println(entry.path)
        }
    }
    return Ok(())
}
```

### 5.3. `std.path`

```akt
use std.path

fn main() {
    println(path.join("src", "http", "server.akt"))
    println(path.basename("/home/user/file.txt"))   // file.txt
    println(path.dirname("/home/user/file.txt"))    // /home/user
    println(path.extension("file.txt"))             // txt
    println(path.absolute("file.txt"))
}
```

### 5.4. `std.env` и `std.process`

```akt
use std.env
use std.process

fn main() {
    println(f"HOME = {env.get("HOME") ?? "unknown"}")
    println(f"cwd = {env.cwd()}")
    println(f"args = {env.args()}")
    println(f"pid = {process.pid()}")
}
```

### 5.5. `std.collections`

```akt
use std.collections

fn main() {
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

    // Map
    Map<string, i32> ages = {}
    ages.set("alice", 30)
    ages.set("bob", 25)
    println(f"alice = {ages.get("alice") ?? -1}")
    for (k, val in ages) {
        println(f"{k} = {val}")
    }

    // Set
    Set<i32> s = {1, 2, 3, 2, 1}
    println(f"set len = {s.len()}")

    // Deque
    Deque<i32> dq = []
    dq.push_back(1)
    dq.push_front(0)
    println(f"front = {dq.front() ?? -1}")
}
```

Литералы:

- `[]` / `[a, b, c]` — `Vector<T>` или `Deque<T>` по контексту.
- `{}` — `Map<K, V>` по контексту.
- `{a, b, c}` — `Set<T>` по контексту.

### 5.6. `std.string`

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
    println("hello".as_c_str())
    println("hello"[0])          // u8
    println("hello"[1..3])       // string
    println("a\nb".lines())
    println("a b c".words())

    auto n = "123".parse_int[i32]()?
    auto x = "3.14".parse_float[f64]()?
    println(f"n={n} x={x:.2}")
    return Ok(())
}
```

`len()` возвращает длину в байтах. Для code points — `char_len()`
и `chars()`.

### 5.7. `std.math`

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

`std.math.statistics`:

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

### 5.8. `std.time`

```akt
use std.time

fn main() {
    i64 t = now_ms()
    sleep_ms(10)
    i64 elapsed = now_ms() - t
    println(f"elapsed = {elapsed} ms")

    auto d = Duration::from_ms(1500)
    println(f"{d.as_secs()} s")
}
```

### 5.9. `std.json` (feature `json`)

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

`json.parse` возвращает `Result<Json, Error>`. `Json` — enum:
`Null`, `Bool`, `Number`, `String`, `Array`, `Object`.

### 5.10. `std.regex` (feature `regex`)

```akt
use std.regex

fn main() Result<void, std.error.Error> {
    auto re = Regex::new(r"\d+")?
    for (m in re.find_all("abc123def456")) {
        println(f"match: {m}")
    }
    println(f"replace: {re.replace("abc123", "X")}")
    return Ok(())
}
```

### 5.11. `std.http` (feature `http`)

```akt
use std.http
use std.json

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

### 5.12. `std.net` (feature `net`)

```akt
use std.net

async fn main() Result<void, std.error.Error> {
    auto stream = await net.TcpStream::connect("example.com", 80)?
    await stream.write_string("GET / HTTP/1.0\r\n\r\n")?
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

### 5.13. `std.db.sqlite` (feature `sqlite`)

```akt
use std.db.sqlite

fn main() Result<void, std.error.Error> {
    auto conn = sqlite.open(":memory:")?

    conn.exec("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)")?
    conn.insert("INSERT INTO users (name) VALUES (?)", "Alice")?
    conn.insert("INSERT INTO users (name) VALUES (?)", "Bob")?

    for (row in conn.query("SELECT * FROM users")?) {
        println(f"{row["id"]}: {row["name"]}")
    }

    conn.begin()?
    conn.exec("INSERT INTO users (name) VALUES (?)", "Carol")?
    conn.commit()?
    return Ok(())
}
```

### 5.14. `std.thread`, `std.sync`, `std.atomic`

```akt
use std.thread
use std.sync
use std.atomic
use std.collections

fn main() Result<void, std.error.Error> {
    // Mutex
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
    // Channel
    auto (tx, rx) = thread.channel.bounded[i32](10)
    thread.spawn(move || {
        tx.send(42)
    })
    println(f"recv = {rx.recv()}")
    return Ok(())
}
```

### 5.15. `std.async` (feature `async`)

```akt
use std.async
use std.http

async fn fetch(string url) Result<string, std.error.Error> {
    auto resp = await http.get(url)?
    return Ok(resp.body)
}

async fn main() Result<void, std.error.Error> {
    auto a = await fetch("https://example.com/a")?
    auto b = await fetch("https://example.com/b")?

    auto results = await async.all([
        fetch("https://example.com/1"),
        fetch("https://example.com/2"),
        fetch("https://example.com/3"),
    ])?

    auto first = await async.select([
        fetch("https://a.example.com"),
        fetch("https://b.example.com"),
    ])?

    auto result = await async.timeout(5000, fetch("https://slow.example.com"))?
    return Ok(())
}
```

### 5.16. `std.crypto` (feature `crypto`)

```akt
use std.crypto.hash
use std.crypto.random

fn main() {
    println(f"md5 = {md5("hello").hex()}")
    println(f"sha1 = {sha1("hello").hex()}")
    println(f"sha256 = {sha256("hello").hex()}")
    println(f"blake3 = {blake3("hello").hex()}")

    auto salt = random.bytes(16)
    println(f"salt = {salt.hex()}")
}
```

### 5.17. `std.compress` (feature `compress`)

```akt
use std.compress

fn main() Result<void, std.error.Error> {
    auto data = "hello world hello world hello world"
    auto gz = compress.gzip(data)?
    println(f"compressed: {gz.len()} bytes")
    auto restored = compress.gunzip(gz)?
    println(f"decompressed: {restored}")
    return Ok(())
}
```

### 5.18. `std.test`

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

### 5.19. `std.log`

```akt
use std.log

fn main() {
    log.info("server started")
    log.warn("cache miss")
    log.error("connection failed")
    log.debug("value x=42")
}
```

### 5.20. `std.cli` (feature `cli`)

```akt
use std.cli
use std.env

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

### 5.21. `std.term` (feature `term`)

```akt
use std.term

fn main() {
    println(term.red("red text"))
    println(term.green("green text"))
    println(term.yellow("yellow text"))
    println(term.blue("blue text"))
    term.clear_screen()
    term.move_cursor(10, 5)
}
```

### 5.22. `std.c` и `std.mem`

```akt
use std.c
use std.mem

fn main() {
    cstring s = c.cstring("hello")
    string view = c.to_string(s)
    println(view)

    *u8 p = mem.alloc(16) as *u8
    defer mem.free(p)
    mem.set(p, 0, 16)
    println(f"size_of(i32) = {mem.size_of[i32]()}")
}
```

---

## 6. Feature-флаги

В `katek.toml`:

```toml
[std]
features = ["json", "http", "tls", "sqlite"]
```

или через зависимость:

```toml
[dependencies]
std = { features = ["json", "http"] }
```

Правила:

- Feature не включает код в бинарник, пока модуль не используется.
- Feature может тянуть native-зависимость:
  `tls = ["native:openssl"]`.
- `default`-набор — минимальный: `std` без тяжёлых модулей.
- `katek build --no-default-features` отключает `default`.

---

## 7. Размер и производительность

Принцип: std не раздувает бинарник.

| Сценарий | Ориентир |
|---|---|
| hello world без std | < 10 КБ |
| + `std.io` | ~50 КБ |
| + `std.json` | ~100 КБ |
| + `std.http` | ~500 КБ |
| + `std.http` + `tls` | ~1–2 МБ (зависит от TLS) |

Это ориентиры, а не контракт. Реальные цифры публикуются в CI.

Zero-cost:

- неиспользуемые модули не линкуются;
- неиспользуемые функции удаляются (`-ffunction-sections -Wl,--gc-sections`);
- generics мономорфизируются только для использованных типов;
- `const fn` вычисляется на этапе компиляции.

---

## 8. Расширение std

Новый модуль попадает в std, только если:

1. Он нужен большинству проектов.
2. Он не тянет тяжёлую C-библиотеку без feature-флага.
3. Он детерминирован и не требует GC.
4. Он следует модели памяти akt.
5. Он покрыт тестами и документацией.
6. Он не дублирует пакет экосистемы.

Иначе — отдельный пакет `akt-*` в `hub.akt.dev`.

Агент, реализующий std, обязан:

- документировать каждый публичный элемент;
- писать тесты для каждой функции;
- использовать `Result` для fallible-операций;
- не добавлять глобальное состояние без явного API;
- не скрывать аллокации и I/O;
- не добавлять GC.

---

## 9. Итог

`std` — это:

- минимальное, но достаточное ядро;
- предсказуемые ошибки;
- C-совместимость где разумно;
- feature-флаги;
- отсутствие GC;
- детерминизм;
- расширение через пакеты, а не через раздувание std.

Главный принцип:

> В std — только то, что нужно почти всем.
> Остальное — в пакеты.
> Ничего не попадает в бинарник, пока не используется.
