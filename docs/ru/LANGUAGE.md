# Язык akt: практическое руководство

Это руководство описывает **реализованное сейчас** подмножество akt.
Полная спецификация — в [`../design/01-syntax.md`](../design/01-syntax.md).
Если реализация и спецификация расходятся, в этом репозитории действует
реализация; расхождение нужно закрыть тестом и правкой.

## 1. Файлы и модули

- Исходник — `.akt`.
- `use a.b` загружает `a/b.akt` относительно `src/`.
- Сейчас модули используют одно глобальное пространство имён: имена
  функций/структур/enum должны быть уникальны.
- `module.func(...)` разрешается в глобальную `func(...)`.
- Точка входа — `fn main() int`, `fn main()` или
  `fn main() Result<void, E>`.
- `use std.*` — встроенные модули, отдельные файлы не нужны.

## 2. Комментарии

```akt
// однострочный
/* многострочный */
```

Вложенные `/* */` не поддерживаются.

## 3. Типы

| Тип | C-представление | Пример |
|---|---|---|
| `int` | `int` | `int x = 42` |
| `i8..i64` | `intN_t` | `i32 x = 7` |
| `u8..u64` | `uintN_t` | `u64 n = 10` |
| `f32`, `f64` | `float`, `double` | `f64 pi = 3.14` |
| `bool` | `bool` | `bool ok = true` |
| `string` | `AktString` | `string s = "hi"` |
| `char` | `uint32_t` | `char c = 'A'` |
| `[T; N]` | C-массив | `[i32; 3] a = [1,2,3]` |
| `Vector<T>` | `AktVec` | `Vector<f64> xs = []` |
| `*T` | `T*` | `*i32 p` |
| `^T` | `T*` + проверки (план) | `^Vec2 r` |
| struct | `typedef struct` | `Point p` |
| enum | C enum | `Color c` |
| `cstruct`/`cunion` | `typedef struct/union` | `cstruct Pair { ... }` |
| `cfn` | прототип C-функции | `cfn strlen(*const cchar s) size` |
| `cstring` | `const char*` | `cstring s = null` |
| `size`/`isize` | `size_t`/`ptrdiff_t` | `size n = strlen(s)` |

Псевдонимы (`type`) подставляются при разборе:

```akt
type Count = i64
type Handle = *void

Count n = 3
```

`auto` — не тип, а маркер вывода:

```akt
auto x = 10        // int
auto s = "hello"   // string
auto xs = [1,2,3]  // [int; 3]
```

## 4. Литералы

```akt
42
0xFF
0b1010
3.14
1.0e9
true
false
'A'
"hello"
f"x = {x}, y = {y + 1}"
```

Строковые литералы поддерживают `\n`, `\t`, `\r`, `\\`, `\"`, `\'`,
`\0`, `\xNN`.

## 5. Выражения

### Арифметика

```akt
a + b
a - b
a * b
a / b
a % b
a ** b       // pow, возвращает f64
```

### Сравнения и логика

```akt
a == b
a != b
a < b
a > b
a <= b
a >= b
a && b
a || b
!a
not a
```

`and`, `or`, `not` — словесные формы.

### Тернарный оператор

```akt
auto label = x > 0 ? "positive" : "negative"
```

### `??`

Для строк: если строка непустая — она, иначе значение по умолчанию.
Для указателей — проверка на `null`.

```akt
string home = env.get("HOME") ?? "unknown"
```

### Приведение

```akt
f64 x = 3.14
i32 n = x as i32
```

Сейчас `as` — обычный C-cast; проверка диапазона запланирована.

### Вызовы и доступ

```akt
f(1, 2)
obj.field
obj.method()
arr[i]
vec[i]
```

Индексация `Vector` и `string` проверяет границы в рантайме.

### f-строки

```akt
println(f"user={user.name} age={user.age}")
```

Сейчас f-строка превращается в цепочку `akt_str_concat` и
`akt_str_from_*`. Для чисел с плавающей точкой можно задать точность:

```akt
println(f"pi = {pi:.2}")   // pi = 3.14
```

## 6. Переменные

```akt
int x = 10
auto y = 20
string name = "akt"
Vector<i32> v = [1, 2, 3]
[i32; 3] a = [1, 2, 3]
```

На верхнем уровне переменные объявляются так же, без `static`:

```akt
i32 score = 0
f64 camera_x = 0.0
bool running = true
[Cube; 5] cubes
```

Это обычные глобальные переменные — как в C. `static`/`const` тоже
принимаются, но не обязательны.

Присваивание:

```akt
x = 11
x += 1
x -= 1
x *= 2
x /= 2
x %= 2
```

Для строк `+=` делает конкатенацию.

## 7. Управление потоком

```akt
if (cond) {
    ...
} else if (other) {
    ...
} else {
    ...
}
```

```akt
while (cond) {
    ...
}

loop {
    if (done) { break }
}

for (i32 i = 0; i < 10; i += 1) {
    ...
}

for (i in 0..10) {
    ...
}

for (i in 0..=10 step 2) {
    ...
}

for (x in array_or_vector_or_string) {
    ...
}

for (idx, x in array_or_vector) {
    ...
}
```

`break` и `continue` работают в циклах.

## 8. Функции

```akt
fn add(int a, int b) int {
    return a + b
}

fn greet(string name) {
    println(f"hello, {name}")
}
```

- Параметры: `Тип имя`.
- Возвращаемый тип можно опустить — тогда `void`.
- `return` без значения — для `void`.
- Рекурсия поддерживается.

`pub fn` пока равен обычному `fn`.

## 9. Структуры

```akt
struct User {
    string name
    i32 age

    fn birthday() {
        self.age += 1
    }

    static fn guest() User {
        return User { name: "guest", age: 0 }
    }
}
```

- Поля инициализируются по имени: `User { name: "A", age: 1 }`.
- Метод получает `self` как `User*`.
- `static fn` не имеет `self`; вызов `User::guest()`.
- `impl User { ... }` поддерживается и добавляет методы к уже
  объявленной структуре.

## 10. Enum

```akt
enum Color { Red, Green, Blue }

fn main() int {
    Color c = Color::Red
    if (c == Color::Green) {
        println("green")
    }
    return 0
}
```

Сейчас поддерживаются простые enum и payload-enum (`Shape::Circle(f64)`).
`switch` работает как statement и выражение; payload-варианты можно
распаковывать в `case`.

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

## 11. Массивы и Vector

```akt
[i32; 5] fixed = [1, 2, 3, 4, 5]
println(f"len={len(fixed)}")

Vector<string> names = []
names.push("Alice")
names.push("Bob")
println(f"{names.len()} {names[0]}")
```

`Vector` methods:

- `.len()`
- `.push(x)`
- `.pop()`
- `.clear()`

`Vector` хранит элементы через `void*` и `elem_size`; тип элементов
знает компилятор.

## 12. `defer`

```akt
fn read() int {
    defer println("close file")
    if (bad) {
        return -1
    }
    return 0
}
```

`defer` срабатывает:

- при обычном выходе из блока;
- при `return`;
- при `break`/`continue` (сейчас — все активные defer-инструкции).

Порядок — LIFO.

## 13. Память

Сейчас:

- скалярные значения и массивы — обычные C-переменные;
- `string` хранит указатель и длину; конкатенация выделяет память;
- `Vector` выделяет память через `realloc`;
- `arena name` создаёт `AktArena` и освобождает её при выходе из блока;
- `defer` гарантирует очистку;
- `shared T` — heap-ячейка с atomic refcount; освобождается через
  `defer akt_shared_unref`;
- `weak T` — невладеющая ссылка на такую ячейку; `w.upgrade()`
  возвращает `Option<T>`: `Some` со снимком значения, если ячейка
  жива, и `None`, если сильных ссылок не осталось;
- `manual` — обычное владение; очистка через `defer`;
- все аллокации рантайма отслеживаются и освобождаются в
  `akt_runtime_shutdown()` при завершении `main`.

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

Компилятор отклоняет возврат ссылки на локальную переменную или её
поле: `return ^local` и `return ^local.field` — ошибка.

## 14. Ошибки: Option и Result

```akt
fn find(int id) Option<string> {
    if (id == 1) { return Some("one") }
    return None
}

fn divide(int a, int b) Result<int, string> {
    if (b == 0) { return Err("division by zero") }
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
    case Some(name):
        println(name)
    case None:
        println("not found")
}
```

`??` даёт значение по умолчанию для `Option`:

```akt
string name = find(2) ?? "nobody"
```

`?` пробрасывает `Err`/`None` из функции с совместимым возвращаемым
типом. `Result<void, E>` поддерживает `Ok(())`.

## 15. `async` / `await`

```akt
async fn fetch(int id) int {
    await sleep_ms(1)
    return id * 10
}

fn main() int {
    int a = await fetch(1)
    println(f"a = {a}")
    return 0
}
```

`async fn` компилируется в worker + `AktTask`, который выполняется в
отдельном потоке; `await` ждёт задачу и забирает результат.

```akt
auto all = await async.all([fetch(1), fetch(2), fetch(3)])
int first = await async.select([fetch(1), fetch(2)])
```

`async.timeout` пока не реализован.

## 16. Замыкания

Замыкания поддерживаются в `Vector.filter`, `Vector.map` и
`Vector.reduce` и захватывают локальные переменные лексически:

```akt
int limit = 3
auto big = nums.filter(|x| x > limit)
auto squares = nums.map(|x| x * x)
int total = nums.reduce(0, |acc, x| acc + x)
```

Сейчас замыкание — inline-выражение внутри HOF, а не отдельное значение
функционального типа. Передача замыканий в `thread.spawn` и хранение в
переменных — следующий этап.

## 17. C-интероп

Базовый C-интероп работает:

```akt
cinclude <stdio.h>
cinclude <math.h>

cimport "libs/sokol/sokol_app.h"

fn main() int {
    c.printf("hello from C\n")
    f64 s = c.sin(3.14159 / 2.0)
    println(f"sin = {s}")
    return 0
}
```

- `cimport "header.h"` импортирует заголовок и автоматически
  компилирует соседний `header.c` — удобно для sokol/miniaudio и
  подобных библиотек «заголовок + исходник».

- `cinclude <...>` добавляет `#include` в сгенерированный C.
- `clink "name"` добавляет `-lname` при линковке.
- `cfn` объявляет внешнюю C-функцию (можно с `...`), вызывается по
  имени или через `c.`.
- `cstruct`/`cunion` задают C-совместимый layout.
- `c.func(args)` вызывает C-функцию `func`.
- Строковые аргументы автоматически передаются как `.data`
  (`const char*`).
- `c.to_string(ptr)` / `c.cstring(ptr)` превращают `const char*` в
  `string`.
- `cinclude <header>` сам импортирует типы, функции, enum и `#define`
  из заголовка — ручной bindgen не нужен.
- Сгенерированный C и кэш заголовков лежат в `.kache/`.
- Для ручных `cstruct`, объявленных и в заголовке, печатается
  `_Static_assert` на размер.

Ограничения: импортёр не разбирает битфилды и сложные declarator'ы;
такие места объявляйте вручную через `cfn`/`cstruct`.

## 18. Ограничения текущей версии

- Замыкания работают в HOF и `thread.spawn`, но не как произвольные
  значения первого класса.
- Нет дженериков, кроме `Vector<T>`.
- `shared`/`weak` без полного анализа времени жизни (есть `upgrade`
  и запрет `return ^local`).
- Нет настоящего async-executor: задачи выполняются в потоках.
- Нет автоподключения C-библиотек через провайдеры.

Эти ограничения отражены в [`ROADMAP.md`](ROADMAP.md).
