# Стандартная библиотека akt

Сейчас std — это набор встроенных функций и модулей, которые
реализованы в рантайме и/или кодогенераторе. Полноценные `.akt`-модули
появятся после реализации импорта между файлами.

## 1. Встроенные функции

| Функция | Описание |
|---|---|
| `print(x)` | Печать без перевода строки |
| `println(x)` | Печать с переводом строки |
| `println()` | Пустая строка |
| `len(x)` | Длина строки, массива или `Vector` |
| `str(x)` | Преобразовать в `string` |
| `int(x)` | Преобразовать в `int` |
| `float(x)` | Преобразовать в `f64` |
| `assert(cond)` | Паника, если `cond` ложно |
| `assert(cond, msg)` | То же (msg пока не печатается) |
| `assert_eq(a, b)` | Проверка равенства |
| `assert_ne(a, b)` | Проверка неравенства |
| `panic(msg)` | Аварийное завершение |

`print`/`println` понимают `string`, целые, `f64`, `bool`, `char`.

## 2. Математика

Доступны без импорта:

```akt
sqrt(x)
sin(x)
cos(x)
tan(x)
floor(x)
ceil(x)
round(x)
abs(x)
pow(a, b)
min(a, b)
max(a, b)
```

`abs` для целых возвращает `f64`; для `f64` — `f64`.

## 3. `std.string` / методы строк

```akt
s.len()
s.byte_len()
s.char_len()
s.trim()
s.to_upper()
s.to_lower()
s.reverse()
s.capitalize()
s.contains(sub)
s.starts_with(prefix)
s.ends_with(suffix)
s.find(sub)
s.replace(from, to)
s.repeat(n)
s.pad_left(width, ch)
s.pad_right(width, ch)
s.split(sep)       // Vector<string>
s.parse_int()
s.parse_float()
s.as_c_str()      // план
```

`std.fmt`:

```akt
fmt.parse_int(s)
fmt.parse_float(s)
```

Константы: `PI`, `E`, `TAU`, `PHI`, `INF`, `NAN`.

Реализованы в `akt_runtime.c`, кроме `as_c_str`.

## 4. `std.io`

```akt
io.read_to_string(path)       // string
io.write_to_string(path, s)   // bool
io.exists(path)               // bool
io.read_file(path)            // алиас
io.write_file(path, s)        // алиас
```

Пример:

```akt
use std.io

fn main() int {
    io.write_to_string("hello.txt", "hello\n")
    string s = io.read_to_string("hello.txt")
    print(s)
    return 0
}
```

`?` пока не является полноценным пробросом `Result`; функции возвращают
значение напрямую.

## 5. `std.fs`

```akt
fs.exists(path)          // bool
fs.read_to_string(path)  // string
fs.list_dir(path)        // Vector<string>
fs.walk(path)            // Vector<string>
```

`metadata` — план.

## 6. `std.path`

```akt
path.join(a, b)
path.basename(p)
path.dirname(p)
path.extension(p)
path.absolute(p)
```

## 7. `std.env`

```akt
env.get(name)   // string; пустая строка, если нет
env.cwd()       // string
env.args()      // план: массив аргументов
```

## 8. `std.time`

```akt
time.now_ms()      // int64
time.sleep_ms(n)   // void
```

## 9. `std.process`, `std.random`, `std.hash`, `std.base64`, `std.json`, `std.regex`, `std.thread`, `std.http`, `std.db.sqlite`

```akt
process.pid()
process.exit(code)
random.int(max)
random.float()
hash.fnv1a(s)
base64.encode(s)
base64.decode(s)

Json j = json.parse("{...}")
json.stringify(j)
j["key"]

regex.match("\\d+", "abc123")
regex.find("\\d+", "abc123")
regex.replace("\\d+", "a1b2", "#")

fn worker() { println("thread") }
Handle h = thread.spawn(worker)
thread.join(h)

shared i32 counter = 0
shared Mutex mtx = Mutex::new()
Handle h2 = thread.spawn(|| {
    mtx.lock()
    defer mtx.unlock()
    counter += 1
})
thread.join(h2)

string body = http.get("https://example.com")
string echo = http.post("https://example.com/api", "{}")

Sqlite db = sqlite.open(":memory:")
db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)")
for (row in db.query("SELECT * FROM t")) {
    println(row)
}
db.close()
```

`thread.spawn` сейчас принимает **имя глобальной функции без
параметров**. Захват замыканий в поток — следующий этап.

`std.sync` / `std.atomic`:

```akt
Mutex m = Mutex::new()
m.lock()
m.unlock()

Atomic<i32> a = Atomic::new(0)
a.fetch_add(1)
a.load()
a.store(10)
```

`thread.spawn` принимает глобальную функцию без параметров или
замыкание `|| { ... }`; захваченные `shared`-переменные передаются как
refcount-ячейки.

## 10. `std.c` — базовый C-интероп

```akt
cinclude <stdio.h>
cinclude <math.h>
clink "m"

fn main() int {
    c.printf("hello from C\n")
    f64 s = c.sin(3.14159 / 2.0)
    println(f"sin = {s}")
    return 0
}
```

- `cinclude <header>` → `#include`.
- `clink "lib"` → `-llib` (на Windows `m` игнорируется).
- `c.func(args)` → `func(args)`.
- строковые аргументы → `.data`.
- `c.to_string(ptr)` / `c.cstring(ptr)` → `string`.

`cinclude` сам импортирует объявления из заголовка. Пример линковки с
системной библиотекой: `tests/lang/opengl_probe.akt` (`opengl32`).

## 11. `std.collections`

`Vector<T>`:

```akt
Vector<i32> v = [1, 2, 3]
v.push(4)
v.pop()
v.clear()
v.len()
```

`Map<string, T>`:

```akt
Map<string, i32> ages = {}
ages.set("alice", 30)
println(f"{ages.get("alice") ?? -1}")
println(ages.contains("alice"))
ages.remove("alice")
```

`Set<T>`:

```akt
Set<i32> s = [1, 2, 2, 3]
s.add(4)
s.contains(2)
s.remove(2)
```

`Deque<T>` (алиас `Vector` с методами):

```akt
Deque<i32> dq = []
dq.push_back(2)
dq.push_front(1)
dq.front()
dq.back()
```

## 12. `std.math`

Помимо встроенных функций, планируются константы `PI`, `E`, `TAU`,
`PHI`, `INF`, `NAN` и `std.math.statistics`.

## 13. Tier-модель

- **Tier 0** — без feature-флагов: строки, IO, fs, path, env, math,
  time, collections, thread (план), sync (план).
- **Tier 1** — feature-флаги: json, regex, http, sqlite, crypto,
  compress, net.
- **Экосистема** — akt-ui, akt-gfx, akt-ml.

Сейчас реально доступны только функции из разделов 1–9. Остальное —
цели из [`ROADMAP.md`](ROADMAP.md).

## 14. Принципы расширения

1. Сначала C-рантайм, потом akt-обёртка.
2. Ошибки — значения (`Result`), а не `panic`.
3. Никакого GC.
4. Внешние C-библиотеки — через провайдеры, а не build-hooks.
5. Каждая функция покрыта тестом.
