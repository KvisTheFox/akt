# akt для новичков

Эта глава — для тех, кто впервые видит akt. Никаких предположений о
знании C, Rust или компиляторов.

## 1. Что такое akt

akt — это язык, в котором вы пишете файлы `.akt`, а `aktc` превращает их
в обычную программу для Windows, Linux или macOS. Под капотом akt
генерирует C-код, но вам не нужно писать на C.

Главные идеи:

- **Нет сборщика мусора.** Программа не делает пауз на GC.
- **Память предсказуема.** Локальные значения живут в регионе функции,
  `defer` гарантирует очистку, `arena` (в разработке) — групповое
  освобождение.
- **C-подобный синтаксис.** Знакомые `if`, `while`, `for`, `{}`, `//`.
- **C-библиотеки рядом.** akt создан, чтобы вызывать существующий C-код.
- **Ошибки — значения.** `Result<T, E>` вместо исключений (в разработке).

## 2. Установка из исходников

Нужны:

- Windows: LLVM/Clang и Visual Studio Build Tools, либо MinGW.
- Linux/macOS: `clang` или `gcc`, `make`.

Сборка через XMake:

```bash
git clone <repo> akt
cd akt
xmake f -c --toolchain=clang-cl -m debug   # Windows
xmake build
```

На Linux/macOS:

```bash
xmake f -c -m release
xmake build
```

Или CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

После сборки в `bin/` будут `aktc` и `katek`.

## 3. Первая программа

Создайте `hello.akt`:

```akt
fn main() int {
    println("Hello, World!")
    return 0
}
```

Запустите:

```bash
bin/aktc run hello.akt
```

Вы увидите:

```text
Hello, World!
```

`fn main() int` — точка входа. `return 0` означает успешное завершение.

## 4. Переменные

```akt
fn main() int {
    int age = 42
    f64 price = 19.99
    string name = "akt"
    bool ok = true

    println(f"name={name} age={age} price={price} ok={ok}")
    return 0
}
```

`f"..."` — f-строка. Внутри `{}` можно писать выражения:

```akt
println(f"2 + 2 = {2 + 2}")
```

Тип можно не писать, если он очевиден:

```akt
auto x = 10
auto pi = 3.14
auto message = "hello"
```

## 5. Условия и циклы

```akt
fn main() int {
    int n = 7
    if (n % 2 == 0) {
        println("even")
    } else {
        println("odd")
    }

    for (i in 0..5) {
        print(f"{i} ")
    }
    println("")

    int i = 0
    while (i < 3) {
        println(f"i={i}")
        i += 1
    }
    return 0
}
```

`0..5` — полуинтервал `[0, 5)`. Для включительного диапазона:
`0..=5`.

## 6. Функции

```akt
fn add(int a, int b) int {
    return a + b
}

fn main() int {
    println(f"sum = {add(2, 3)}")
    return 0
}
```

Рекурсия работает:

```akt
fn fib(int n) int {
    if (n < 2) { return n }
    return fib(n - 1) + fib(n - 2)
}
```

## 7. Строки

```akt
fn main() int {
    string s = "  Hello, akt!  "
    println(s.trim())
    println(s.to_upper())
    println(f"len = {s.len()}")
    println(f"contains = {s.contains("akt")}")
    return 0
}
```

Строки индексируются по байтам, но `akt_str_char_at` умеет читать
UTF-8 code point. Для Unicode-итерации используйте `for (ch in s)`.

## 8. Массивы и Vector

Фиксированный массив:

```akt
[i32; 5] nums = [10, 20, 30, 40, 50]
println(f"{nums[0]} {nums[4]} len={len(nums)}")
```

Динамический `Vector<T>`:

```akt
Vector<i32> v = [1, 2, 3]
v.push(4)
v.push(5)
println(f"len={v.len()} third={v[2]}")
for (x in v) {
    print(f"{x} ")
}
```

`Vector` хранит элементы в куче и растёт по мере необходимости.

## 9. Структуры

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

fn main() int {
    Point p = Point { x: 3.0, y: 4.0 }
    println(f"len = {p.length()}")
    Point o = Point::origin()
    println(f"o = ({o.x}, {o.y})")
    return 0
}
```

`self` — это ссылка на текущий объект. `static fn` не имеет `self` и
вызывается через `Type::name()`.

## 10. `defer`

`defer` выполняет инструкцию при выходе из области, даже через `return`:

```akt
fn main() int {
    defer println("cleanup 1")
    defer println("cleanup 2")
    println("body")
    return 0
}
```

Вывод:

```text
body
cleanup 2
cleanup 1
```

Порядок — LIFO, как у стека.

## 11. Проект и `katek`

```bash
katek new hello
cd hello
katek run
```

Структура:

```text
hello/
├── katek.toml
├── src/
│   └── main.akt
├── tests/
└── target/
```

`katek test` ищет `tests/*.akt`, собирает и запускает каждый файл.

## 12. Что дальше

- [Язык: полный обзор](LANGUAGE.md)
- [Стандартная библиотека](STDLIB.md)
- [CLI](CLI.md)
- [Архитектура](ARCHITECTURE.md)
- [План до 1.0](ROADMAP.md)

## 13. Если что-то не работает

1. Убедитесь, что `clang` доступен: `clang --version`.
2. Запустите `aktc check file.akt` — он покажет ошибку без сборки C.
3. Посмотрите сгенерированный C: `aktc build file.akt --keep-c`.
4. Если путь содержит кириллицу, используйте ASCII-путь или задайте
   `AKT_ROOT` на ASCII-путь.
