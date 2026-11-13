# akt — полный тур по ядру языка

Этот файл показывает все конструкции ядра akt в одном месте.
Синтаксис нормативно зафиксирован в [`01-syntax.md`](01-syntax.md);
здесь — примеры и типичные комбинации.

Правило: **объявления типов, функций и тестов — только на уровне
модуля**. Внутри функции можно объявлять переменные, но не
`struct`, `enum`, `fn` или `test`.

---

## 1. Модуль, константы и статические переменные

```akt
// Константы времени компиляции
const i32 MAX_SIZE = 1000
const f64 PI_VALUE = 3.141592653589793
const string APP_NAME = "akt demo"
const u32 VERSION = 0x0001_0000

// Изменяемое состояние модуля
static i32 g_counter = 0
static bool g_ready = false

// Псевдонимы типов
type Pair = (f64, f64)
type IntList = Vector<i32>
```

`static` инициализируется константным выражением. Для ленивой
инициализации используйте `std.sync.Once`.

---

## 2. Типы

```akt
fn types_demo() {
    // Целые точного размера
    i8 a = -128
    u8 b = 255
    i32 c = 42
    u64 d = 1_000_000
    i128 big = 170141183460469231731687303715884105727

    // Платформенные типы для C-совместимости
    int native = 42
    size sz = 1024
    isize isz = -1024
    cchar ch = 'A'

    // Плавающие
    f32 f32v = 3.14f
    f64 f64v = 3.141592653589793

    // Прочие примитивы
    bool flag = true
    char letter = 'A'          // Unicode scalar, не C char

    // Текстовые типы
    string view = "immutable view"
    dynstr dyn = "dynamic"
    cstring cstr = "C string"
}
```

- `string` — view (`ptr + len`), не владеет.
- `dynstr` — владеющий буфер в текущем регионе.
- `cstring` — `*const cchar`, NUL-терминированная, не владеет.
- `[T; N]` — массив; `[]T` — срез; `*T` — сырой указатель;
  `^T` — безопасная ссылка.

---

## 3. Переменные и деструктуризация

```akt
fn variables_demo() {
    i32 x = 50
    auto inferred = 100            // i32
    const i32 LIMIT = 1000

    auto (q, r) = (17 / 5, 17 % 5)
    auto (first, second) = (1, 2)
    println(f"{x} {inferred} {LIMIT} {q} {r} {first} {second}")
}
```

Объявления `struct`, `enum`, `fn` и `test` внутри функции
запрещены. Если тип нужен только одной функции, объявите его на
уровне модуля и сделайте приватным.

Деструктуризация структуры (фрагмент внутри функции, где `point`
имеет тип `Point`):

```akt
auto { x: px, y: py } = point
auto [head, ...tail] = arr
```

---

## 4. Функции

```akt
fn add(int a, int b) int {
    return a + b
}

fn greet(string name) {              // void
    println(f"Hello, {name}!")
}

fn square(int x) {                   // возврат выводится
    return x * x
}

inline fn fast_square(int x) int {
    return x * x
}

const fn factorial(int n) int {
    if (n <= 1) { return 1 }
    return n * factorial(n - 1)
}
```

Правила:

- Тип возврата можно опустить, если он выводится из `return`.
- Рекурсивная функция обязана указывать тип возврата явно.
- `const fn` не имеет права на I/O, аллокации и глобальное
  состояние.
- Порядок модификаторов: `pub inline const fn`.

### 4.1. Параметры

```akt
fn by_value(BigStruct s) { }         // копия
fn by_ref(^BigStruct s) { }          // ссылка, без владения
fn by_const_ref(^const BigStruct s) { }
fn by_ptr(*BigStruct s) { }          // сырой указатель
fn by_const_ptr(*const BigStruct s) { }
```

Для больших структур предпочтителен `^Type`.

### 4.2. Перегрузка

```akt
fn multiply(int a, int b) i64 {
    return (a as i64) * (b as i64)
}

fn multiply(f64 a, f64 b) f64 {
    return a * b
}

fn multiply(i8 a, i8 b) i32 {
    return (a as i32) * (b as i32)
}
```

Разрешение — по точному типу. Неявных повышений нет. Если точного
совпадения нет, используется fallback-перегрузка (ровно одна на
имя):

```akt
fn describe(int x) string    { return f"int: {x}" }
fn describe(f64 x) string    { return f"f64: {x}" }
fn describe(string x) string { return f"string: {x}" }
fn describe(x)               { return f"other: {x}" }
```

### 4.3. Generics

```akt
fn max_of<T>(T a, T b) T {
    return a > b ? a : b
}

fn pair<T, U>(T a, U b) (T, U) {
    return (a, b)
}

auto m = max_of[i32](3, 7)
auto p = pair[i32, string](1, "hello")
```

Явные аргументы типа — в квадратных скобках: `f[T](args)`.
Generics мономорфизируются; ограничений (`where`) нет, операции
проверяются при инстанцировании.

### 4.4. Множественный возврат

```akt
use std.collections

fn divmod(int a, int b) (int, int) {
    return (a / b, a % b)
}

fn stats(Vector<f64> xs) (f64 mean, f64 var) {
    f64 sum = 0.0
    for (x in xs) { sum += x }
    f64 mean = sum / (xs.len() as f64)

    f64 sq = 0.0
    for (x in xs) {
        f64 d = x - mean
        sq += d * d
    }
    return (mean, sq / (xs.len() as f64))
}

auto (q, r) = divmod(17, 5)
auto (mean, var) = stats([1.0, 2.0, 3.0])
```

Имена в типе возврата — документация; значение остаётся кортежем.

---

## 5. Struct, `impl` и ассоциированные функции

```akt
use std.math

struct Vec2 {
    f64 x
    f64 y

    fn length() f64 {
        return math.sqrt(self.x * self.x + self.y * self.y)
    }

    fn add(Vec2 other) Vec2 {
        return Vec2 { x: self.x + other.x, y: self.y + other.y }
    }

    fn dot(Vec2 other) f64 {
        return self.x * other.x + self.y * other.y
    }

    static fn zero() Vec2 {
        return Vec2 { x: 0.0, y: 0.0 }
    }
}

impl Vec2 {
    fn scale(f64 factor) Vec2 {
        return Vec2 { x: self.x * factor, y: self.y * factor }
    }

    fn normalize() Vec2 {
        f64 len = self.length()
        if (len == 0.0) { return Vec2 { x: 0.0, y: 0.0 } }
        return self.scale(1.0 / len)
    }
}
```

- `self` — неявный первый параметр типа `^Self`.
- `static fn` не имеет `self`; вызов — `Vec2::zero()`.
- `::` разрешён только для вариантов enum и ассоциированных
  элементов типа. Для модулей — `.`.
- Наследования нет; используйте композицию.
- Все поля публичны, если структура `pub`. Приватных полей нет.

Создание:

```akt
Vec2 a = { x: 1.0, y: 2.0 }
Vec2 b = Vec2 { x: 3.0, y: 4.0 }
Vec2 c = Vec2::zero()
```

Позиционная инициализация запрещена, кроме C-структур.

### 5.1. Вложенные структуры

```akt
struct Line {
    Vec2 start
    Vec2 end
    string label
}

Line ln = {
    start: { x: 0.0, y: 0.0 }
    end:   { x: 3.0, y: 4.0 }
    label: "diagonal"
}
```

---

## 6. Enum и pattern matching

```akt
enum Direction { North, South, East, West }

enum Shape {
    Circle(f64)
    Rect(f64, f64)
    Triangle(f64, f64)
}

enum Event {
    Click(i32, i32)
    KeyPress(char)
    Quit
}

enum Level { Debug = 0, Info = 1, Warn = 2, Error = 3 }
```

`switch` не имеет fallthrough:

```akt
fn area(Shape s) f64 {
    return switch (s) {
        case Shape::Circle(r):      PI_VALUE * r * r
        case Shape::Rect(w, h):     w * h
        case Shape::Triangle(b, h): 0.5 * b * h
    }
}
```

Guards и диапазоны:

```akt
fn grade(i32 score) string {
    return switch (score) {
        case 90..=100: "A"
        case 80..=89:  "B"
        case 70..=79:  "C"
        case 60..=69:  "D"
        default:       "F"
    }
}

fn classify(Option<i32> value) string {
    return switch (value) {
        case Some(x) if x > 10: "big"
        case Some(_):           "small"
        case None:              "none"
    }
}
```

Правила:

- `switch` по enum обязан покрывать все варианты или иметь
  `default`.
- `default` эквивалентен `case _` и должен быть последним.
- В `switch`-выражении все ветви дают один тип.
- `break` внутри `switch`-выражения запрещён.

---

## 7. Управление потоком

```akt
fn branch_demo() {
    i32 x = 5

    if (x > 0) {
        println("positive")
    } else if (x < 0) {
        println("negative")
    } else {
        println("zero")
    }

    auto sign = if (x >= 0) { "+" } else { "-" }
    println(sign)
}
```

```akt
fn loops_demo() {
    i32 i = 0
    [i32; 3] items = [10, 20, 30]
    bool done = false

    while (i < 10) {
        i += 1
    }

    for (i32 j = 0; j < 10; j += 1) {
        println(j)
    }

    for (item in items) {
        println(item)
    }

    for (idx, item in items) {
        println(f"{idx}: {item}")
    }

    for (k in 0..10 step 2) {
        println(k)
    }

    loop {
        if (done) { break }
    }
}
```

Метки:

```akt
fn labels_demo() {
    outer: for (i in 0..5) {
        for (j in 0..5) {
            if (i * j > 6) { break outer }
            if (j == 2) { continue outer }
        }
    }
}
```

`defer`:

```akt
fn read() Result<void, std.error.Error> {
    auto f = std.io.open("data.txt")?
    defer f.close()
    return Ok(())
}
```

`defer` выполняется в порядке LIFO при любом выходе из области:
`return`, `break`, `continue`, `?`.

---

## 8. Операторы

```akt
fn operators_demo() {
    i32 a = 10
    i32 b = 3

    println(f"a + b = {a + b}")
    println(f"a - b = {a - b}")
    println(f"a * b = {a * b}")
    println(f"a / b = {a / b}")       // целочисленное
    println(f"a % b = {a % b}")
    println(f"a ** 2 = {a ** 2}")

    println(f"a & b = {a & b}")
    println(f"a | b = {a | b}")
    println(f"a ^ b = {a ^ b}")
    println(f"~a = {~a}")
    println(f"a << 2 = {a << 2}")
    println(f"a >> 1 = {a >> 1}")

    println(f"a == b = {a == b}")
    println(f"a != b = {a != b}")
    println(f"a > b && b > 0 = {a > b && b > 0}")
    println(f"a > b || b > 0 = {a > b || b > 0}")
    println(f"not (a == b) = {not (a == b)}")

    auto s = a > b ? "bigger" : "smaller"
    Option<i32> maybe = None
    auto v = maybe ?? -1
    println(f"{s}, {v}")
}
```

- `&&`/`||`/`and`/`or` — короткое замыкание, только `bool`.
- `??` работает для `Option<T>`, но не для `*T`.
- `as` — единственное числовое преобразование; проверяет диапазон.
- `&x` — сырой указатель, `^x` — безопасная ссылка, `*x` —
  разыменование.

---

## 9. Замыкания

```akt
fn closures_demo() {
    auto double = |x| x * 2
    auto add = |a, b| a + b
    auto thunk = || { println("later") }

    i32 n = 10
    auto by_ref = |x| x + n          // захват по ссылке
    auto by_value = move |x| x + n   // копия
    auto by_shared = shared |x| x + n

    println(f"{double(21)} {add(2, 3)}")
    println(f"{by_ref(1)} {by_value(1)} {by_shared(1)}")
    thunk()
}
```

- По умолчанию — захват по ссылке (`^T`).
- `move` — по значению; для `manual`/`shared` — передача владения.
- `shared` — refcount +1.
- Замыкание, переживающее регион (например, в `thread.spawn`), не
  может захватывать `auto`/`arena` по ссылке.
- Замыкание с захватами нельзя передать в C как callback.

---

## 10. Указатели и ссылки

```akt
struct Point {
    i32 x
    i32 y
}

fn pointers_demo() {
    i32 value = 42
    *i32 p = &value          // сырой указатель
    ^i32 r = ^value          // безопасная ссылка

    println(f"p = {*p}")
    println(f"r = {r}")

    *p = 100                 // запись через указатель
    println(f"value = {value}")

    *i32 null_p = null
    if (null_p == null) {
        println("null handled")
    }

    Point pt = { x: 1, y: 2 }
    *Point pp = &pt
    pp.x = 10                // авто-разыменование
    println(f"pt.x = {pt.x}")
}
```

`null` допустим только для `*T`, `*const T`, `*volatile T` и
`cstring`. `^T` всегда указывает на живое значение.

---

## 11. Память

```akt
use std.collections

struct Cache {
    i32 hits
}

fn memory_demo() {
    // auto: освобождается при выходе из региона
    {
        dynstr s = "temporary"
        s += " value"
        println(s)
    }

    // arena: освобождается целиком
    {
        arena ar
        Vector<i32> ar.values = [1, 2, 3]
        i32 ar.count = 3
        println(f"count = {ar.count}, len = {ar.values.len()}")
    }

    // manual: освобождается явно
    {
        manual *i32 buf = std.mem.alloc(4) as *i32
        defer std.mem.free(buf)
        *buf = 42
        println(f"buf = {*buf}")
    }

    // shared: atomic refcount
    shared Cache cache = Cache { hits: 0 }

    // weak: не продлевает жизнь
    weak Cache weak_cache = cache
    switch (weak_cache.upgrade()) {
        case Some(strong): { println(f"alive, hits = {strong.hits}") }
        case None:         { println("freed") }
    }
}
```

`shared` делает атомарным **только refcount**. Для данных нужен
`Mutex`, `RwLock` или `Atomic<T>`.

Циклы сильных ссылок запрещены и диагностируются компилятором.
Обратные ссылки делайте `weak`.

---

## 12. Модули

```akt
// src/math.akt
pub fn add(int a, int b) int { return a + b }
pub const ANSWER = 42

fn helper() { }        // приватный
```

```akt
// src/main.akt
use math              // модуль src/math.akt
use std.io            // std
use std.http.server   // вложенный std
use mypkg.submodule   // пакет-зависимость
use std.math as m     // псевдоним

pub use math          // реэкспорт
```

- Файл = модуль. Заголовочных файлов нет.
- `use a.b.c` импортирует публичные имена и связывает `c` как
  пространство имён: `c.fn()`, `c.Type`.
- `::` в `use` запрещён: `use std::io` — ошибка.
- При конфликте имён нужен `as` или полное имя.

---

## 13. C-интероп

```akt
use std.c

cinclude <stdio.h>
cinclude <stdlib.h>
cinclude <string.h>
cinclude <math.h>
cinclude <time.h>

clink "m"

fn main() int {
    c.printf("hello\n")

    size n = c.strlen("hello")
    f64 s = c.sin(3.14159 / 2.0)
    println(f"n={n} sin={s:.4}")

    c.timespec ts = {}
    c.clock_gettime(c.CLOCK_MONOTONIC, &ts)
    println(f"sec = {ts.tv_sec}")
    return 0
}
```

Если заголовок недоступен или bindgen не может его разобрать,
объявления пишут вручную:

```akt
// Ручные C-объявления (заголовок недоступен)
cfn mylib_init(*const cchar name) int
cfn mylib_version() cstring
cfn mylib_set_callback(fn(i32) void callback) void

cstruct mylib_config {
    i32 flags
    size buffer_size
}

cunion mylib_value {
    i64 as_int
    f64 as_float
}
```

Ручное объявление должно точно совпадать с ABI библиотеки.
`...` разрешён только в `cfn` и только для C-функций.

Строковые литералы NUL-терминированы и передаются в `cstring`
напрямую. Значение `string` требует `s.as_c_str()` или
`std.c.cstring(s)`.

Callback:

```akt
cfn atexit(fn() void callback) int

fn cleanup() {
    println("bye")
}

fn main() int {
    c.atexit(&cleanup)
    return 0
}
```

Замыкание с захватами в C передавать нельзя.

---

## 14. Тесты

```akt
// tests/math_test.akt
use math
use std.test

test "add works" {
    assert_eq(math.add(2, 2), 4)
}

test "add is commutative" {
    assert_eq(math.add(2, 3), math.add(3, 2))
}

test "expect panic" {
    assert_throws(|| {
        panic("boom")
    })
}
```

`test` — только на уровне модуля. `katek test` находит
`tests/**/*.akt` и `*_test.akt`.

---

## 15. Полная программа

Ниже — один файл, в котором собраны структуры, enum, generics,
замыкания, арены, `defer`, `Result` и тест.

```akt
// examples/full/inventory.akt
use std.collections
use std.error
use std.sync
use std.test
use std.thread

const i32 MAX_ITEMS = 100
const f64 TAX_RATE = 0.2

static i32 g_created = 0

enum Category { Book, Food, Electronics }

struct Item {
    string name
    Category category
    i32 quantity
    f64 unit_price

    fn subtotal() f64 {
        return (self.quantity as f64) * self.unit_price
    }

    static fn new(string name, Category category, i32 quantity, f64 unit_price) Item {
        g_created += 1
        return Item {
            name: name
            category: category
            quantity: quantity
            unit_price: unit_price
        }
    }
}

struct Inventory {
    Vector<Item> items

    fn add(Item item) Result<void, Error> {
        if (self.items.len() >= MAX_ITEMS as size) {
            return std.error.err[void](1, "inventory is full")
        }
        self.items.push(item)
        return Ok(())
    }

    fn total() f64 {
        f64 sum = 0.0
        for (item in self.items) {
            sum += item.subtotal()
        }
        return sum
    }

    fn with_tax() f64 {
        return self.total() * (1.0 + TAX_RATE)
    }

    fn cheapest() Option<Item> {
        if (self.items.len() == 0) {
            return None
        }
        Item best = self.items[0]
        for (item in self.items) {
            if (item.unit_price < best.unit_price) {
                best = item
            }
        }
        return Some(best)
    }
}

fn parse_category(string s) Result<Category, Error> {
    if (s == "book")        { return Ok(Category::Book) }
    if (s == "food")        { return Ok(Category::Food) }
    if (s == "electronics") { return Ok(Category::Electronics) }
    return std.error.err[Category](2, f"unknown category: {s}")
}

fn grade(i32 score) string {
    return switch (score) {
        case 90..=100: "A"
        case 80..=89:  "B"
        case 70..=79:  "C"
        case 60..=69:  "D"
        default:       "F"
    }
}

fn apply_discount(^Item item, f64 percent) {
    if (percent <= 0.0 || percent >= 1.0) {
        return
    }
    item.unit_price = item.unit_price * (1.0 - percent)
}

fn main() Result<void, Error> {
    Inventory inv = { items: [] }

    inv.add(Item::new("Dune", Category::Book, 2, 12.5))?
    inv.add(Item::new("Apple", Category::Food, 10, 0.8))?
    inv.add(Item::new("Keyboard", Category::Electronics, 1, 79.0))?

    // Замыкания и HOF
    auto expensive = inv.items.filter(|it| it.unit_price > 10.0)
    auto names = inv.items.map(|it| it.name)
    i32 total_qty = inv.items.reduce(0, |acc, it| acc + it.quantity)

    println(f"items = {inv.items.len()}")
    println(f"names = {names}")
    println(f"total quantity = {total_qty}")
    println(f"expensive count = {expensive.len()}")

    // Арена для временных строк
    arena scratch
    dynstr scratch.summary = f"subtotal={inv.total():.2}"
    println(scratch.summary)

    // defer для мьютекса
    auto mtx = Mutex::new()
    mtx.lock()
    defer mtx.unlock()
    println(f"total with tax = {inv.with_tax():.2}")

    // Option
    switch (inv.cheapest()) {
        case Some(item): {
            println(f"cheapest = {item.name} ({item.unit_price:.2})")
        }
        case None: {
            println("inventory is empty")
        }
    }

    // Result и ? внутри функции с совместимым Error
    auto c = parse_category("book")?
    println(f"category = {c}")

    // Изменение через ссылку
    if (inv.items.len() > 0) {
        apply_discount(^inv.items[0], 0.1)
    }

    // Range switch
    println(f"grade(87) = {grade(87)}")

    // Потокобезопасный счётчик
    shared i32 counter = 0
    shared Mutex mtx2 = Mutex::new()
    Vector<thread.Handle> handles = []
    for (i in 0..4) {
        handles.push(thread.spawn(shared || {
            for (j in 0..1000) {
                mtx2.lock()
                defer mtx2.unlock()
                counter += 1
            }
        }))
    }
    for (h in handles) {
        thread.join(h)?
    }
    println(f"counter = {counter}")
    println(f"created = {g_created}")

    return Ok(())
}

fn must(Result<void, Error> r) {
    switch (r) {
        case Ok(_): { }
        case Err(_): { assert(false, "add failed") }
    }
}

test "item subtotal" {
    Item item = Item::new("Test", Category::Book, 2, 12.5)
    assert_eq(item.subtotal(), 25.0)
}

test "inventory total" {
    Inventory inv = { items: [] }
    must(inv.add(Item::new("A", Category::Book, 1, 10.0)))
    must(inv.add(Item::new("B", Category::Food, 2, 5.0)))
    assert_eq(inv.total(), 20.0)
}
```

Что демонстрирует программа:

| Конструкция | Где |
|---|---|
| Константы, `static` | `MAX_ITEMS`, `g_created` |
| `struct` + методы | `Item`, `Inventory` |
| `static fn` | `Item::new` |
| Enum | `Category` |
| `switch`-выражение и диапазоны | `grade` |
| Generics | `Option<Item>`, `Vector<Item>` |
| Замыкания и HOF | `filter`, `map`, `reduce` |
| `Result` + `?` | `add`, `parse_category`, `main` |
| `Option` + `??`/`switch` | `cheapest` |
| `arena` | `scratch` |
| `defer` | `mtx.unlock()` |
| `shared` + `Mutex` | счётчик |
| Ссылки | `apply_discount` |
| Тесты | `test "..."` |

---

## 16. Чего в языке нет

- `let`, `mut`, `try`, `is`, `unsafe`, `macro`, `template`.
- Наследования классов.
- Исключений (`throw`/`catch`).
- Сборщика мусора.
- Препроцессора и `#define`.
- `->` и `::` для модулей.
- Автоматических числовых повышений.
- Приватных полей структуры.
- Вложенных объявлений типов и функций.
