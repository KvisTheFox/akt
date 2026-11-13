# akt — синтаксис и семантика ядра

Это нормативный документ. Если другой файл противоречит ему по
синтаксису — прав `01-syntax.md`. Запреты собраны в
[`09-negative.md`](09-negative.md).

---

## 1. Лексика

### 1.1. Зарезервированные слова (40)

```text
fn        const     static    type      struct
enum      impl      pub       use       if
else      while     for       in        loop
switch    case      default   break     continue
return    defer     arena     shared    weak
manual    cinclude  clink     cfn       cstruct
cunion    async     await     inline    test
true      false     null      self      as
```

Дополнительно:

- **Словесные операторы (3):** `and`, `or`, `not`. Это
  альтернативная запись `&&`, `||`, `!`; они не считаются
  зарезервированными словами и могут быть идентификаторами, если
  стоят не в позиции оператора. Для читаемости не рекомендуется
  использовать их как имена.
- **Контекстные слова (3):** `step` (в диапазоне `for`), `move`
  (модификатор захвата замыкания), `volatile` (квалификатор
  указателя в C-привязках). Слово `shared` уже входит в 40
  зарезервированных; в захвате замыкания используется тот же
  модификатор памяти.
- **Имена встроенных типов** (`void`, `i32`, `string`, `Option`,
  ...) зарезервированы, но не входят в 40 слов. Маркер вывода
  `auto` также нельзя использовать как идентификатор.

Слово `let` **не существует**. Слово `free` не является ключевым:
освобождение — это функция (`std.mem.free`) или C-функция
(`c.free`). Слова `try`, `is`, `unsafe`, `macro`, `template` не
существуют.

### 1.2. Идентификаторы

- Начинаются с буквы Unicode или `_`.
- Далее — буквы, цифры, `_`.
- Стиль: `snake_case` для функций/переменных, `PascalCase` для
  типов, `SCREAMING_SNAKE` для констант.
- Нарушение стиля — предупреждение, не ошибка.
- Идентификаторы чувствительны к регистру.

### 1.3. Комментарии

```akt
// однострочный
/* многострочный */
/// документация для следующего объявления
//! документация модуля
```

Вложенные `/* */` не поддерживаются. Doc-комментарии собираются
`katek doc`.

### 1.4. Точка с запятой и перевод строки

`;` не обязательна в конце строки. Она нужна, если несколько
инструкций стоят в одной строке.

Правило конца инструкции:

1. Инструкция заканчивается на переводе строки, **если** строка не
   заканчивается токеном-продолжением и следующая строка не
   начинается с токена-продолжения.
2. Токены-продолжения в конце строки: бинарные операторы
   (`+ - * / % ** & | ^ << >> && || and or ??`), `=`, `,`, `.`,
   `(`, `[`, `{`, `:`, `?`, `as`, `in`, `step`.
3. Токены-продолжения в начале строки: `)`, `]`, `}`, `.`, `?`,
   `??`, `as`, `else`, `and`, `or`, бинарные операторы.
4. Если следующая строка начинается с `(` или `[`, это **не**
   продолжение: так начинается новая инструкция. Для переноса
   используйте явные скобки или `;`.

```akt
i32 x = 50                 // конец по \n
i32 a = 1; i32 b = 2       // две инструкции в строке

i32 y = 1 +
        2                  // продолжение: строка кончилась на +

i32 z = foo(
    1,
    2
)                          // продолжение: открыта (

auto s = "a"               // конец
(1 + 2)                    // новая инструкция, не продолжение
```

### 1.5. Числовые литералы

```akt
42              // i32 по умолчанию
42u8            // u8
42i64           // i64
42u128          // u128
0xFF            // hex
0b1010          // binary
0o755           // octal
1_000_000       // разделитель
3.14            // f64
3.14f           // f32
1e10            // f64
```

- Целочисленный литерал без суффикса имеет тип `i32`, но может
  использоваться там, где ожидается другой целый тип, если значение
  представимо. Это **типизация литерала**, а не неявное приведение.
- Если значение не представимо в целевом типе — ошибка компиляции.
- Вещественный литерал без суффикса — `f64`; с `f` — `f32`.
- Отрицательные литералы — унарный минус перед литералом.

### 1.6. Строковые литералы

```akt
"hello"                 // string (UTF-8 view)
"a\nb\tc"               // escape
f"x = {x}"              // интерполяция
r"C:\path"              // raw string, без escape
"""
    multiline
    with "quotes"
"""                     // многострочная строка
```

- `string` — неизменяемое представление UTF-8 (`ptr + len`).
- `f"..."` интерполирует выражения в `{}`. Внутри `{}` работает
  обычный синтаксис выражений, включая вложенные строки:
  `f"name = {user["name"]}"`.
- Чтобы вывести `{` или `}`, используйте `{{` и `}}`.
- `r"..."` не обрабатывает escape; кавычку внутри удваивают: `r"a""b"`.
- Многострочная строка не интерполируется, если нет префикса `f`.
- Строки — UTF-8. Невалидный UTF-8 в литерале — ошибка.

Формат внутри `{}`:

```akt
f"{x}"          // обычный вывод
f"{x:.4}"      // 4 знака после запятой (для float)
f"{n:08}"       // ширина 8, ведущие нули
f"{n:>8}"       // выравнивание вправо
f"{n:<8}"       // выравнивание влево
```

Спецификатор начинается с `:` и действует до закрывающей `}`.
Для целых поддерживаются `:0N` и выравнивание; для float —
`:.N`. Других спецификаторов нет.

### 1.7. Символьные литералы

```akt
'A'             // char (Unicode scalar, u32)
'\n'
'\\'
'\u{1F600}'     // code point
```

`char` — не `u8` и не C `char`. Для C используется `cchar`.

---

## 2. Типы

### 2.1. Целые

| Тип | Размер | Диапазон |
|---|---|---|
| `i8` | 1 байт | −128..127 |
| `i16` | 2 байта | −32768..32767 |
| `i32` | 4 байта | −2³¹..2³¹−1 |
| `i64` | 8 байт | −2⁶³..2⁶³−1 |
| `i128` | 16 байт | −2¹²⁷..2¹²⁷−1 |
| `u8` | 1 байт | 0..255 |
| `u16` | 2 байта | 0..65535 |
| `u32` | 4 байта | 0..2³²−1 |
| `u64` | 8 байт | 0..2⁶⁴−1 |
| `u128` | 16 байт | 0..2¹²⁸−1 |
| `int` | C `int` | платформенный; 32 бита на поддерживаемых целях |
| `size` | C `size_t` | беззнаковый платформенный |
| `isize` | C `ptrdiff_t` | знаковый платформенный |
| `cchar` | C `char` | 1 байт; знаковость — как у платформы |

Для точных размеров используйте `iN`/`uN`. `int` нужен для
C-совместимости, а не как «универсальный» тип.

### 2.2. Плавающие

| Тип | Размер |
|---|---|
| `f32` | 4 байта, IEEE 754 binary32 |
| `f64` | 8 байт, IEEE 754 binary64 |

### 2.3. Прочие примитивы

```akt
bool      // true | false
char      // Unicode scalar, 32 бита
void      // только тип возврата; можно опускать
```

`void` — не значение. Нельзя объявить `void x`.

### 2.4. Текстовые типы

| Тип | Что | Владение |
|---|---|---|
| `string` | UTF-8 view (`*const u8` + `size`) | не владеет |
| `dynstr` | UTF-8 буфер с длиной и ёмкостью | владеет, живёт в регионе |
| `cstring` | `*const cchar`, NUL-терминированная | не владеет |

```akt
string s = "hello"          // view на литерал
dynstr d = "hello"          // копия в текущем регионе
d += " world"               // рост (может переаллоцировать)
```

`string` не гарантирует NUL в конце. Для C используйте
`s.as_c_str()` (аллоцирует `cstring` в текущем регионе) или
`std.c.cstring`.

### 2.5. Составные типы

```akt
// Кортеж
(i32, f64, string) t = (42, 3.14, "hello")

// Массив фиксированного размера
[i32; 5] arr = [1, 2, 3, 4, 5]

// Срез — view на массив или Vector
[]i32 s = arr[1..4]

// Сырой указатель (C, manual)
*T
*const T
*volatile T

// Ссылка (безопасная, без владения)
^T
^const T

// Опциональное и ошибочное
Option<T>       // Some(T) | None
Result<T, E>    // Ok(T) | Err(E)
```

Синтаксис массива — **`[T; N]`**, не `T[N]`. Это устраняет
C-деклараторную путаницу.

### 2.6. Struct, cstruct, enum

```akt
struct Vec2 {
    f64 x
    f64 y
}

// C-совместимая структура: порядок полей = ABI
cstruct Timespec {
    i64 tv_sec
    i64 tv_nsec
}

cunion Value {
    i64 as_int
    f64 as_float
}

enum Color { Red, Green, Blue }

enum Shape {
    Circle(f64)
    Rect(f64, f64)
}

// Явные дискриминанты разрешены для enum без payload
enum Level { Debug = 0, Info = 1, Warn = 2, Error = 3 }
```

### 2.7. Псевдонимы

```akt
type Pair = (f64, f64)
type IntList = Vector<i32>
```

Псевдоним — это другое имя типа, а не новый тип. Для нового типа
используйте `struct` с одним полем.

---

## 3. Объявления и переменные

### 3.1. Локальные переменные

```akt
i32 x = 50                 // явный тип
f64 y = 3.14
string s = "hello"

auto z = 100               // вывод типа: i32
auto pi = 3.14             // f64

const i32 MAX = 1000       // локальная compile-time константа
```

Правила:

- Переменная должна быть инициализирована при объявлении.
- `auto` требует инициализатора.
- Тип `auto` не является типом значения; это маркер вывода.
- Все локальные переменные изменяемы. Отдельного `mut` нет.
- `const` — compile-time значение; изменять нельзя.
- `static` — только на уровне модуля.

### 3.2. Модульные переменные

```akt
const i32 MAX = 1000           // compile-time, тип можно опустить
static i32 counter = 0         // изменяемая переменная модуля
static string NAME = "akt"     // статическая строка
```

`static` инициализируется константным выражением. Для ленивой
инициализации используйте `std.sync.Once`.

### 3.3. Деструктуризация

```akt
auto (a, b) = (1, 2)
auto (q, r) = divmod(17, 5)

// Структура
auto { x, y } = point          // поля с теми же именами
auto { x: px, y: py } = point  // переименование

// Массив/срез
auto [first, second] = arr
auto [first, ...rest] = arr    // rest — срез
```

Деструктуризация — это синтаксический сахар над последовательными
присваиваниями; она не создаёт скрытых копий сверх обычных.

---

## 4. Функции

### 4.1. Базовый вид

```akt
fn add(int a, int b) int {
    return a + b
}

fn greet(string name) {          // возврат void
    println(f"Hello, {name}!")
}

fn square(int x) {               // возврат выводится: int
    return x * x
}

fn pi() auto {                   // явный вывод
    return 3.14159               // f64
}
```

### 4.2. Вывод типа возврата

- Если есть `return expr` — тип выводится из всех `return`.
- Если `return` нет или есть `return` без значения — `void`.
- Если типы разных `return` не совпадают — ошибка.
- Рекурсивная функция обязана указывать тип возврата явно.
- `auto` как тип возврата — то же, что опустить тип, но явно.

### 4.3. Параметры

```akt
fn by_value(BigStruct s) { }        // копия
fn by_ref(^BigStruct s) { }         // ссылка, без владения
fn by_const_ref(^const BigStruct s) { }
fn by_ptr(*BigStruct s) { }         // сырой указатель
fn by_const_ptr(*const BigStruct s) { }
```

Параметр `Type name` передаётся по значению (копия). Для больших
структур предпочтителен `^Type`.

### 4.4. Перегрузка

```akt
fn add(int a, int b) int { return a + b }
fn add(f64 a, f64 b) f64 { return a + b }
fn add(string a, string b) string { return a + b }

add(1, 2)          // int
add(1.0, 2.0)      // f64
add("a", "b")      // string
add(1u8, 2u8)      // ошибка: точного совпадения нет,
                   // неявных повышений нет
```

Правила разрешения:

1. Точное совпадение типов (с учётом типизации литералов).
2. Никаких стандартных повышений (`i8 → i32`, `f32 → f64`).
3. Если точного совпадения нет — fallback-перегрузка с
   нетипизированными параметрами.
4. Если подходят два кандидата — ошибка неоднозначности.

`fn add(i32, i32)` и `fn add(int, int)` на цели, где `int == i32`,
считаются одной сигнатурой и конфликтуют.

### 4.5. Fallback-перегрузка

```akt
fn describe(int x) string    { return f"int: {x}" }
fn describe(f64 x) string    { return f"f64: {x}" }
fn describe(x)               { return f"other: {x}" }  // fallback
```

- Fallback — это шаблон с неявными generic-параметрами.
- У fallback все параметры должны быть без типов.
- На имя функции допускается не более одного fallback.
- Fallback мономорфизируется под каждый вызов.
- Fallback нельзя брать как значение функции (`&describe`).
- Fallback не может быть рекурсивным без явной типизации.

### 4.6. Generics

```akt
fn max<T>(T a, T b) T {
    return a > b ? a : b
}

fn pair<T, U>(T a, U b) (T, U) {
    return (a, b)
}

// Явные аргументы типа — в квадратных скобках
auto m = max[i32](3, 7)
auto p = pair[i32, string](1, "hello")
```

- Параметры типа выводятся из аргументов, если не указаны явно.
- Явная форма — `f[T1, T2](args)`, чтобы не конфликтовать с `<`.
- Generics мономорфизируются: для каждого набора типов создаётся
  отдельная C-функция.
- Ограничений (`where T: Ord`) нет. Операции проверяются при
  инстанцировании: если для `T` нет `>`, будет ошибка.
- Generic-типы: `Vector<T>`, `Map<K, V>`, `Option<T>`.

### 4.7. Множественный возврат

```akt
fn divmod(int a, int b) (int, int) {
    return (a / b, a % b)
}

auto (q, r) = divmod(17, 5)

// Имена в типе возврата — документация, не отдельные поля
fn stats(Vector<f64> xs) (f64 mean, f64 var) {
    // ...
    return (mean, var)
}
```

### 4.8. `inline`, `const fn`, `async`

```akt
inline fn square(int x) int { return x * x }

const fn factorial(int n) int {
    if (n <= 1) { return 1 }
    return n * factorial(n - 1)
}

async fn fetch(i32 id) Result<User, string> {
    auto resp = await http.get(f"/users/{id}")
    return Ok(resp.json_as(User))
}
```

- `inline` — подсказка компилятору и `static inline` в C.
- `const fn` вычисляется на этапе компиляции. Разрешены только
  детерминированные операции без I/O, аллокаций и глобального
  состояния.
- `async fn` возвращает future; `await` допустим только внутри
  `async`-контекста. Рантайм — `std.async`.

Порядок модификаторов: `pub inline const fn`, `pub async fn`.
`const` и `async` взаимоисключающи.

### 4.9. Указатели на функции

```akt
fn(i32) i32 fptr = &square
println(fptr(7))

// C-callback
cfn qsort(*void base, size n, size size,
          fn(*const void, *const void) i32 cmp) void
```

Функция, передаваемая в C, должна иметь C-совместимые типы
параметров и возврата. Замыкания в C передавать нельзя.

---

## 5. Замыкания

```akt
auto double = |x| x * 2
auto add = |a, b| a + b
auto thunk = || { println("later") }

// Захват
auto n = 10
auto f = |x| x + n          // захват по ссылке (^n)
auto g = move |x| x + n     // захват по значению (копия)
auto h = shared |x| x + n   // захват shared-копии
```

Правила:

- По умолчанию замыкание захватывает по ссылке (`^T`).
- `move` захватывает по значению; для `manual`/`shared` это передача
  владения.
- `shared` захватывает `shared`-копию (refcount +1).
- Замыкание, которое переживает текущий регион (например,
  передаётся в `thread.spawn`), не может захватывать `auto`/`arena`
  по ссылке. Компилятор требует `move` или `shared`.
- Замыкание без захватов можно привести к `fn(...)`.
- Замыкание с захватами — это `std.fn.Closure`, не C-callback.

---

## 6. Struct

### 6.1. Объявление

```akt
struct Vec2 {
    f64 x
    f64 y

    fn length() f64 {
        return c.sqrt(self.x * self.x + self.y * self.y)
    }

    fn add(Vec2 other) Vec2 {
        return Vec2 { x: self.x + other.x, y: self.y + other.y }
    }
}
```

- `self` — неявный первый параметр метода, тип `^Self`.
- Методы объявляются внутри `struct` или в `impl`.
- `static fn new() T` — ассоциированная функция без `self`;
  вызывается как `T::new()`.
- Поля могут иметь значения по умолчанию: `i32 count = 0`.
- Методы не влияют на layout.
- Наследования нет. Композиция — через поля.

### 6.2. `impl` для расширения

```akt
impl Vec2 {
    fn scale(f64 factor) Vec2 {
        return Vec2 { x: self.x * factor, y: self.y * factor }
    }
}

impl string {
    fn shout() dynstr {
        return self.to_upper() + "!"
    }
}
```

`impl` может расширять как свои, так и встроенные типы. Один и тот
же метод нельзя объявить дважды.

### 6.3. Создание

```akt
Vec2 a = { x: 1.0, y: 2.0 }
Vec2 b = Vec2 { x: 1.0, y: 2.0 }    // то же самое

// Пустой литерал допустим, если у всех полей есть значения
// по умолчанию; иначе — ошибка.
Vec2 c = Vec2 {}
```

Поля инициализируются по имени. Позиционная инициализация
запрещена (кроме C-структур через `cinclude`/`cstruct` и явного
`cstruct`-литерала).

### 6.4. Вложенные структуры

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

Объявления типов допустимы **только на уровне модуля**. Внутри
функции можно объявлять переменные, но не `struct`/`enum`/`fn`.

---

## 7. Enum и pattern matching

### 7.1. Варианты

```akt
enum Color { Red, Green, Blue }

Color c = Color::Red

enum Shape {
    Circle(f64)
    Rect(f64, f64)
    Triangle(f64, f64)
}

Shape s = Shape::Circle(5.0)
```

`::` разрешён **только** для двух вещей:

- вариантов enum: `Color::Red`;
- ассоциированных элементов типа: `Cache::new()`, `Vec2::zero()`.

Для модулей используется `.`: `std.io`, `math.add`, `io.read`.
`use std::io` — ошибка.

### 7.2. `switch`

```akt
switch (s) {
    case Shape::Circle(r): {
        println(f"circle r={r}")
    }
    case Shape::Rect(w, h): {
        println(f"rect {w}x{h}")
    }
    case Shape::Triangle(b, h): {
        println(f"tri {b}x{h}")
    }
}
```

Правила:

- Fallthrough отсутствует. `break` не нужен.
- Каждый `case` — блок или одна инструкция.
- `switch` по enum обязан покрывать все варианты или иметь
  `default`/`case _`.
- `default` эквивалентен `case _` и должен быть последним.
- Допустимы guards: `case Some(x) if x > 0:`.
- Допустимы range-паттерны: `case 0..=9:`.
- В `switch`-выражении все ветви должны давать один тип.

### 7.3. `switch` как выражение

```akt
auto name = switch (color) {
    case Color::Red:   "red"
    case Color::Green: "green"
    case Color::Blue:  "blue"
}
```

Если `switch` используется как выражение, `break` внутри запрещён,
а `default` обязателен, если покрыты не все значения.

### 7.4. `Option` и `Result`

`Option<T>` и `Result<T, E>` — встроенные enum:

```akt
Option<i32> a = Some(42)
Option<i32> b = None

Result<i32, string> ok = Ok(10)
Result<i32, string> err = Err("failed")

switch (a) {
    case Some(v): { println(f"got {v}") }
    case None:    { println("none") }
}

// ?? — значение по умолчанию для Option
i32 x = a ?? -1

// ? — проброс ошибки в функции, возвращающей Result
fn parse(string s) Result<i32, string> {
    auto n = std.fmt.parse_int(s)?
    return Ok(n)
}
```

`?` применим только к `Result<T, E>` в функции с совместимым `E`.
Для `Option` используйте `??` или `switch`. Неявного
преобразования `Option` → `Result` нет.

---

## 8. Управление потоком

### 8.1. `if` / `else`

```akt
if (x > 0) {
    println("positive")
} else if (x < 0) {
    println("negative")
} else {
    println("zero")
}

auto sign = if (x >= 0) { "+" } else { "-" }
```

- Скобки вокруг условия обязательны.
- Фигурные скобки обязательны.
- Условие — строго `bool`.
- `if`-выражение требует `else` и одинаковый тип ветвей.

### 8.2. `while`

```akt
while (i < 10) {
    i += 1
}
```

### 8.3. `for` (C-стиль)

```akt
for (i32 i = 0; i < 10; i += 1) {
    println(i)
}
```

### 8.4. `for-in`

```akt
for (item in items) {
    println(item)
}

for (i, item in items) {
    println(f"{i}: {item}")
}
```

Итерируемое значение должно реализовывать протокол итерации:
`iter()` → `Iterator<T>`. Массивы, срезы, `Vector`, `Map`, `Set`,
диапазоны и `string` (по code points) поддерживаются в std.

### 8.5. Диапазоны

```akt
for (i in 0..10)       { }   // 0..9
for (i in 0..=10)      { }   // 0..10
for (i in 0..10 step 2){ }   // 0, 2, 4, 6, 8
for (i in (0..10).rev()) { } // через std.iter
```

`step` — контекстное слово, допустимое только в `for`-диапазоне.
Шаг должен быть положительным целым литералом или `const`.

### 8.6. `loop`

```akt
loop {
    if (done) { break }
}
```

### 8.7. Метки

```akt
outer: for (i in 0..5) {
    for (j in 0..5) {
        if (i * j > 6) { break outer }
        if (j == 2) { continue outer }
    }
}
```

Метка ставится перед `for`, `while`, `loop` или блоком. `break` и
`continue` принимают метку.

### 8.8. `defer`

```akt
fn read() Result<void, std.error.Error> {
    auto f = std.io.open("data.txt")?
    defer f.close()
    // ...
    return Ok(())
}
```

`defer` выполняет инструкцию при выходе из текущей области
видимости в порядке LIFO, в том числе при `return`, `break`,
`continue` и пробросе ошибки через `?`.

Форма `decl defer stmt` — сахар:

```akt
fn read2() Result<void, std.error.Error> {
    auto f = std.io.open("data.txt")? defer f.close()
    return Ok(())
}
```

---

## 9. Операторы

### 9.1. Приоритет (от высокого к низкому)

| Уровень | Операторы | Ассоциативность |
|---|---|---|
| 1 | `f(x)`, `a[i]`, `a.b`, `x?` | слева |
| 2 | `!` `not` `~` `-` `+` `^` `*` `&` (префиксные) | справа |
| 3 | `**` | справа |
| 4 | `*` `/` `%` | слева |
| 5 | `+` `-` | слева |
| 6 | `<<` `>>` | слева |
| 7 | `&` | слева |
| 8 | `^` | слева |
| 9 | `\|` | слева |
| 10 | `==` `!=` `<` `>` `<=` `>=` | слева |
| 11 | `&&` `and` | слева |
| 12 | `\|\|` `or` | слева |
| 13 | `??` | справа |
| 14 | `?:` | справа |
| 15 | `..` `..=` | не ассоциируются |
| 16 | `as` | слева |
| 17 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | справа |

Скобки всегда разрешены и рекомендуются при сомнении.

### 9.2. Арифметика

```text
+  -  *  /  %  **
```

- `/` для целых — целочисленное деление; деление на ноль всегда
  диагностируется и завершает программу (и в debug, и в release).
  Это стоит одной ветки.
- `%` — остаток; знак — как у делимого.
- `**` — возведение в степень; для целых — только неотрицательный
  целый показатель; для float — `pow`.
- Переполнение целых: ошибка в debug, wrapping в release **только**
  при явном `std.math.wrapping`; по умолчанию — проверка и panic.
  Это отличается от C и должно быть явно указано в профиле.

### 9.3. Битовые

```text
&  |  ^  ~  <<  >>
```

Сдвиг на величину ≥ разрядности — ошибка (debug) / маска (release
только при явном `std.math.wrapping_shl`).

### 9.4. Логика

```text
&&  ||  !        // или: and or not
```

`&&`/`||` — короткое замыкание, операнды только `bool`.

### 9.5. Сравнение

```text
==  !=  <  >  <=  >=
```

Сравнение структур и enum — поэлементное, если все поля сравнимы.
Сравнение `string` — лексикографическое по code points. Указатели
сравниваются как адреса; `null` — только с `*T`/`cstring`.

### 9.6. Присваивание

```text
=  +=  -=  *=  /=  %=  &=  |=  ^=  <<=  >>=
```

`a = b` — копия значения. `a = ^b` — ссылка (если тип `a` — `^T`).

### 9.7. Тернарный оператор

```akt
auto result = cond ? a : b
```

`cond` — `bool`; ветви одного типа; ленивое вычисление.

### 9.8. Null-coalescing

```akt
auto x = maybe ?? default_value
```

Работает для `Option<T>`: если `Some(v)` — `v`, если `None` —
`default_value`. Для `*T` не применяется (там явная проверка
`p == null`).

### 9.9. Приведение

```akt
auto f = x as f64
auto u = i as u8          // проверка диапазона
auto p = q as *void       // pointer cast
auto c = s.as_c_str()
```

Правила:

- `as` — единственный способ преобразования между числовыми типами.
- `as` между числовыми типами проверяет диапазон всегда; при
  потере значения — паника. Для явного усечения используйте
  `std.math.truncate`.
- `*T` ↔ `*void` — явный `as`.
- `*T` ↔ `^T` — запрещено; это разные модели.
- `string` → `dynstr` — явная копия: `s.to_dynstr()`.

### 9.10. Доступ и вызов

```akt
obj.field              // поле
obj.method()           // метод
arr[i]                 // индекс
ptr.field              // авто-разыменование указателя
ref.field              // авто-разыменование ссылки
^x                     // взять ссылку
&x                     // взять сырой указатель
*x                     // разыменовать
x?                     // пробросить ошибку
```

`ptr.field` и `ref.field` — синтаксический сахар для `(*ptr).field`.
Для указателя на указатель `pp.field` разыменовывает один уровень;
для второго используйте `(*pp).field`.

Индексация `arr[i]` проверяет границы и завершает программу с
диагностикой при выходе за пределы. Для намеренно unchecked-доступа
используйте `std.ptr.read`/`std.ptr.write` с явным указателем.

---

## 10. Модули

### 10.1. Файл = модуль

Каждый `.akt` — модуль. Заголовочных файлов нет.

```akt
// src/math.akt
pub fn add(int a, int b) int { return a + b }

fn helper() { }        // приватный
```

Имя модуля — путь от `src/` без расширения, с точками:
`src/net/http/server.akt` → `net.http.server`.

### 10.2. Импорт

```akt
use math                       // свой модуль src/math.akt
use std.io                     // std
use std.http.server            // вложенный std
use mypkg.submodule            // пакет из зависимости
use std.math as m              // псевдоним
pub use math                   // реэкспорт
```

`use a.b.c`:

- импортирует все публичные имена модуля в текущую область;
- связывает имя `c` как пространство имён для функций, констант и
  типов: `c.fn()`, `c.Type`;
- при конфликте имён требуется `as` или полное имя.

`::` в `use` запрещён. `use std::io` — ошибка; правильно
`use std.io`.

### 10.3. Публичность

```akt
pub fn public_fn() { }
pub struct PublicStruct { }
pub const PUBLIC_CONST = 42

fn private_fn() { }            // приватный по умолчанию
```

`pub use` реэкспортирует имена. В `pub struct` **все поля
публичны**. Приватных полей нет — это осознанное упрощение. Для
инкапсуляции используйте методы и непрозрачные типы в модуле.

---

## 11. C-интероп

### 11.1. Заголовки

```akt
cinclude <stdio.h>       // системный заголовок
cinclude "vendor/foo.h"  // локальный заголовок
```

`cinclude` запускает bindgen: разбирает объявления C и создаёт
привязки. Имена доступны в пространстве `c` и, если нет конфликта,
без префикса.

Строковые **литералы** NUL-терминированы и могут передаваться в
параметры `cstring`/`*const cchar` напрямую. Значение типа `string`
так не передаётся: нужен явный `s.as_c_str()` (аллоцирует копию в
текущем регионе).

```akt
cinclude <stdio.h>

fn main() int {
    c.printf("hello\n")
    printf("hello\n")       // то же, если нет конфликта
    return 0
}
```

Include-пути берутся из `[native]` в `katek.toml` и из
провайдеров зависимостей (xrepo/vcpkg/CMake/pkg-config).

### 11.2. Линковка

```akt
clink "m"                       // -lm
clink "pthread"                 // -lpthread
clink path "vendor/libfoo.a"    // явный файл
clink framework "Cocoa"         // macOS framework
```

Дубликаты `clink` дедуплицируются. Порядок линковки
детерминирован. Предпочтительный способ — `[native]` в манифесте;
`clink` нужен для ручных случаев.

### 11.3. Ручные объявления

Если заголовок не парсится или нужна тонкая настройка:

```akt
cfn strlen(*const cchar s) size
cfn printf(*const cchar fmt, ...) int

cstruct timespec {
    i64 tv_sec
    i64 tv_nsec
}

cunion ieee_double {
    f64 value
    [u8; 8] bytes
}
```

`cfn` объявляет внешнюю C-функцию. Variadic `...` разрешён только
в `cfn` и только для C-функций.

### 11.4. Типы C ↔ akt

| C | akt |
|---|---|
| `char` | `cchar` |
| `signed char` | `i8` |
| `unsigned char` | `u8` |
| `short` | `i16`/`u16` (по знаковости) |
| `int` | `int` |
| `unsigned int` | `u32` (на поддерживаемых целях) |
| `long` | `i64`/`i32` по ABI; bindgen подставляет точный |
| `size_t` | `size` |
| `ptrdiff_t` | `isize` |
| `float` | `f32` |
| `double` | `f64` |
| `void*` | `*void` |
| `T*` | `*T` |
| `const T*` | `*const T` |
| `char*` | `*cchar` |
| `const char*` | `cstring` |
| `struct S` | `cstruct S` |
| `union U` | `cunion U` |
| `enum E` | `enum E` (unit-only, с дискриминантами) |
| `bool` (`stdbool.h`) | `bool` |

### 11.5. Указатели и `null`

```akt
*i32 p = null
if (p == null) { println("null") }

cstring s = null
```

`null` допустим только для `*T`, `*const T`, `*volatile T` и
`cstring`. Для «может быть, а может нет» в akt-коде — `Option<T>`.

### 11.6. Callback'и

```akt
cfn atexit(fn() void callback) int

fn cleanup() { println("bye") }

fn main() int {
    c.atexit(&cleanup)
    return 0
}
```

C-callback не может быть замыканием с захватами. Для передачи
контекста используйте `*void userdata` и явный `cast`/`as`.

### 11.7. `volatile`

```akt
*volatile u32 reg = 0x4000_0000 as *volatile u32
u32 value = *reg
*reg = value | 1
```

`volatile` — квалификатор указателя, не отдельный тип. Он
запрещает оптимизации чтения/записи. Используется в драйверах и
MMIO.

---

## 12. Тесты

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
```

- `test "name" { }` — только на уровне модуля.
- `assert`, `assert_eq`, `assert_ne`, `assert_throws` — из
  `std.test`.
- `katek test` находит `tests/**/*.akt` и `*_test.akt`.
- `katek test --filter add` фильтрует по имени.

---

## 13. Точка входа

```akt
fn main() int {
    return 0
}
```

Допустимые формы:

```akt
fn main() { }                                  // код возврата 0
fn main() int { return 0; }                    // код возврата
async fn main() int { return 0; }              // с block_on
fn main(i32 argc, *const *const cchar argv) int { return 0; }
fn main() Result<void, std.error.Error> {      // Ok(()) → 0, Err → 1
    return Ok(())
}
async fn main() Result<void, std.error.Error> { // то же, с block_on
    return Ok(())
}
```

В форме с `Result` ошибка печатается в stderr, а процесс
завершается с ненулевым кодом. В `main() int` оператор `?`
запрещён.

Только одна точка входа на бинарник. Для библиотеки `main` не
нужен.

---

## 14. Форматирование

`katek fmt` приводит код к каноническому виду:

- отступ — 4 пробела;
- `{` на той же строке;
- один оператор на строку;
- запятая в конце многострочных списков;
- пустая строка между объявлениями верхнего уровня;
- `;` только при нескольких инструкциях в строке.

Спорные случаи решает `katek fmt`, а не вкус.

---

## 15. Полный пример ядра

```akt
// full_core.akt — тур по ядру без std (кроме C через cinclude)

cinclude <stdio.h>
cinclude <stdlib.h>
cinclude <string.h>
cinclude <math.h>
cinclude <time.h>

const i32 MAX_SIZE = 1000
const f64 PI_VALUE = 3.141592653589793
const string APP_NAME = "akt demo"

static i32 g_counter = 0
static bool g_ready = false

type Pair = (f64, f64)

struct Vec2 {
    f64 x
    f64 y

    fn length() f64 {
        return c.sqrt(self.x * self.x + self.y * self.y)
    }

    fn add(Vec2 other) Vec2 {
        return Vec2 { x: self.x + other.x, y: self.y + other.y }
    }

    fn dot(Vec2 other) f64 {
        return self.x * other.x + self.y * other.y
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

enum Direction { North, South, East, West }

enum Shape {
    Circle(f64)
    Rect(f64, f64)
    Triangle(f64, f64)
}

fn describe(int x) string    { return f"int: {x}" }
fn describe(f64 x) string    { return f"f64: {x}" }
fn describe(string x) string { return f"string: {x}" }
fn describe(x)               { return f"other: {x}" }

fn max_of<T>(T a, T b) T {
    return a > b ? a : b
}

fn swap<T>(*T a, *T b) {
    T tmp = *a
    *a = *b
    *b = tmp
}

fn divmod(int a, int b) (int, int) {
    return (a / b, a % b)
}

const fn factorial(int n) int {
    if (n <= 1) { return 1 }
    return n * factorial(n - 1)
}

fn square(i32 x) i32 { return x * x }

fn try_div(f64 a, f64 b) Result<f64, string> {
    if (b == 0.0) { return Err("division by zero") }
    return Ok(a / b)
}

fn chained() Result<f64, string> {
    auto x = try_div(10.0, 2.0)?
    auto y = try_div(x, 5.0)?
    return Ok(y)
}

test "add works" {
    assert(2 + 2 == 4)
}

fn main() int {
    // Примитивы
    i8 a_i8 = -128
    u8 b_u8 = 255
    f32 f32v = 3.14f
    f64 f64v = 3.141592653589793
    bool flag = true
    char ch = 'A'
    int native = 42
    size sz = 1024
    isize isz = -1024

    println(f"i8={a_i8} u8={b_u8}")
    println(f"f32={f32v} f64={f64v} bool={flag} char={ch}")
    println(f"int={native} size={sz} isize={isz}")

    // Операторы
    i32 a = 10
    i32 b = 3
    println(f"a + b = {a + b}")
    println(f"a ** 2 = {a ** 2}")
    println(f"a & b = {a & b}")
    println(f"a == b = {a == b}")
    println(f"a && b = {a > 0 && b > 0}")
    auto ternary = a > b ? "bigger" : "smaller"
    println(ternary)

    Option<i32> maybe = None
    println(f"maybe ?? -1 = {maybe ?? -1}")

    // Строки
    string view = "immutable view"
    dynstr dyn = "dynamic"
    dyn += " — extended"
    println(f"{view} | {dyn}")
    println(f"len = {view.len()} first = {view[0]}")
    println(f"slice = {view[1..4]}")

    // Массивы, срезы, кортежи
    [i32; 5] arr = [10, 20, 30, 40, 50]
    for (i, v in arr) {
        println(f"arr[{i}] = {v}")
    }

    []i32 slice = arr[1..4]
    println(f"slice.len() = {slice.len()}")

    (i32, f64, string) tup = (42, 3.14, "hello")
    println(f"tuple: {tup.0}, {tup.1}, {tup.2}")
    auto (ti, tf, ts) = tup
    println(f"destructured: {ti}, {tf}, {ts}")

    // Указатели и ссылки
    i32 pv = 10
    i32 qv = 20
    *i32 pa = &pv
    *i32 pb = &qv
    swap(pa, pb)
    println(f"after swap: pv={pv}, qv={qv}")

    Vec2 vstruct = { x: 1.5, y: 2.5 }
    *Vec2 sp = &vstruct
    sp.x = 10.0
    println(f"vstruct.x={vstruct.x}")

    *i32 null_p = null
    if (null_p == null) { println("null handled") }

    Vec2 vv = { x: 3.0, y: 4.0 }
    ^Vec2 vref = ^vv
    println(f"vref.length() = {vref.length()}")

    fn(i32) i32 fptr = &square
    println(f"fptr(7) = {fptr(7)}")

    // Struct и impl
    Vec2 a1 = { x: 3.0, y: 4.0 }
    Vec2 a2 = { x: -1.0, y: 2.5 }
    println(f"a1.length() = {a1.length()}")
    println(f"a1.dot(a2) = {a1.dot(a2)}")
    auto norm = a1.normalize()
    println(f"norm = ({norm.x:.4}, {norm.y:.4})")

    // Enum
    Direction dir = Direction::North
    auto dir_name = switch (dir) {
        case Direction::North: "N"
        case Direction::South: "S"
        case Direction::East:  "E"
        case Direction::West:  "W"
    }
    println(f"dir = {dir_name}")

    Shape shape = Shape::Circle(5.0)
    f64 area = switch (shape) {
        case Shape::Circle(r):      PI_VALUE * r * r
        case Shape::Rect(w, h):     w * h
        case Shape::Triangle(b, h): 0.5 * b * h
    }
    println(f"area = {area:.4}")

    // Управление
    for (i in 0..5) { print(f"{i} ") }
    println("")
    for (i in 0..10 step 2) { print(f"{i} ") }
    println("")

    i32 loopy = 0
    loop {
        loopy += 1
        if (loopy >= 5) { break }
    }
    println(f"looped {loopy} times")

    outer: for (i in 0..5) {
        for (j in 0..5) {
            if (i * j > 6) { break outer }
        }
    }

    // Память
    {
        dynstr tmp = "auto"
        tmp += " freed"
        println(f"auto: {tmp}")
    }

    {
        manual *i32 buf = c.malloc(4) as *i32
        defer c.free(buf as *void)
        *buf = 42
        println(f"manual buf: {*buf}")
    }

    {
        arena ar
        i32 ar.counter = 0
        dynstr ar.msg = "in arena"
        ar.counter += 10
        println(f"arena: {ar.counter}, {ar.msg}")
    }

    {
        arena early
        i32 early.val = 5
        println(f"early.val = {early.val}")
        arena_free(early)
    }

    {
        defer println("defer 3 (last)")
        defer println("defer 2")
        defer println("defer 1 (first)")
        println("body first")
    }

    // Option / Result
    Option<i32> some_v = Some(42)
    Option<i32> empty_v = None
    println(f"some ?? -1 = {some_v ?? -1}")
    println(f"empty ?? -1 = {empty_v ?? -1}")

    Result<i32, string> ok_r = Ok(10)
    Result<i32, string> err_r = Err("failed")
    switch (ok_r) {
        case Ok(v):  { println(f"ok: {v}") }
        case Err(e): { println(f"err: {e}") }
    }
    switch (err_r) {
        case Ok(v):  { println(f"ok: {v}") }
        case Err(e): { println(f"err: {e}") }
    }
    switch (chained()) {
        case Ok(v):  { println(f"chained: {v}") }
        case Err(e): { println(f"chained err: {e}") }
    }

    // Перегрузка и generics
    println(describe(42))
    println(describe(3.14))
    println(describe("hello"))
    println(describe(true))
    println(f"max = {max_of[i32](3, 7)}")
    println(f"max f64 = {max_of[f64](3.14, 2.71)}")
    println(f"factorial(5) = {factorial(5)}")

    // C-интероп
    i32 atoi_val = c.atoi("12345")
    f64 atof_val = c.atof("3.14")
    size slen = c.strlen("hello")
    println(f"atoi={atoi_val} atof={atof_val} strlen={slen}")
    println(f"sin(PI/2) = {c.sin(PI_VALUE / 2.0)}")

    c.timespec ts = {}
    c.clock_gettime(c.CLOCK_MONOTONIC, &ts)
    i64 now_ms = (ts.tv_sec as i64) * 1000 + (ts.tv_nsec as i64) / 1000000
    println(f"monotonic ms = {now_ms}")

    c.printf("direct printf: %d, %s\n", 42, "hello")

    // Глобальное состояние
    g_counter = 100
    g_counter += 50
    g_ready = true
    println(f"global: g_counter={g_counter}, g_ready={g_ready}")

    return 0
}
```

Этот пример не использует std. В реальном коде для `sqrt`,
`strlen`, `malloc` и т.п. чаще берут `std.math`, `std.mem`,
`std.string`, а `cinclude` — для библиотек, которых нет в std.
