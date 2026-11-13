# Архитектура akt

## 1. Общая схема

```text
.akt source
   │
   ▼
[lexer]  aktc/src/lexer.c
   │ tokens
   ▼
[parser] aktc/src/parser.c
   │ AST
   ▼
[checker] aktc/src/checker.c
   │ типизированный AST
   ▼
[codegen] aktc/src/codegen.c
   │ .gen.c
   ▼
[C compiler] clang / gcc / cc
   │
   ▼
executable
```

`katek` — оркестратор: находит `src/main.akt`, вызывает `aktc`, запускает
результат, ищет тесты, пишет `katek.toml`.

## 2. Лексер

Файл: `aktc/src/lexer.c`.

- Кодировка UTF-8, BOM пропускается.
- Комментарии `//`, `/* */`.
- Числа: decimal, hex `0x`, binary `0b`, float, `f`-суффикс.
- Строки: обычные, raw `r"..."`, f-строки `f"..."`.
- F-строки сканируются с учётом вложенных `{...}` и строк внутри
  выражений.
- Ключевые слова и операторы — таблица в начале файла.

Токен содержит `kind`, `text`, `ival`, `fval`, `line`, `col`.

## 3. Парсер

Файл: `aktc/src/parser.c`.

- Рекурсивный спуск.
- AST: `Expr` и `Stmt` из `aktc.h`.
- Приоритеты операторов заданы отдельными функциями:
  `parse_or` → `parse_and` → ... → `parse_postfix` → `parse_primary`.
- Постфиксные операции (`(`, `[`, `.`, `?`, `as`) разрешены только на
  той же строке, что и предыдущий токен. Это заменяет обязательные
  точки с запятой.
- F-строка разбирается на текстовые части и подвыражения; подвыражения
  парсятся отдельным лексером/парсером.

### Ограничение

Нет полноценных токенов перевода строки, поэтому перенос выражения
посреди строки может потребовать скобок. Это осознанный компромисс
текущей версии.

## 4. Checker

Файл: `aktc/src/checker.c`.

- Собирает области видимости (`Scope`) с переменными.
- Находит функции, структуры, enum, поля и методы.
- Аннотирует каждое выражение полем `Expr.type`.
- Проверяет:
  - известность имён;
  - количество аргументов;
  - совместимость типов при инициализации/присваивании/return;
  - поля структур;
  - методы и `static fn`;
  - `break`/`continue` только в цикле.
- Встроенные функции (`print`, `println`, `len`, `str`, `int`,
  `float`, `assert`, `panic`, математика) описаны в checker и codegen
  согласованно.

`require_assignable` допускает числовые преобразования между разными
числовыми типами (пока без range-check).

## 5. Codegen

Файл: `aktc/src/codegen.c`.

Генерирует C11:

- `typedef struct` для структур;
- `typedef enum` для enum;
- прототипы функций и методов;
- тела функций;
- `int main(void)`-обёртку, которая вызывает `akt_user_main` и
  `akt_runtime_shutdown`.

Соглашения:

- метод `Point.length` → `Point_length(Point* self, ...)`;
- `static fn Point.origin` → `Point_origin(...)`;
- `self.field` → `self->field`;
- `Vector<T>` → `AktVec`, доступ через `akt_vec_get`;
- `Option<T>` → `AktOption`, `Result<T,E>` → `AktResult`;
- `?` → statement-expression с проверкой `is_ok`/`is_some`;
- `string` → `AktString`;
- `f"..."` → `akt_str_concat` + `akt_str_from_*`;
- `defer` → отложенные инструкции, эмитятся в LIFO на выходе из блока и
  перед `return`;
- `arena name` → `AktArena` + `akt_arena_free` через defer.

## 5.1. Модули

`aktc/src/main.c` рекурсивно загружает `use a.b`, парсит каждый файл и
сливает программы в одну. Сейчас используется глобальное пространство
имён; `module.func()` разрешается в глобальную `func()`. Это первый
рабочий вариант; mangling и видимость — следующий этап.

Для `Vector.push` используется GNU-расширение `__typeof__` (Clang/GCC).
Windows-сборка идёт через Clang, поэтому это допустимо; для MSVC
потребуется отдельная ветка.

## 6. Runtime

Файлы: `runtime/include/akt_runtime.h`, `runtime/src/akt_runtime.c`.

Компоненты:

- `AktString { const char* data; size_t len; }`
- `AktVec { unsigned char* data; size_t len, cap, elem_size; }`
- `AktArena` — блочный arena-аллокатор.
- Печать: `akt_print_*`, `akt_println_*`.
- Строки: concat, slice, eq, cmp, upper/lower/trim/replace/repeat,
  parse_int/parse_float, UTF-8 char_at.
- Пути: join, basename, dirname, extension, absolute.
- Файлы: read/write/exists.
- Время: now_ms, sleep_ms.
- Ошибки: `akt_panic`, `akt_assert_fail`.

Память строк и векторов сейчас освобождается вручную или живёт до
конца процесса. Это временно; арены и владение — в roadmap.

## 7. `katek`

Файл: `katek/src/main.c`.

- `find_project_root` ищет `katek.toml` или `src/main.akt` вверх по
  дереву.
- `build` вызывает `aktc build src/main.akt -o target/app.exe`.
- `run` делает то же и запускает результат.
- `test` обходит `tests/*.akt`, собирает и запускает каждый.
- `new` создаёт проект.
- `c add` дописывает запись в `[native.dependencies]`.

## 8. Сборка

### XMake

`xmake.lua`:

- `akt_runtime` — static library;
- `aktc` — binary;
- `katek` — binary;
- `akt_tests` — C-тесты рантайма.

Windows:

```bash
xmake f -c --toolchain=clang-cl -m debug
xmake build
```

Linux/macOS:

```bash
xmake f -c -m release
xmake build
```

### CMake

`CMakeLists.txt` собирает те же цели. CMake — официальная альтернатива.

## 9. Диагностика

Формат:

```text
file.akt:line:col: error: message
```

Ошибки парсера/чекера завершают процесс с кодом 1. `aktc check` не
вызывает C-компилятор.

## 10. Как добавить возможность

1. Добавить токен (если нужно) в `lexer.c` и `aktc.h`.
2. Добавить AST-узел.
3. Разобрать его в `parser.c`.
4. Проверить типы в `checker.c`.
5. Сгенерировать C в `codegen.c`.
6. Добавить позитивный и негативный тест в `tests/`.
7. Обновить `docs/LANGUAGE.md` и `PLAN.md`.
