# akt — план реализации и критерии готовности

Этот план описывает **последовательность вертикальных срезов**:
каждый этап заканчивается работающим артефактом, который можно
запустить и проверить. Даты не обещаются: важна не скорость, а
отсутствие «мёртвых» слоёв.

---

## 0. Принципы плана

1. **Один язык, одна спецификация.** `01-syntax.md` — источник
   истины. Реализация не расширяет язык «по ходу».
2. **Никакого GC, исключений и макросов.** Это не обсуждается на
   поздних этапах.
3. **C-граница — первоклассная.** Bindgen и провайдеры
   C-библиотек входят в MVP, а не «потом».
4. **Воспроизводимость с первого релиза.** Lock-файл и
   детерминированная кодогенерация — не опция.
5. **Каждый этап имеет exit criteria.** Этап не считается
   завершённым, пока команды из критериев не проходят.

---

## 1. M0 — спецификация и каркас

**Цель:** зафиксировать язык и структуру репозитория.

**Результат:**

- `README/00..12` без противоречий.
- `aktc/`, `katek/`, `std/`, `tests/`, `examples/`.
- `xmake.lua`, `CMakeLists.txt`, `Makefile` для toolchain.
- CI: линт документации, сборка, тесты.

**Exit criteria:**

```bash
python tools/check_docs.py README
xmake build
ctest --test-dir build
```

- Нет `.cdr`, `let`, `try`, `is`, `std::` в akt-блоках.
- Все ссылки на файлы существуют.
- README-индекс соответствует фактическим файлам.

**Риск:** расползание спецификации. Мера — правило «одно правило
— один файл».

---

## 2. M1 — лексер, парсер, диагностика

**Цель:** разобрать весь синтаксис ядра.

**Результат:**

- Лексер с позициями и вложенными f-строками.
- Парсер всех объявлений и инструкций.
- AST dump (`--dump-ast`).
- Диагностики `E-LEX-*`, `E-PARSE-*`.

**Exit criteria:**

```bash
aktc --dump-ast examples/full/inventory.akt > /dev/null
ctest --test-dir build -R "lexer|parser"
```

- `01-syntax.md` §15 разбирается без ошибок.
- Все negative-фикстуры дают одну ошибку с позицией.
- Время разбора 10k строк < 100 мс на ноутбуке.

**Риск:** неоднозначность `f"..."`. Мера — тесты на вложенные
кавычки и `{{`/`}}`.

---

## 3. M2 — семантика и типы

**Цель:** проверять корректные программы и отклонять некорректные.

**Результат:**

- Resolver модулей и имён.
- Type checker.
- Вывод `auto` и типа возврата.
- Перегрузка + fallback.
- Generics с мономорфизацией.
- Проверка `switch`, `Result`, `Option`, `?`.

**Exit criteria:**

```bash
ctest --test-dir build -R sema
```

- `f[T](args)` и вывод типов работают.
- `add(1u8, 2u8)` отклоняется.
- `?` в `fn main() int` отклоняется.
- Неполный `switch` без `default` отклоняется.
- Нет падений на fuzz-корпусе парсера.

**Риск:** слишком умный вывод типов. Мера — правило: никаких
неявных числовых повышений.

---

## 4. M3 — кодогенерация C11

**Цель:** получить исполняемый бинарник для ядра языка.

**Результат:**

- `.gen.c`/`.gen.h` на модуль.
- Struct/enum/union layout.
- `Result`/`Option` как тегированные union.
- Проверки overflow/div-by-zero.
- `defer` через cleanup/goto.
- Детерминированные имена.

**Exit criteria:**

```bash
aktc build examples/full/inventory.akt -o /tmp/inv
/tmp/inv
cc -std=c11 -Wall -Wextra -Werror -c /tmp/inv.gen.c
```

- Hello World, factorial, FizzBuzz, inventory работают.
- Два прогона дают одинаковый `.gen.c`.
- `defer` срабатывает при `return`, `break`, `continue`, `?`.

**Риск:** кодогенерация «в лоб» даёт медленный код. Мера —
бенчмарки после M6, не раньше.

---

## 5. M4 — модель памяти

**Цель:** реализовать регионы, арены, `shared`/`weak`, `manual`.

**Результат:**

- Дерево регионов.
- Escape-анализ `^T`.
- `shared` с atomic refcount.
- `weak.upgrade()`.
- Проверка циклов сильных ссылок.
- `defer` и порядок LIFO.

**Exit criteria:**

```bash
ctest --test-dir build -R memory
```

Negative-тесты:

- возврат `^T` на локальную переменную;
- `shared`-цикл;
- `arena` как значение;
- `auto` в другом потоке по ссылке.

Positive-тесты:

- арена освобождается целиком;
- `weak` не продлевает жизнь;
- `manual` + `defer` не течёт под ASan.

**Риск:** слишком строгий escape-анализ. Мера — начать с
консервативного запрета и ослаблять только с тестами.

---

## 6. M5 — C-интероп и bindgen

**Цель:** вызывать реальные C-библиотеки без ручных обёрток.

**Результат:**

- `cinclude`, `clink`, `cfn`, `cstruct`, `cunion`.
- Bindgen для подмножества C11.
- Маппинг типов.
- Генерация `.akti`.
- Кэш привязок.

**Exit criteria:**

```bash
katek bindgen zlib
aktc build examples/c/zlib_version.akt -o /tmp/zv
/tmp/zv
```

- `zlibVersion`, `sqlite3_open`, `printf` доступны.
- `const char*` → `cstring`, `char*` → `*cchar`.
- Неподдерживаемый макрос — warning, не падение.
- Сгенерированный `.akti` детерминирован.

**Риск:** парсить весь C — бесконечно. Мера — явно
поддерживаемое подмножество и диагностика на остальное.

---

## 7. M6 — `katek` MVP

**Цель:** собирать проект одной командой.

**Результат:**

- `katek new/build/run/test/clean`.
- `katek.toml` и `katek.lock`.
- Граф модулей и инкрементальная сборка.
- Кэш объектов.
- `--locked`, `--offline`.

**Exit criteria:**

```bash
katek new demo && cd demo && katek run
katek build --locked
katek clean && katek build
```

- Повторная сборка без изменений не вызывает `cc`.
- Изменение модуля пересобирает только зависимые.
- `katek.lock` детерминирован.

**Риск:** свой формат вместо TOML. Мера — использовать готовый
TOML-парсер на C.

---

## 8. M7 — провайдеры C-зависимостей

**Цель:** `katek c add zlib` работает без ручных путей.

**Результат:**

- `xrepo`, `vcpkg`, `conan`, `cmake`, `pkg-config`, `system`,
  `path`, `prebuilt`.
- Единый интерфейс include/lib/defines.
- `katek vendor`.
- Диагностика ABI/версий.

**Exit criteria:**

```bash
katek c add zlib --provider xrepo --version 1.3.1
katek c add openssl --provider pkg-config
katek c add sqlite3 --provider cmake --package SQLite3
katek build --locked --offline
```

- В `katek.lock` есть provider/version/checksum.
- Сборка не ходит в сеть при `--offline`.
- Конфликт версий даёт понятную ошибку.

**Риск:** разные провайдеры возвращают разные имена библиотек.
Мера — нормализация в `native.json` + тесты на каждом провайдере.

---

## 9. M8 — stdlib Tier 0

**Цель:** писать реальные утилиты без C.

**Результат:**

- `std.error/mem/ptr/alloc/io/fs/path/env/os/process`.
- `std.string/fmt/collections/math/time`.
- `std.thread/sync/atomic`.
- `std.test/log/debug`.
- `std.c`.

**Exit criteria:**

```bash
katek test std
```

- Все тесты проходят под ASan и TSan.
- `Vector`, `Map`, `Set`, `Deque` проходят property-тесты.
- Нет утечек в `std.io`/`std.fs`.
- `std.thread` не имеет гонок под TSan.

**Риск:** stdlib разрастается. Мера — Tier 0 только то, что
нужно большинству; остальное — feature/пакеты.

---

## 10. M9 — XMake и CMake backend

**Цель:** альтернативные сборки без потери воспроизводимости.

**Результат:**

- `katek build --backend xmake`.
- `katek build --backend cmake`.
- Экспорт akt-библиотеки в CMake.
- Генерация `xmake.lua` без абсолютных путей.

**Exit criteria:**

```bash
katek build --backend xmake
katek build --backend cmake
cmake -S . -B build -DAKT_USE_CMAKE=ON && cmake --build build
```

- `internal` не требует XMake/CMake.
- Повторная генерация даёт тот же файл.
- CMake-проект линкует akt-библиотеку.

**Риск:** XMake/CMake становятся обязательными. Мера — backend
только по явному флагу.

---

## 11. M10 — async и многопоточность

**Цель:** HTTP-клиент/сервер и конкурентные задачи.

**Результат:**

- `async fn`/`await`, executor.
- `async.all/select/timeout`.
- `std.net`, `std.http`.
- Каналы, `Mutex`, `RwLock`, `Atomic`.

**Exit criteria:**

```bash
katek test std/async std/http std/net
```

- HTTP-клиент скачивает JSON.
- HTTP-сервер отвечает на 1000 запросов без утечек.
- `async.timeout` корректно отменяет задачу.
- TSan не находит гонок в тестах.

**Риск:** async-рантайм разрастается. Мера — минимальный
executor; тяжёлые вещи — в пакетах.

---

## 12. M11 — stdlib Tier 1

**Цель:** покрыть типовые прикладные задачи.

**Результат:**

- `std.json`, `std.regex`, `std.toml`, `std.yaml`, `std.csv`.
- `std.crypto`, `std.compress`, `std.archive`.
- `std.db.sqlite`.
- `std.cli`, `std.term`, `std.log`.
- Feature-флаги.

**Exit criteria:**

```bash
katek test --features json,regex,sqlite
```

- Без feature модуль не попадает в бинарник.
- `json.parse_as[T]` работает на вложенных структурах.
- SQLite-тесты проходят с провайдером xrepo.
- `std.crypto` использует проверенную C-библиотеку.

**Риск:** Tier 1 превращается в «всё подряд». Мера — правило
«большая C-библиотека → пакет».

---

## 13. M12 — инструменты

**Цель:** комфортная разработка.

**Результат:**

- `katek fmt` (идемпотентный).
- `katek test` с фильтрами и таймаутами.
- `katek doc`.
- LSP: диагностика, completion, go-to-definition, hover.
- `--json` диагностика.

**Exit criteria:**

```bash
katek fmt --check .
katek test --filter add
katek doc --check
```

- `fmt` не меняет AST.
- LSP показывает ошибки в VS Code.
- Completion не предлагает запрещённые слова.

**Риск:** форматтер становится источником конфликтов. Мера —
канонический стиль в `01-syntax.md` §14.

---

## 14. M13 — кросс-компиляция и воспроизводимость

**Цель:** одинаковый бинарник на разных машинах.

**Результат:**

- `--target`, `--sysroot`.
- `-ffile-prefix-map`, `SOURCE_DATE_EPOCH`.
- Детерминированный порядок символов.
- CI-матрица: Linux x86_64/aarch64, macOS, Windows, wasm32-wasi.

**Exit criteria:**

```bash
katek build --release
sha256sum target/bin/app
katek clean && katek build --release
sha256sum target/bin/app
# хэши совпадают

aktc build --target aarch64-linux-gnu examples/full/inventory.akt
file /tmp/inventory
```

- Два чистых билда дают одинаковый хэш.
- Кросс-бинарник запускается в qemu.
- Нет абсолютных путей в артефактах.

**Риск:** разные версии `cc` дают разный код. Мера — фиксировать
версию toolchain в CI и lock.

---

## 15. M14 — bootstrap

**Цель:** `aktc`, написанный на akt.

**Результат:**

- Stage 0: `aktc` на C.
- Stage 1: `aktc.akt` компилируется Stage 0.
- Stage 2: Stage 1 компилирует сам себя.
- Сравнение поведения Stage 1/Stage 2.

**Exit criteria:**

```bash
make bootstrap
cmp build/stage1/aktc build/stage2/aktc
build/stage2/aktc --version
```

- Stage 1 и Stage 2 проходят один тест-набор.
- Время bootstrap < 10 минут на CI.
- Нет ручных шагов.

**Риск:** bootstrap ломается на каждой новой функции. Мера —
запускать его в CI с первого дня.

---

## 16. M15 — экосистема и реестр

**Цель:** пакеты и воспроизводимые зависимости.

**Результат:**

- Реестр пакетов (HTTP + git).
- `katek add/remove/update/publish`.
- SemVer-резолвинг.
- Подписи пакетов.
- `katek vendor`.

**Exit criteria:**

```bash
katek add akt-json
katek publish --dry-run
katek vendor
katek build --locked --offline
```

- Пакет нельзя опубликовать без лицензии и README.
- `yanked`-версии не резолвятся.
- `vendor/` достаточен для offline-сборки.

**Риск:** реестр — это инфраструктура. Мера — начать с
git-зависимостей и локального реестра.

---

## 17. M16 — 1.0

**Цель:** стабильный язык и toolchain.

**Критерии 1.0:**

- [ ] Спецификация `01-syntax.md` заморожена в `edition = "2026"`.
- [ ] `aktc` и `katek` собираются из исходников одной командой.
- [ ] Bootstrap воспроизводим.
- [ ] Tier 0 stdlib стабилен; Tier 1 — с feature-флагами.
- [ ] C-библиотеки подключаются через xrepo/vcpkg/Conan/CMake/
      pkg-config.
- [ ] XMake и CMake backend работают.
- [ ] Кросс-компиляция на 4 цели.
- [ ] Два чистых release-билда дают одинаковый хэш.
- [ ] Нет открытых ошибок уровня `P0`/`P1`.
- [ ] Документация проверяется в CI.

**Не входит в 1.0:**

- GUI, ML, графика, аудио, видео в std.
- Макросы, метапрограммирование, наследование.
- Сборщик мусора.
- IDE-плагины beyond LSP.
- Менеджер пакетов с централизованной модерацией.

---

## 18. Метрики

| Метрика | Цель к 1.0 |
|---|---|
| Время сборки hello world | < 200 мс |
| Время сборки 100k строк | < 5 с |
| Размер `aktc` | < 5 МБ |
| Размер hello world | < 50 КБ |
| Замедление vs C `-O2` | ≤ 10% |
| Покрытие тестами ядра | ≥ 80% |
| Открытые P0/P1 баги | 0 |
| Воспроизводимость release | 100% |

---

## 19. Порядок работ

```text
M0  спецификация
 │
M1  lexer/parser
 │
M2  sema/types
 │
M3  C codegen ──────┐
 │                  │
M4  memory          │
 │                  │
M5  C interop/bindgen
 │                  │
M6  katek MVP       │
 │                  │
M7  native providers
 │                  │
M8  stdlib Tier 0   │
 │                  │
M9  xmake/cmake ────┘
 │
M10 async
 │
M11 stdlib Tier 1
 │
M12 tooling
 │
M13 cross/repro
 │
M14 bootstrap
 │
M15 ecosystem
 │
M16 1.0
```

Параллелить можно M9 (backend) и M10 (async) после M7; M12
(tooling) — после M6. Нельзя начинать M14 до стабильного M3–M5.
