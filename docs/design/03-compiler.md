# akt — устройство компилятора `aktc`

## 1. Общие принципы

**`aktc` — компилятор akt, написанный на C11.**

- Вход: `.akt` + `.akti` + манифест/флаги.
- Выход: `.gen.c` → `.o` → бинарник (через системный C-компилятор).
- Зависимости: libc и системный C-компилятор. Больше ничего.
- Целевой размер: < 3 МБ (release, stripped).
- Целевая скорость: > 50 000 строк/сек на одном ядре для фронтенда
  (без вызова `cc`).
- Кодогенерация: C11, без LLVM/MLIR/Cranelift и без собственного
  машинного backend.
- Оптимизации уровня C-кода — на `aktc`; тяжёлые оптимизации — на
  GCC/Clang/MSVC.

`aktc` не обязан быть «самым быстрым компилятором в мире». Он
обязан быть достаточно быстрым, чтобы `katek build` в
инкрементальном режиме ощущался мгновенным.

---

## 2. Пайплайн

```text
.akt + .akti + флаги
        │
        ▼
[1] Reader        чтение файлов, UTF-8, BOM, карта строк
        │
        ▼
[2] Lexer         токены, без regex, один проход
        │
        ▼
[3] Parser        AST в арене, Pratt для выражений
        │
        ▼
[4] Resolver      модули, use, pub, имена, типы-заголовки
        │
        ▼
[5] Bindgen       cinclude → привязки C (кэш .akti)
        │
        ▼
[6] Checker       типы, generics, перегрузки, exhaustiveness
        │
        ▼
[7] Memory check  регионы, ^T, shared-циклы, manual
        │
        ▼
[8] Lowering      сахар → простые узлы; monomorphization
        │
        ▼
[9] CodeGen       потоковая запись C11
        │
        ▼
[10] cc           gcc/clang/msvc/tcc → .o
        │
        ▼
[11] Linker       cc/ld/lld → бинарник
```

Важно:

- **Постоянного IR нет.** AST хранится в arena-аллокаторе и живёт
  до конца компиляции модуля. `--emit aktir` печатает линейный дамп
  AST/IR-подобного представления только для отладки; это не вход
  для сборки.
- **Стриминг есть.** CodeGen пишет C по мере обхода, не держит
  второй AST.
- **Один проход на фазу**, а не «всё за один проход». Resolver и
  Checker требуют таблиц символов и типов; это нормально.
- **Модули компилируются параллельно**, но порядок вывода
  детерминирован.

---

## 3. Reader

- Читает файл в память (`mmap` на Unix, `ReadFile` на Windows).
- Проверяет UTF-8; BOM удаляется.
- Строит таблицу `line_start[]` для `line:column`.
- Нормализует переводы строк: CRLF → LF.
- Ограничение размера файла настраивается (по умолчанию 64 МБ).

Ошибки чтения — диагностика `E1000`, без паники.

---

## 4. Lexer

- Один проход по байтам.
- Токены: `KW_*`, `IDENT`, `LIT_INT`, `LIT_FLOAT`, `LIT_STRING`,
  `LIT_CHAR`, `OP_*`, `EOF`.
- Пробелы и комментарии пропускаются; doc-комментарии сохраняются
  отдельным списком.
- Нет regex, нет backtrack, нет рекурсии.
- Таблица переходов для ASCII; UTF-8-идентификаторы — отдельная
  ветка.
- Для интерполяции `f"..."` лексер возвращает последовательность
  `LIT_STRING_START`, `EXPR_START`, токены выражения,
  `EXPR_END`, `LIT_STRING_END`.
- `r"..."` и `"""..."""` — отдельные режимы.

Требования:

- Скорость: > 10 МБ/сек на одном ядре.
- Размер: < 1200 строк C.
- Fuzz: не падает и не зависает на любых байтах.

---

## 5. Parser

### 5.1. Архитектура

- Recursive descent для объявлений.
- Pratt parser для выражений.
- AST-узлы выделяются из arena; `free` для узлов не вызывается.
- Ошибки восстанавливаются на границах инструкций/объявлений,
  чтобы показать несколько ошибок за прогон.
- Парсер не делает семантических проверок.

### 5.2. Точка с запятой

Правило из [`01-syntax.md` §1.4](01-syntax.md). Парсер реализует
его явно:

- хранит `prev_token` и `next_token`;
- проверяет токены-продолжения в конце/начале строки;
- `(` и `[` в начале новой строки не считаются продолжением;
- `;` всегда завершает инструкцию.

### 5.3. Ограничения

- Глубина вложенности выражений ограничена (по умолчанию 512) —
  защита от stack overflow.
- Размер AST на модуль ограничен (по умолчанию 1 млн узлов).
- При превышении — диагностика, а не падение.

### 5.4. Стоимость

Цель: ~20% времени фронтенда.

---

## 6. Resolver

- Строит граф модулей по `use`.
- Проверяет циклы `use` (ошибка `E0201`).
- Разрешает `pub`/приватность.
- Строит таблицы символов: типы, функции, константы, модули.
- Разрешает `use a.b as c`.
- Имена C-привязок (`c.*`) добавляются после bindgen.
- Не разрешает перегрузки — это делает Checker.

Ошибки:

- `E0200` — модуль не найден.
- `E0201` — цикл `use`.
- `E0202` — приватный символ.
- `E0203` — неоднозначное имя.

---

## 7. Bindgen (`cinclude`)

### 7.1. Задача

Превратить C-заголовки в `.akti`-привязки:

```akt
cinclude <zlib.h>
```

→

```akt
// .katek/cache/bindings/zlib-<hash>.akti
cstruct z_stream {
    *const u8 next_in
    u32 avail_in
    // ...
}
cfn deflateInit2_(*z_stream strm, i32 level, i32 method,
                  i32 windowBits, i32 memLevel, i32 strategy,
                  cstring version, i32 stream_size) i32
```

### 7.2. Реализация

- Встроенный парсер C-объявлений (C11 + распространённые
  расширения), без clang как обязательной зависимости.
- Опционально: `clang -Xclang -ast-dump=json` для сложных
  заголовков, если clang найден и включён флагом
  `--bindgen=clang`.
- Поддержка: typedef, struct/union/enum, функции, указатели на
  функции, массивы, `const`, `volatile`, `restrict`, `_Bool`,
  `stdint.h`, `stddef.h`, `stdbool.h`.
- Не поддержано (и это явно диагностируется): макросы с
  вычисляемыми значениями, `_Generic`, `__attribute__`-магия,
  вложенные анонимные структуры без имени.
- Для неподдержанного — ручной `.akti` или `cfn`/`cstruct`.

### 7.3. Кэш

Ключ кэша:

```text
sha256(
  содержимое заголовка +
  все include-заголовки (рекурсивно) +
  include-пути +
  defines +
  версия aktc +
  режим bindgen
)
```

Кэш: `.katek/cache/bindings/<hash>.akti` и глобальный
`~/.cache/katek/bindings/`.

### 7.4. Конфликты

- C-имена доступны как `c.name`.
- Если `name` не конфликтует с akt-именем — доступно и без `c.`.
- Конфликт → требуется `c.`.
- Два `cinclude` с одинаковым именем и разными сигнатурами —
  ошибка `E0400`.

---

## 8. Checker

### 8.1. Типы

- Каждое выражение имеет тип.
- Вывод `auto` и `auto`-возврата.
- Проверка аргументов и возврата.
- Generics: проверка на этапе инстанцирования, мономорфизация — в
  Lowering.
- Перегрузки: точное совпадение; fallback; ошибка
  неоднозначности.
- Приведения только через `as`.
- `switch` по enum/`Option`/`Result` — exhaustiveness.
- `?` — только в подходящем контексте.
- `??` — только для `Option<T>`.

### 8.2. Структуры

- Все поля инициализированы или имеют значения по умолчанию.
- Нет дублирующихся полей.
- `cstruct`/`cunion` — только C-совместимые поля.
- Метод не может конфликтовать с полем.
- `static fn` не имеет `self`.

### 8.3. Ошибки

| Код | Значение |
|---|---|
| `E0100` | Несовпадение типов |
| `E0101` | Неверное число аргументов |
| `E0102` | Неизвестный тип |
| `E0103` | Неоднозначная перегрузка |
| `E0104` | Нет подходящей перегрузки |
| `E0105` | Неполная инициализация структуры |
| `E0106` | Неисчерпывающий `switch` |
| `E0107` | Недопустимое приведение |
| `E0108` | `?` вне функции с совместимым `Result` |
| `E0110` | `i128`/`u128` не поддержан target |

---

## 9. Memory checker

Проверяет [`02-memory.md`](02-memory.md):

- `auto`/`arena` не переживают регион.
- `^T` не переживает владельца.
- Циклы через `shared` запрещены.
- `manual` без освобождения — warning (в `--strict` — error).
- Использование до инициализации.
- `weak` только для `shared`-типов.
- Замыкания для `thread.spawn` не захватывают `auto`/`arena`.
- `arena` не возвращается и не присваивается.

| Код | Значение |
|---|---|
| `E0001` | Значение переживает регион |
| `E0002` | Использование до инициализации |
| `E0003` | Цикл через `shared` |
| `E0004` | `manual` без освобождения |
| `E0005` | `arena` escaped |
| `E0006` | `weak` без `shared` |
| `E0007` | Захват `auto`/`arena` в поток |

Memory checker **не** доказывает отсутствие гонок данных и **не**
проверяет C-границу. Это указано в [`09-negative.md`](09-negative.md).

---

## 10. Lowering

Дешугаринг перед кодогенерацией:

| Из | В |
|---|---|
| `auto x = e` | `T x = e` (тип выведен) |
| `decl defer stmt` | `decl; defer stmt` |
| `arena ar` | `AktArena ar = akt_arena_create(...)` |
| `Type ar.x = e` | `Type* x = akt_arena_alloc(&ar, ...)` |
| `shared T` | `AktShared(T)` + `akt_shared_new/ref/unref` |
| `weak T` | `AktWeak(T)` |
| `^T` | `T*` + проверенное время жизни |
| `switch`-выражение | блок + временная переменная + `switch` |
| `??` | `if (x.is_some) x.value else y` |
| `for (i in a..b)` | `for (T i = a; i < b; i++)` |
| `for (i in a..b step s)` | `for (T i = a; i < b; i += s)` |
| `for (x in xs)` | вызов `xs.iter()` + цикл |
| Generics | мономорфизация |
| Методы | свободные функции с `self` первым аргументом |
| `static fn` | свободная функция без `self` |
| `Option<T>` | tagged union |
| `Result<T,E>` | tagged union |
| `?` | `if (r.is_err) return r.err;` + cleanup |
| `f"..."` | конкатенация/`akt_fmt_*` |
| `switch` по enum | `switch` по tag + проверка варианта |

Lowering обязан быть детерминированным: одинаковый AST → одинаковый
простой AST.

---

## 11. CodeGen

### 11.1. Архитектура

- Пишет C11 потоково.
- Один `.gen.c` на модуль.
- Имена детерминированно манглируются:
  `akt_<module>_<name>_<stable_hash>`.
- `stable_hash` — FNV-1a от сигнатуры, не от адресов и не от
  времени.
- Заголовочные объявления модулей — в `.gen.h`, чтобы C-компилятор
  мог проверять типы между модулями.
- Все `#include` — только те, что нужны модулю.

### 11.2. Представление типов в C

| akt | C |
|---|---|
| `i8..i128` | `int8_t..int128_t` (или `__int128` с fallback) |
| `u8..u128` | `uint8_t..uint128_t` |
| `int` | `int` |
| `size` | `size_t` |
| `isize` | `ptrdiff_t` |
| `bool` | `bool` (`stdbool.h`) |
| `char` | `uint32_t` + UTF-8-хелперы |
| `cchar` | `char` |
| `f32`/`f64` | `float`/`double` |
| `string` | `struct { const uint8_t* ptr; size_t len; }` |
| `dynstr` | `struct { uint8_t* ptr; size_t len, cap; }` |
| `cstring` | `const char*` |
| `[T; N]` | `struct { T data[N]; }` (или массив в локальной) |
| `[]T` | `struct { T* ptr; size_t len; }` |
| `*T` | `T*` |
| `^T` | `T*` + проверка в aktc |
| `Option<T>` | `struct { bool is_some; T value; }` |
| `Result<T,E>` | `struct { bool is_ok; union { T ok; E err; } }` |
| `shared T` | `AktShared(T)*` |
| `weak T` | `AktWeak(T)` |

`int128` на MSVC отсутствует; для `i128/u128` на MSVC —
диагностика `E0110` или программная эмуляция (медленнее). Это
указано в профиле target.

### 11.3. Память в C

| akt | C-реализация |
|---|---|
| Регион функции | `AktArena` на стеке; `alloca` только для известных мелких размеров вне циклов |
| `arena` | `AktArena { uint8_t* buf; size_t pos, cap; }` |
| `shared` | `AktSharedHeader { _Atomic uint32_t refcount; void (*destroy)(void*); }` |
| `weak` | `AktWeak { AktSharedHeader* hdr; }` |
| `manual` | `malloc`/`free` или ресурсный тип |
| `defer` | `__attribute__((cleanup))` (GCC/Clang) или явный goto-cleanup |
| `^T` | `T*` |
| `*T` | `T*` |

Индексация `a[i]` для массивов, срезов и `Vector` проверяется по
границам; проверка не устраняется оптимизатором. Непроверяемый
доступ — только через `std.ptr.read`/`std.ptr.write` и явный
указатель.

`defer` через `atexit` **не реализуется**: `atexit` не привязан к
scope и ломает семантику. Для MSVC и C99 используется
goto-cleanup:

```c
int akt_fn(void) {
    int _ret = 0;
    FILE* f = fopen("x", "r");
    if (!f) { _ret = 1; goto _cleanup_0; }
    // ...
_cleanup_0:
    if (f) fclose(f);
    return _ret;
}
```

### 11.4. Пример

```akt
fn add(int a, int b) int {
    return a + b
}

fn main() int {
    auto x = add(3, 4)
    println(f"x = {x}")
    return 0
}
```

```c
/* akt_hello.gen.c */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef int32_t akt_i32;
typedef bool akt_bool;

typedef struct {
    const uint8_t* ptr;
    size_t len;
} akt_string;

static inline akt_i32 akt_hello_add(akt_i32 a, akt_i32 b) {
    return a + b;
}

int main(void) {
    akt_i32 x = akt_hello_add(3, 4);
    printf("x = %d\n", x);
    return 0;
}
```

### 11.5. Оптимизации на уровне C

| Оптимизация | Что делает |
|---|---|
| `static inline` | Для мелких функций |
| `const` | Для неизменяемых значений |
| `restrict` | Для указателей без алиасинга (консервативно) |
| Constant folding | `2 + 3` → `5` |
| DCE | Удаление недостижимого кода |
| SROA | Мелкие структуры → локальные переменные (ограниченно) |
| `-O2` | Передаётся в `cc` по профилю |

`aktc` **не делает** SSA, register allocation, loop unrolling,
auto-vectorization. Это работа C-компилятора.

---

## 12. Вызов C-компилятора и линкера

```bash
aktc src/main.akt -o app
```

Внутри:

1. Парсинг и проверка всех модулей.
2. Генерация `.gen.c`/`.gen.h`.
3. `cc -std=c11 -O2 -c ...`.
4. `cc ... -o app`.

Флаги по умолчанию:

```text
-std=c11
-O2
-fno-strict-aliasing        # консервативно для C-границы
-fno-delete-null-pointer-checks
# -fwrapv не используется: akt проверяет переполнение явно
-ffile-prefix-map=<abs>=.
-fdebug-prefix-map=<abs>=.
```

`-march=native` **не** включается по умолчанию: он ломает
переносимость и reproducible builds. Включается явно
`--march=native`.

### 12.1. Кэш объектных файлов

Ключ:

```text
sha256(
  C-код модуля +
  флаги cc +
  версия cc +
  target +
  sysroot +
  defines
)
```

Кэш: `~/.cache/akt/obj/<hash>.o`.

### 12.2. Параллелизм

- Модули компилируются параллельно (пул потоков).
- Порядок линковки — детерминированная сортировка.
- `--jobs N`; по умолчанию — число ядер.

---

## 13. Reproducible builds

`aktc` и `katek` гарантируют:

- нет `__DATE__`/`__TIME__` в генерируемом C;
- нет абсолютных путей в debug-info (`-ffile-prefix-map`);
- нет случайных имён;
- нет зависимости от порядка хэш-таблиц;
- сортировка модулей и флагов;
- `SOURCE_DATE_EPOCH` учитывается, если задан;
- C-компилятор вызывается с фиксированным набором флагов;
- link order детерминирован.

Проверка:

```bash
katek build --release --reproducible
```

---

## 14. Диагностика

Формат:

```text
error[E0001]: auto variable escapes its region
  --> src/main.akt:12:5
   |
12 |     return ^x
   |            ^^ x объявлен в fn make() на строке 10
   |
   = help: верните значение по значению: `return x`
   = help: или используйте `shared Vec2`
```

Правила:

- Всегда `file:line:column`.
- Всегда код ошибки.
- Всегда `help`, если есть однозначное исправление.
- Без ANSI, если stderr не TTY.
- `--json` для машинной обработки.
- Максимум 100 ошибок за прогон (настраивается).

Коды сгруппированы:

| Диапазон | Тема |
|---|---|
| `E0001–E0099` | Память |
| `E0100–E0199` | Типы |
| `E0200–E0299` | Модули |
| `E0300–E0399` | Синтаксис |
| `E0400–E0499` | C-интероп |
| `E0500–E0599` | Сборка/link |
| `E1000+` | I/O и внутренние ошибки |

---

## 15. CLI `aktc`

```text
aktc [OPTIONS] <files...>

Основное:
  -o <path>              выходной файл
  --emit c               только C
  --emit obj             объектный файл
  --emit asm             ассемблер (через cc -S)
  --emit aktir           дамп IR в `.aktir` (отладка)

Проверки:
  --strict               все проверки (по умолчанию)
  --lenient              только синтаксис и явные ошибки
  --warn-all             все предупреждения
  --no-warn              без предупреждений
  --reproducible         детерминированный вывод

Память:
  --memory-report        отчёт об использовании памяти
  --arena-stats          статистика арен

Отладка:
  --dump-ast
  --dump-types
  --dump-regions
  --time
  --json

C-интероп:
  --bindgen=builtin|clang
  --include <dir>
  --define <K=V>
  --link <lib>
  --link-path <dir>

Цель:
  --target <triplet>
  --sysroot <dir>
  --cc <path>
  --ar <path>
  --linker <path>

Оптимизация:
  -O0 / -O1 / -O2 / -O3 / -Os
  --march=native

Стандарт C:
  --std c11|c99
```

`aktc` не управляет зависимостями. Это делает `katek`.

---

## 16. Тестирование компилятора

### 16.1. Unit

- `lexer_test.c` — все токены и escape.
- `parser_test.c` — все конструкции.
- `check_test.c` — типы, перегрузки, generics.
- `mem_test.c` — регионы и `shared`.
- `codegen_test.c` — diff с эталонным C.
- `bindgen_test.c` — C-заголовки.

### 16.2. Integration

```text
tests/integration/<name>/
├── main.akt
├── expected.gen.c
└── expected.out
```

Прогон:

1. `aktc --emit c` → сравнить с `expected.gen.c`.
2. `cc` → запустить → сравнить с `expected.out`.

### 16.3. Fuzz

- libFuzzer на lexer/parser/bindgen.
- Инварианты: не падает, не зависает, не выходит за память,
  не выдаёт UB (ASan/UBSan).

### 16.4. Дифференциальное тестирование

- Один и тот же `.akt` компилируется через `--emit c` и полный
  цикл.
- Поведение и вывод должны совпадать.
- Сравнение с эквивалентным C-кодом для бенчмарков.

---

## 17. Производительность

| Фаза | Цель на 10k строк |
|---|---|
| Reader | < 10 мс |
| Lexer | < 50 мс |
| Parser | < 100 мс |
| Resolver | < 50 мс |
| Checker | < 200 мс |
| Memory | < 100 мс |
| Lowering | < 100 мс |
| CodeGen | < 100 мс |
| **Итого без `cc`** | **< 500 мс** |
| `cc -O2` | +1–3 с (цена C-компилятора) |

Это цели, а не гарантии. Они измеряются на CI и публикуются.

---

## 18. Bootstrap

### 18.1. Основная версия

- `aktc` написан на C11.
- Собирается XMake, Make или CMake.
- Bootstrap: `xmake build aktc` → `aktc`.
- Нет цепочки «нужен старый aktc, чтобы собрать новый».

### 18.2. Self-hosted (опционально)

- `aktc.akt` компилируется текущим `aktc` → `aktc2`.
- `aktc2.akt` компилируется `aktc2` → `aktc2'`.
- Проверка: `aktc2 == aktc2'` побайтово.
- Self-hosted версия **не** является основной и не требуется для
  сборки.

---

## 19. Структура исходников

```text
aktc/
├── src/
│   ├── main.c
│   ├── reader.c/.h
│   ├── lexer.c/.h
│   ├── parser.c/.h
│   ├── ast.c/.h
│   ├── resolve.c/.h
│   ├── bindgen.c/.h
│   ├── check_types.c/.h
│   ├── check_mem.c/.h
│   ├── lower.c/.h
│   ├── codegen.c/.h
│   ├── mangler.c/.h
│   ├── diag.c/.h
│   ├── mempool.c/.h
│   ├── strtab.c/.h
│   ├── hashmap.c/.h
│   └── vec.c/.h
├── include/
│   └── akt_runtime.h
├── tests/
│   ├── unit/
│   └── integration/
└── xmake.lua
```

Целевой объём: ~15–20 тыс. строк C.

---

## 20. Сборка `aktc` через XMake

```lua
-- aktc/xmake.lua
target("aktc")
    set_kind("binary")
    add_files("src/**.c")
    add_includedirs("src", "include")
    add_defines("AKT_VERSION=\"$(version)\"")
    add_cxflags("-Wall", "-Wextra", "-Wpedantic")
    add_syslinks("m")
    set_targetdir("../build/bin")
```

```bash
xmake f -m release
xmake build aktc
xmake install aktc
```

CMake-эквивалент:

```cmake
add_executable(aktc ${AKTC_SOURCES})
target_include_directories(aktc PRIVATE src include)
target_compile_features(aktc PRIVATE c_std_11)
target_compile_options(aktc PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(aktc PRIVATE m)
```

---

## 21. Итог

`aktc`:

- написан на C11;
- не содержит LLVM/MLIR;
- строит AST в арене и стримит C;
- проверяет память на уровне регионов;
- генерирует детерминированный C11;
- вызывает системный `cc`;
- поддерживает C-привязки через встроенный bindgen;
- собирается XMake/Make/CMake;
- остаётся маленьким и понятным.

Главный принцип: **не строить свой backend — использовать C**.
