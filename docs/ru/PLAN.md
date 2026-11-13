# akt — план разработки

Этот файл — живой план. Он нужен, чтобы работу можно было в любой момент
приостановить и продолжить без потери контекста.

## Видение

akt — компилируемый системный язык с C-подобным синтаксисом, без GC,
с предсказуемой памятью и C11-бэкендом. Инструменты:

- `aktc` — компилятор `.akt` → C11 → системный C-компилятор.
- `katek` — сборка, запуск, тесты, зависимости, C-библиотеки.
- `runtime/` — небольшая C-библиотека поддержки (строки, арены, печать).
- `std/` — модули стандартной библиотеки.
- `examples/`, `tests/` — примеры и регрессионные тесты.
- `docs/` — документация для новичков и для профессионалов.

## Принципы

1. Сначала работает вертикальный срез: `hello.akt` → C → exe.
2. Каждая возможность покрыта тестом.
3. Ошибки компилятора — понятные, с файлом, строкой и колонкой.
4. Память: арены и `defer`; GC нет.
5. Никаких скрытых аллокаций в горячем коде без явного API.
6. XMake — основной способ сборки; CMake — официальная альтернатива.

## Этапы

### M0. Каркас и сборка

- [x] Каталоги `aktc/`, `katek/`, `runtime/`, `std/`, `examples/`, `tests/`, `docs/`.
- [x] `xmake.lua`.
- [x] `CMakeLists.txt`.
- [x] Рантайм: строки, печать, арены.
- [ ] `bin/` и PATH.

### M1. Компилятор: вертикальный срез

- [x] Лексер: идентификаторы, числа, строки, комментарии, операторы.
- [x] Парсер: `fn`, переменные, `if`, `while`, `for`, `return`, вызовы.
- [x] Типы: `int`, `i32`, `i64`, `u32`, `u64`, `f32`, `f64`, `bool`,
      `string`, `char`, `void`.
- [x] Кодогенерация C11.
- [x] `aktc build`, `aktc run`, `aktc check`, `--version`.
- [ ] Диагностика с колонками во всех ошибках.

### M2. Ядро языка

- [x] Арифметика, сравнения, логика, тернарный оператор, `??`.
- [x] `struct`, поля, методы, `static fn`.
- [x] Простые `enum` и `Type::Variant`.
- [x] Массивы `[T; N]` и итерация `for (x in arr)`.
- [x] `Vector<T>` через рантайм.
- [x] `Map<string, T>`.
- [x] `Set<T>`.
- [x] `Deque<T>` (базово).
- [x] `defer` (LIFO, на выходе из блока и `return`).
- [x] `switch`-statement по enum/числам/строкам.
- [x] `Option<T>` / `Result<T,E>`, `Some/None/Ok/Err`, `?`, pattern-switch.
- [x] Модули между файлами: `use a.b`, загрузка `.akt`.
- [x] `async fn` / `await` (thread-backed tasks).
- [x] `async.all/select`.
- [x] `async.timeout` и отложенный запуск (`auto t = f(); await t`).
- [x] `arena` с инициализацией и освобождением через `defer`.
- [x] Payload-enum (`Shape::Circle(f64)`) и pattern-switch.
- [x] `switch`-выражение.
- [x] Все `switch`-ветки по enum проверяются на типы.
- [x] Замыкания в `Vector.filter/map/reduce` (inline, с захватом).
- [x] Замыкания как значения (inline/HOF/thread capture).
- [ ] Дженерики, кроме `Vector<T>`.
- [x] `shared` refcount-ячейки и `weak`-ссылки с `weak.upgrade()`.
- [x] Проверка возврата ссылок на локальные `^T` (включая поля).
- [x] `cinclude`/`clink` + вызовы `c.func` (базовый C-интероп).
- [x] `cfn`, `cstruct`, `cunion` и нативный импорт C-заголовков.
- [x] `cimport` (заголовок + соседний `.c`) и глобалы без `static`.
- [x] xrepo-зависимости и бенчмарки против C/C++/Rust/Zig.
- [x] `type`-алиасы и ABI-проверки (`_Static_assert`).

### M3. `katek`

- [x] `katek new`, `build`, `run`, `check`, `test`, `clean`, `doctor`.
- [x] `katek run file.akt`.
- [x] `katek.toml` (native include/lib/link/defines, system/pkg-config).
- [x] `katek c add` (запись native-зависимости).
- [x] `katek c install` (xrepo, pkg-config, vcpkg, Conan).
- [ ] Полное разрешение CMake.
- [ ] Lock-файл и content-hash.
- [ ] Workspace.

### M4. Стандартная библиотека

- [x] `print`, `println`, `len`, `str`, `int`, `float`.
- [x] `std.string`: базовые функции.
- [x] `std.math`: базовые функции.
- [x] `std.io`: чтение/запись файлов.
- [x] `std.fs`, `std.env`, `std.path`.
- [x] `std.process`, `std.random`, `std.hash`, `std.base64`.
- [x] `std.json` (parse/stringify/index).
- [x] `std.regex` (match/find/replace, подмножество).
- [x] `std.http` (GET/POST через curl).
- [x] `std.db.sqlite` (winsqlite3/libsqlite3).
- [x] `Mutex` и `Atomic<T>`.
- [x] `thread.spawn/join` (функции и `|| { ... }` с захватом shared).
- [x] `std.collections` (Vector/Map/Set/Deque).
- [ ] `std.json`, `std.regex`, `std.http`, `std.sqlite`.
- [ ] `std.thread`, `std.sync`, `std.atomic`.
- [ ] `std.async`.

### M5. Документация

- [x] `docs/GETTING_STARTED.md`.
- [x] `docs/LANGUAGE.md`.
- [x] `docs/ARCHITECTURE.md`.
- [x] `docs/STDLIB.md`.
- [x] `docs/CLI.md`.
- [x] `docs/ROADMAP.md`.
- [x] `docs/CONTRIBUTING.md`.
- [ ] Перевод всей документации на английский.
- [x] `docs/en/` — английские версии ключевых глав.
- [x] `installer/` и `bin/akt-setup.exe`.
- [x] `benchmarks/` (akt/c/c++/rust).
- [x] Android NDK `.so` через `katek build --target android-*`.
- [x] Release-оптимизация `-O3 -march=native -flto`.

### M6. Качество

- [x] `tests/run.ps1` — сквозные тесты.
- [x] Позитивные и негативные примеры.
- [x] Fuzz лексера/парсера (`tests/fuzz.ps1`).
- [x] ASan/UBSan прогон (`tests/asan.ps1`).
- [x] CI (GitHub Actions).

### M7. 1.0

- [ ] Все пункты M2–M4 закрыты.
- [ ] Стабильный ABI рантайма.
- [ ] SemVer и `edition = "2026"`.
- [ ] Установщик для Windows/Linux/macOS.
- [ ] Реальный реестр пакетов.

## Как продолжить после паузы

1. Прочитать `PLAN.md` и `docs/ROADMAP.md`.
2. Запустить `xmake build`.
3. Запустить `powershell -File tests/run.ps1`.
4. Взять первый незакрытый пункт из M2–M4.
5. После изменения добавить тест и обновить документацию.

## Текущий статус

Версия: `1.0.0`. Реализованы компилятор, рантайм, std, C-интероп,
`katek`, JNI/Android-сборка, тесты, fuzz, ASan/UBSan и установщик.
Оставшиеся ограничения перечислены в `docs/ROADMAP.md`.
