# akt — промпты для ИИ-агента

Этот файл — набор заданий для агента, который реализует `aktc` и
`katek`. Каждый промпт самодостаточен, но предполагает, что агент
прочитал нормативные документы:

- [`00-philosophy.md`](00-philosophy.md)
- [`01-syntax.md`](01-syntax.md)
- [`02-memory.md`](02-memory.md)
- [`03-compiler.md`](03-compiler.md)
- [`04-katek.md`](04-katek.md)
- [`05-stdlib.md`](05-stdlib.md)
- [`09-negative.md`](09-negative.md)

---

## 0. Общие правила для всех промптов

Вставьте этот блок в начало любого задания:

```text
Ты реализуешь язык akt по спецификации в каталоге README/.

Жёсткие ограничения:
- Исходное расширение — только .akt. Никаких .cdr.
- Нет сборщика мусора, исключений, макросов, наследования.
- Нет let, mut, try, is, unsafe, macro, template.
- :: только для вариантов enum и ассоциированных функций.
- Модули — через use a.b; use std::io запрещён.
- Массивы — [T; N]; явные generics — f[T](args).
- Объявления struct/enum/fn/test — только на уровне модуля.
- Ошибки — Result<T, E>; ожидаемые ошибки не паникуют.
- shared делает атомарным только refcount; гонки данных не
  проверяются.
- C-граница: string → cstring только через .as_c_str().

Не придумывай API, которых нет в 05-stdlib.md.
Не меняй спецификацию, чтобы «подогнать» реализацию.
Если требование противоречит спецификации — остановись и объясни.

Definition of done:
1. Код собирается командой из задания.
2. Добавлены тесты на новое поведение.
3. Нет новых нарушений из 09-negative.md.
4. Публичные функции задокументированы.
```

---

## 1. Лексер

**Цель:** реализовать лексер `aktc` на C11.

**Вход:** `aktc/src/lexer.c`, `aktc/include/lexer.h`,
`README/01-syntax.md`.

**Задачи:**

1. Токены: идентификаторы, целые/вещественные литералы с
   суффиксами, строки (`"..."`, `f"..."`, `r"..."`, `"""..."""`),
   символы, комментарии (`//`, `/* */`, `///`, `//!`).
2. Ключевые слова — ровно 40 из §1.1. `and/or/not` — операторы,
   не ключевые слова.
3. Позиции: файл, строка, столбец, байтовое смещение.
4. Диагностика: незакрытая строка/комментарий, невалидный UTF-8,
   недопустимый escape.
5. Потокобезопасность не требуется: один лексер на файл.

**Acceptance criteria:**

```bash
ctest --test-dir build -R lexer
# все тесты проходят
grep -R "\"let\"\|\"try\"\|\"is\"" aktc/src/lexer.c
# пусто: этих ключевых слов нет
```

Добавьте тесты на:

- `f"x = {user["name"]}"` — вложенные кавычки;
- `r"C:\path"` — raw string;
- `'\\u{1F600}'` — code point;
- `1_000_000`, `0xFF`, `3.14f`, `42u128`;
- `//!` и `///`.

---

## 2. Парсер и AST

**Цель:** построить AST для всей грамматики ядра.

**Задачи:**

1. Объявления: `fn`, `struct`, `enum`, `impl`, `type`, `const`,
   `static`, `use`, `test`, `cinclude`, `clink`, `cfn`,
   `cstruct`, `cunion`.
2. Инструкции: `if`, `while`, `for`, `for-in`, `loop`, `switch`,
   `return`, `break`, `continue`, `defer`, `arena`, `shared`,
   `weak`, `manual`, блоки.
3. Выражения: вызовы, индексация, поля, методы, `as`, `??`,
   `?:`, диапазоны, замыкания, `await`, `?`.
4. Типы: `[T; N]`, `[]T`, `*T`, `^T`, `Option<T>`, `Result<T,E>`,
   кортежи, `fn(...) T`.
5. Восстановление после ошибок: минимум одна диагностика на
   инструкцию, без каскада.

**Acceptance criteria:**

```bash
ctest --test-dir build -R parser
aktc --dump-ast tests/parser/full_core.akt > /dev/null
```

- AST для `01-syntax.md` §15 строится без ошибок.
- `struct` внутри `fn` даёт ошибку `E-NESTED-DECL`, а не падение.
- `T[N]` даёт ошибку с подсказкой `[T; N]`.

---

## 3. Семантический анализ и типы

**Цель:** resolver + type checker.

**Задачи:**

1. Разрешение имён модулей (`use a.b`), локальных и модульных
   имён.
2. Проверка типов выражений и инструкций.
3. Вывод типа `auto` и типа возврата.
4. Перегрузка: точное совпадение, fallback, ошибка
   неоднозначности.
5. Generics: мономорфизация, проверка операций при
   инстанцировании.
6. `switch`: полнота, guards, диапазоны, типы ветвей.
7. `Result`/`Option`: `?` только для `Result` в совместимом
   контексте; `??` только для `Option`; `Ok(())` только для
   `Result<void, E>`.
8. Запрет вложенных объявлений.

**Acceptance criteria:**

```bash
ctest --test-dir build -R sema
```

Тесты должны покрывать:

- `f[T](args)` и вывод `f(1, 2)`;
- `describe(true)` → fallback;
- `add(1u8, 2u8)` → ошибка;
- `use std::io` → ошибка;
- `struct` внутри `main` → ошибка;
- `?` в `fn main() int` → ошибка;
- неполный `switch` без `default` → ошибка.

---

## 4. Проверка памяти и регионов

**Цель:** реализовать правила `02-memory.md`.

**Задачи:**

1. Построить дерево регионов для функции, блоков и итераций.
2. Проверить, что `^T` не указывает на дочерний регион после его
   выхода.
3. Запретить возврат `^T` на локальное значение.
4. Запретить передачу `auto`/`arena` в другой поток по ссылке.
5. Проверить циклы сильных `shared`-ссылок.
6. Проверить, что `manual` освобождается (эвристика + warning).
7. `arena` не является значением: запретить присваивание,
   возврат и передачу.

**Acceptance criteria:**

```bash
ctest --test-dir build -R memory
```

Обязательные negative-тесты:

```akt
fn bad() ^i32 {
    i32 x = 5
    return ^x            // E-ESCAPE
}
```

```akt
struct Node {
    shared Node next     // E-SHARED-CYCLE
}
```

```akt
fn bad(arena ar) { }     // E-ARENA-VALUE
```

---

## 5. Кодогенерация C11

**Цель:** `akt` → качественный C11.

**Задачи:**

1. Один `.gen.c` и `.gen.h` на модуль.
2. Имена C-символов: `akt_<module>_<name>`, стабильные и
   детерминированные.
3. Struct/enum/union layout, выравнивание, `cstruct`.
4. `Result`/`Option` — тегированные union.
5. `defer` — LIFO cleanup; fallback на goto для MSVC/C99.
6. Проверки переполнения, деления на ноль, сдвигов.
7. `async`/`await` — state machine или stackful runtime
   (выбрать и задокументировать).
8. Детерминированный порядок объявлений.

**Acceptance criteria:**

```bash
aktc examples/full/inventory.akt -o /tmp/inventory
/tmp/inventory
cc -std=c11 -Wall -Wextra -Werror -c /tmp/inventory.gen.c
```

- Сгенерированный C компилируется без warning'ов.
- Два прогона `aktc` дают побайтово одинаковый `.gen.c`.
- `defer` срабатывает при `return`, `break`, `continue`, `?`.

---

## 6. Bindgen и C-интероп

**Цель:** автоматические привязки к C-заголовкам.

**Задачи:**

1. Разобрать подмножество C11: typedef, struct, union, enum,
   функции, указатели, массивы, `const`, `volatile`, variadic.
2. Сгенерировать `.akti` с `cfn`/`cstruct`/`cunion`.
3. Маппинг типов — таблица §11.4 `01-syntax.md`.
4. `const char*` → `cstring`; `char*` → `*cchar`.
5. Диагностика неподдерживаемых конструкций с указанием файла и
   строки.
6. Кэш привязок по хэшу заголовка и флагов.

**Acceptance criteria:**

```bash
katek bindgen zlib
# создаёт target/bindings/zlib.akti
grep -q "cfn zlibVersion" target/bindings/zlib.akti
```

- `cinclude <zlib.h>` + `c.zlibVersion()` компилируется.
- `cinclude <stdio.h>` даёт `c.printf`.
- Неподдерживаемый макрос не ломает весь заголовок: warning +
  пропуск.

---

## 7. Ядро stdlib (Tier 0)

**Цель:** реализовать Tier 0 из `05-stdlib.md`.

**Задачи:**

1. `std.error`, `std.mem`, `std.ptr`, `std.alloc`.
2. `std.io`, `std.fs`, `std.path`, `std.env`, `std.os`,
   `std.process`.
3. `std.string`, `std.fmt`, `std.collections`.
4. `std.math`, `std.time`.
5. `std.thread`, `std.sync`, `std.atomic`.
6. `std.test`, `std.log`, `std.debug`.
7. `std.c` — C-строки и FFI-хелперы.

**Правила:**

- Никакого GC.
- `Result` для fallible-функций.
- `Ok(())` для void.
- `string` не передаётся как `cstring` неявно.
- Каждый модуль не тянет лишние зависимости.

**Acceptance criteria:**

```bash
ctest --test-dir build -R stdlib
katek test examples/stdlib
```

- Нет `malloc` без `free` в тестах (проверка ASan/Valgrind).
- `std.collections` проходит property-тесты на `Vector`/`Map`.
- `std.thread` тесты проходят под TSan.

---

## 8. `katek`: манифест и граф сборки

**Цель:** реализовать декларативную сборку.

**Задачи:**

1. Парсинг `katek.toml` (пакет, workspace, зависимости, `[std]`,
   `[native]`, `[build]`).
2. Построение графа пакетов и модулей.
3. `katek build/run/test/fmt/doc/clean`.
4. `katek.lock`: версии, провайдеры, флаги, контрольные суммы.
5. `--locked` и `--offline`.
6. Кэш артефактов по content hash.

**Acceptance criteria:**

```bash
katek new demo && cd demo && katek run
katek build --locked
katek build --offline   # после первого fetch
```

- Повторная сборка без изменений не вызывает `cc`.
- Изменение одного модуля пересобирает только его и зависимые.
- `katek.lock` детерминирован: одинаковый ввод → одинаковый файл.

---

## 9. `katek`: провайдеры C-библиотек

**Цель:** первоклассная поддержка C-зависимостей.

**Задачи:**

1. Провайдеры: `xrepo`, `vcpkg`, `conan`, `cmake`, `pkg-config`,
   `system`, `path`, `prebuilt`.
2. Единый интерфейс: `include_dirs`, `library_dirs`, `libs`,
   `defines`, `cflags`, `targets`, `version`.
3. Резолвинг: явный provider → первый доступный из списка.
4. Запись результата в `katek.lock` и `.katek/native.json`.
5. Вендоринг: `katek vendor` копирует исходники/артефакты в
   `vendor/`.
6. Диагностика: библиотека не найдена, версия не совпала,
   ABI-конфликт.

**Acceptance criteria:**

```bash
# xrepo
katek c add zlib --provider xrepo --version 1.3.1
katek build

# pkg-config
katek c add openssl --provider pkg-config
katek build

# CMake package
katek c add sqlite3 --provider cmake --package SQLite3
katek build
```

- Для каждой зависимости в `katek.lock` есть provider, version,
  checksum.
- `katek build --locked --offline` не ходит в сеть.

---

## 10. XMake backend

**Цель:** `katek build --backend xmake`.

**Задачи:**

1. Генерировать `xmake.lua` из графа `katek`.
2. Маппинг: target, `add_files`, `add_includedirs`,
   `add_links`, `add_defines`, `add_syslinks`.
3. C-зависимости — через `add_requires`/`add_packages`, если
   провайдер xrepo; иначе — явные include/lib пути.
4. Не вызывать XMake для проектов без C-зависимостей, если
   выбран `internal`.
5. Детерминированный `xmake.lua`.

**Acceptance criteria:**

```bash
katek build --backend xmake
katek build --backend xmake --release
```

- `xmake.lua` не содержит абсолютных путей.
- Сборка воспроизводима: одинаковый `xmake.lua` при повторной
  генерации.
- `xmake` не обязателен для `--backend internal`.

---

## 11. CMake interop

**Цель:** использовать CMake-пакеты и собирать akt-библиотеку для
CMake-проектов.

**Задачи:**

1. `provider = "cmake"`: вызвать `find_package`, получить target,
   извлечь include/lib/defines.
2. Экспорт akt-библиотеки как CMake target:
   `akt_add_library(name ...)`, `akt_add_executable(...)`.
3. Не тащить CMake в базовую сборку: только по запросу.
4. Поддержка `CMAKE_TOOLCHAIN_FILE` и `CMAKE_PREFIX_PATH`.

**Acceptance criteria:**

```bash
katek build --backend cmake
cmake -S . -B build -DAKT_USE_CMAKE=ON
cmake --build build
```

- CMake-проект может слинковать akt-библиотеку.
- `find_package(SQLite3)` находится через provider.
- Без CMake `--backend internal` работает.

---

## 12. CLI `aktc`

**Цель:** удобный и предсказуемый компилятор.

**Задачи:**

1. `aktc build`, `aktc run`, `aktc check`, `aktc fmt`,
   `aktc doc`, `aktc bindgen`.
2. Флаги: `-o`, `-O0..-O3`, `--target`, `--sysroot`, `--emit`,
   `--dump-ast`, `--emit aktir`, `--color`.
3. Коды возврата: 0 — успех, 1 — ошибка компиляции, 2 —
   ошибка использования.
4. Диагностики в формате `file:line:col: error[E-CODE]: message`.
5. `--json` для машинного вывода.

**Acceptance criteria:**

```bash
aktc check examples/full/inventory.akt
aktc build examples/full/inventory.akt -o /tmp/inv
/tmp/inv
aktc --json check bad.akt | jq .
```

- Нет паники на любом файле из `tests/negative/`.
- Все ошибки имеют код и позицию.

---

## 13. Диагностика

**Цель:** ошибки, которые объясняют, что делать.

**Задачи:**

1. Коды ошибок: `E-LEX-*`, `E-PARSE-*`, `E-TYPE-*`,
   `E-MEM-*`, `E-C-*`, `E-BUILD-*`.
2. Подсказки: `help: используйте [T; N] вместо T[N]`.
3. Показ исходной строки и маркера.
4. Без каскада: одна ошибка на инструкцию.
5. Локализация сообщений — английский по умолчанию, русский —
   через `--lang ru`.

**Acceptance criteria:**

```bash
aktc check tests/negative/array_syntax.akt
# error[E-PARSE-ARRAY]: expected ';' in array type
# help: write [T; N], not T[N]
```

- Каждая negative-фикстура даёт ровно одну основную ошибку.
- Нет сообщений вида «unexpected token» без позиции.

---

## 14. Тест-раннер

**Цель:** `katek test` и `aktc test`.

**Задачи:**

1. Находить `tests/**/*.akt` и `*_test.akt`.
2. Запускать `test "name" { }` в изоляции.
3. `assert`, `assert_eq`, `assert_ne`, `assert_throws`.
4. Фильтр `--filter`, список `--list`, формат `--format`.
5. Таймаут на тест; параллельный запуск с `--jobs`.
6. Код возврата 1 при падении.

**Acceptance criteria:**

```bash
katek test
katek test --filter add
katek test --list
```

- Падающий тест печатает файл, строку и diff значений.
- `assert_throws` ловит панику, но не `Result::Err`.

---

## 15. Форматтер

**Цель:** `katek fmt` без споров о стиле.

**Задачи:**

1. 4 пробела, `{` на той же строке, один оператор на строку.
2. Переносы длинных вызовов и списков.
3. Сохранение комментариев и пустых строк (максимум две).
4. `--check` для CI.
5. Идемпотентность: `fmt(fmt(x)) == fmt(x)`.

**Acceptance criteria:**

```bash
katek fmt --check .
katek fmt .
git diff --exit-code
```

- Форматтер не меняет AST.
- `fmt` не трогает строковые литералы и комментарии.

---

## 16. LSP

**Цель:** базовый language server.

**Задачи:**

1. `initialize`, `textDocument/didOpen/didChange`,
   `publishDiagnostics`.
2. Completion для модулей, полей, методов.
3. Go-to-definition.
4. Hover с типом.
5. Форматирование через `katek fmt`.

**Acceptance criteria:**

- VS Code подключается и показывает ошибки.
- Completion не предлагает `let`, `try`, `is`.
- Go-to-definition работает для `use math` → `src/math.akt`.

---

## 17. Кросс-компиляция

**Цель:** сборка под другую цель.

**Задачи:**

1. `--target x86_64-linux-gnu`, `aarch64-linux-gnu`,
   `x86_64-windows-msvc`, `wasm32-wasi`.
2. Выбор `cc`, sysroot, `--sysroot`, `-isystem`.
3. Запрет `-march=native` по умолчанию.
4. Проверка ABI: размеры типов, выравнивание, endianness.
5. Тесты на кросс-сборку в CI.

**Acceptance criteria:**

```bash
aktc build --target aarch64-linux-gnu examples/full/inventory.akt
file /tmp/inventory
# ELF 64-bit LSB, ARM aarch64
```

- `int`/`size`/`isize` соответствуют целевой платформе.
- Нет абсолютных путей в артефактах.

---

## 18. Воспроизводимость и lock

**Цель:** одинаковый ввод → одинаковый бинарник.

**Задачи:**

1. `-ffile-prefix-map`, `-fdebug-prefix-map`.
2. Детерминированный порядок модулей и символов.
3. `SOURCE_DATE_EPOCH`.
4. Хэш всех входов в `katek.lock`.
5. Проверка `katek build --locked --offline`.

**Acceptance criteria:**

```bash
katek build --release
sha256sum target/bin/app > /tmp/h1
katek clean && katek build --release
sha256sum target/bin/app > /tmp/h2
diff /tmp/h1 /tmp/h2
```

- Два чистых билда дают одинаковый хэш.
- `katek.lock` не меняется при `--locked`.

---

## 19. Документация и проверка примеров

**Цель:** документация не врёт.

**Задачи:**

1. `katek doc` собирает doc-комментарии.
2. CI проверяет все `.akt`-блоки в `README/*.md`.
3. Запрещены `.cdr`, `let`, `try`, `is`, `std::`.
4. Ссылки на файлы существуют.
5. Примеры без `fn main` помечены как фрагменты.

**Acceptance criteria:**

```bash
katek doc --check
python tools/check_docs.py README
# скрипт падает, если в блоке ```akt встретились
# .cdr, let, try, is, std::
```

Скрипт `check_docs.py` должен:

- извлекать ```` ```akt ````-блоки;
- компилировать каждый блок с обёрткой, если он помечен как
  программа;
- падать на `.cdr`, `let`, `try`, `is`, `std::` **внутри
  akt-блоков** (упоминания в прозе и в таблице запретов
  допускаются);
- проверять, что ссылки на файлы существуют.

---

## 20. Безопасность C-границы

**Цель:** не обещать больше, чем даёт C.

**Задачи:**

1. Проверять `null` перед разыменованием `*T`, если это
   доказуемо; иначе — warning.
2. `string → cstring` только через `.as_c_str()`.
3. C-callback без замыканий с захватами.
4. Не встраивать `free` для указателей, пришедших из C.
5. Документировать владение в сигнатурах `cfn`.

**Acceptance criteria:**

```bash
aktc check tests/negative/cstring_from_string.akt
# error[E-C-CSTRING]: string is not NUL-terminated
```

- Нет неявных конверсий `string` ↔ `cstring`.
- `cfn` с `*T` не получает автоматический `free`.
- ASan-тесты на C-границе проходят.

---

## 21. Производительность

**Цель:** не хуже C на типичных задачах.

**Задачи:**

1. Бенчмарки: fib, sort, hash map, JSON parse, HTTP request.
2. Сравнение с C-эталоном; допустимое замедление — ≤ 10%.
3. Профиль: время компиляции, размер бинарника, RSS.
4. Оптимизации только после профиля.

**Acceptance criteria:**

```bash
katek bench --baseline c
```

- `fib(35)` ≤ 1.1× C `-O2`.
- Размер бинарника ≤ 1.2× C-эталона.
- Время компиляции проекта ≤ 2× `cc` для эквивалентного C.

---

## 22. Bootstrap и релиз

**Цель:** собрать `aktc` самим собой и выпустить релиз.

**Задачи:**

1. Stage 0: `aktc` на C.
2. Stage 1: `aktc`, написанный на akt, компилируется Stage 0.
3. Stage 2: Stage 1 компилирует сам себя; сравнить поведение.
4. Релизные артефакты: Linux x86_64/aarch64, macOS, Windows.
5. Подписи и контрольные суммы.
6. `katek self-update` проверяет подпись.

**Acceptance criteria:**

```bash
make bootstrap
# stage0 -> stage1 -> stage2
cmp build/stage1/aktc build/stage2/aktc
```

- Stage 1 и Stage 2 проходят один и тот же тест-набор.
- Релиз содержит `SHA256SUMS` и подпись.

---

## 23. Финальная приёмка

**Цель:** проверить, что реализация соответствует спецификации.

**Команды:**

```bash
katek new hello && cd hello && katek run

katek build --locked
katek build --offline
katek test
katek fmt --check .

aktc check ../README/01-syntax.md   # через check_docs.py
```

**Чек-лист:**

- [ ] Hello World работает.
- [ ] `01-syntax.md` §15 компилируется.
- [ ] `examples/full/inventory.akt` компилируется и запускается.
- [ ] `katek c add zlib --provider xrepo` + `c.zlibVersion()`
      работает.
- [ ] `katek build --backend xmake` работает.
- [ ] `katek build --backend cmake` работает.
- [ ] `katek test` проходит.
- [ ] Два чистых release-билда дают одинаковый хэш.
- [ ] Нет `.cdr`, `let`, `try`, `is`, `std::` в README.
- [ ] Все ссылки на файлы существуют.

---

## 24. Шаблон для нового промпта

```text
Контекст:
- Репозиторий: <path>
- Документы: README/00..12
- Текущая стадия: <lexer|parser|sema|codegen|katek|stdlib>

Задача:
<одно конкретное изменение>

Ограничения:
- Не менять публичный синтаксис без обновления 01-syntax.md.
- Не добавлять запрещённое из 09-negative.md.
- Не придумывать API вне 05-stdlib.md.

План:
1. <шаг>
2. <шаг>
3. <шаг>

Тесты:
- <unit>
- <negative>
- <integration>

Definition of done:
- <команда> завершается с кодом 0.
- <grep> не находит запрещённых конструкций.
- Diff содержит только относящиеся к задаче файлы.
```
