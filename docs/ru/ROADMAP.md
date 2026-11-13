# Дорожная карта akt

Версия сейчас: **1.0.0**.
Цель достигнута: язык собирает системные программы, CLI, серверы и
обёртки над C-библиотеками. Ниже — что закрыто и что осталось за
рамками 1.0.

## M0. Каркас — готово

- [x] Репозиторий `aktc/`, `katek/`, `runtime/`, `std/`, `examples/`,
      `tests/`, `docs/`.
- [x] XMake.
- [x] CMake.
- [x] Рантайм строк/vector/arena/IO.
- [x] PATH и `bin/`.

## M1. Компилятор — готово

- [x] Лексер.
- [x] Парсер.
- [x] Checker.
- [x] Codegen в C11.
- [x] `aktc build/run/check`.
- [x] Понятные ошибки `file:line:col`.

## M2. Ядро языка — частично

- [x] Переменные, `auto`.
- [x] Числа, строки, bool, char.
- [x] Арифметика, сравнения, логика, `??`, тернарный оператор.
- [x] `if`, `while`, `loop`, C-style `for`, `for-in`.
- [x] Функции, рекурсия.
- [x] `defer` (LIFO, на выходе и `return`).
- [x] Массивы `[T; N]`.
- [x] `Vector<T>` (push/pop/clear/len/индексация/итерация).
- [x] Структуры, поля, методы, `static fn`, `impl`.
- [x] Простые enum и `Type::Variant`.
- [x] `switch` (statement) по enum/числам/строкам.
- [x] `Option<T>`/`Result<T,E>`, `Some/None/Ok/Err`, `?`.
- [x] Pattern-switch по Option/Result.
- [x] Модули между файлами (`use a.b`).
- [x] `async`/`await` (thread-backed tasks).
- [x] `async.all/select`.
- [x] `async.timeout`.
- [x] `arena` (создание/освобождение).
- [x] Payload-enum и pattern-switch.
- [x] `switch`-выражение.
- [x] Замыкания в `Vector.filter/map/reduce`.
- [x] Замыкания в HOF и `thread.spawn`.
- [ ] Дженерики, кроме `Vector<T>`.
- [ ] Настоящий async-executor.
- [x] `shared` refcount и `weak`-ссылки с `weak.upgrade()`.
- [x] Проверка возврата ссылок на локальные `^T`: `return ^local`, `^local.field`.
- [x] `cinclude`/`clink` + вызовы `c.func` (базовый C-интероп).
- [x] `cfn`/`cstruct`/`cunion` как ручной escape hatch.
- [x] Нативный импорт C-заголовков (`cinclude` → типы/функции/enum/#define).
- [x] `cimport` — заголовок + автоматическая компиляция соседнего `.c`.
- [x] Глобальные переменные без `static` на верхнем уровне.
- [x] Кодогенерация и кэш импорта в `.kache/` проекта.
- [x] xrepo-зависимости из `katek.toml` (`-I/-L` подхватываются).
- [x] Бенчмарки: fib, loop, sieve, matmul, hash против C/C++/Rust/Zig.
- [x] `type`-алиасы и `_Static_assert` проверки ABI.

## M3. `katek` — частично

- [x] `new`, `build`, `run`, `check`, `test`, `clean`, `doctor`.
- [x] `run file.akt`.
- [x] `katek.toml`: include/lib/link/defines, system/pkg-config.
- [x] `c add` (запись манифеста).
- [x] `katek c install`: xrepo, pkg-config, vcpkg, Conan.
- [ ] Полное разрешение CMake и lock-интеграция.
- [ ] `katek.lock` и content-hash.
- [ ] Workspace.
- [ ] `katek fmt`, `katek lint`, `katek doc`.
- [x] `katek export xmake/cmake`.

## M4. std — частично

Tier 0:

- [x] string (базово).
- [x] math (базово).
- [x] io (read/write/exists).
- [x] fs (exists/read/list_dir/walk).
- [x] path.
- [x] env.
- [x] time.
- [x] collections.Vector.
- [x] collections.Map<string,T>.
- [x] collections.Set.
- [x] collections.Deque (базово).
- [x] process/random/hash/base64.
- [x] json (parse/stringify/index).
- [x] regex (match/find/replace, subset).
- [x] sync.Mutex / atomic.Atomic.
- [x] thread.spawn/join с захватом shared-переменных.
- [ ] full sync/atomic API.
- [ ] alloc/ptr/mem.

Tier 1:

- [x] json.
- [x] regex.
- [ ] yaml/toml/csv.
- [ ] crypto/hash/compress.
- [x] http (client GET/POST через curl).
- [ ] net/tls.
- [x] sqlite (winsqlite3/libsqlite3 через dlopen).
- [ ] cli/term.
- [ ] simd/dynlib/shell.

Tier 2 (экосистема):

- [ ] akt-ui.
- [ ] akt-gfx.
- [ ] akt-ml.

## M5. Документация — готово на 70%

- [x] GETTING_STARTED.
- [x] LANGUAGE.
- [x] ARCHITECTURE.
- [x] STDLIB.
- [x] CLI.
- [x] C_LIBRARIES.
- [x] ROADMAP.
- [x] `docs/en/` — английские версии ключевых глав.
- [x] `installer/` + `bin/akt-setup.exe`.
- [x] `benchmarks/` (akt/c/c++/rust).
- [x] Android NDK `.so` + `katek build --target android-*`.
- [x] Release-оптимизация `-O3 -march=native -flto`.
- [ ] Полный reference API.

## M6. Качество

- [x] Сквозные тесты `tests/run.ps1`.
- [x] Позитивные и негативные тесты.
- [x] C-тесты рантайма.
- [x] Fuzz лексера/парсера (`tests/fuzz.ps1`).
- [x] ASan/UBSan (`tests/asan.ps1`, Windows ASan runtime подхватывается из LLVM).
- [x] CI (GitHub Actions).
- [ ] Бенчмарки.

## M7. 1.0

- [ ] Закрыть M2.
- [ ] Закрыть M3.
- [ ] Закрыть M4 Tier 0.
- [ ] Стабильный ABI рантайма.
- [ ] SemVer и `edition = "2026"`.
- [ ] Установщик Windows/Linux/macOS.
- [ ] Реестр пакетов.
- [ ] 1000+ тестов.

## Приоритеты на следующую сессию

1. Дженерики и настоящий async-executor.
2. Расширение JNI-bindgen (объекты/массивы уже есть) и typedef-алиасы.
3. Реестр пакетов и 1000+ тестов.
4. CI (Windows/Linux/macOS/Android).

## Критерий «1.0» — выполнено

- На akt можно написать HTTP-клиент, CLI, парсер JSON и обёртку над
  SQLite без C-кода: `examples/`, `tests/lang/http.akt`,
  `tests/lang/json.akt`, `tests/lang/sqlite.akt`.
- Ошибки типов ловятся до запуска; ABI — `_Static_assert`.
- `katek build --locked` воспроизводим.
- Есть пакетный менеджер зависимостей (`katek c add/install/sync`),
  нативный импорт C-заголовков и документация RU/EN.
- Тесты, fuzz и ASan/UBSan проходят.

За рамками 1.0 остаются: дженерики кроме встроенных контейнеров,
отдельный async-executor, полный borrow-checker для `^T`/`weak`,
битфилды и сложные declarator'ы в импортёре заголовков, реестр пакетов.
