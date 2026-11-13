# katek — система сборки и управления зависимостями

## 1. Назначение

`katek` — не «ещё один Make». Он решает четыре задачи:

1. **Сборка akt-кода**: `aktc` + системный C-компилятор.
2. **Управление akt-пакетами**: версии, lock, registry, workspace.
3. **Управление C-библиотеками**: xrepo, vcpkg, Conan, CMake
   `find_package`, pkg-config, system, vendored, prebuilt.
4. **Воспроизводимость**: content-hash, offline-сборка, одинаковый
   артефакт.

`katek` не пытается заменить CMake или XMake внутри чужих проектов.
Он **использует** их как провайдеров C-пакетов и как опциональные
backend'ы сборки. Это принципиально: C-экосистема уже существует,
и akt должен в неё входить, а не требовать переписать всё.

---

## 2. Установка

```bash
# Linux / macOS
curl -fsSL https://katek.akt.dev/install.sh | sh

# Windows
winget install akt.katek

# Из исходников (XMake)
git clone https://github.com/akt-lang/akt
cd akt
xmake build katek
xmake install katek
```

Требования:

| Компонент | Обязателен | Зачем |
|---|---|---|
| `aktc` | Да | Компиляция akt |
| C-компилятор (`cc`) | Да | Сборка сгенерированного C |
| XMake | Нет | Backend, провайдер C-пакетов |
| CMake | Нет | Провайдер C-пакетов, backend |
| pkg-config | Нет | Провайдер системных библиотек |
| vcpkg / Conan | Нет | Провайдеры C-пакетов |
| Git | Для git-зависимостей | |

`katek doctor` проверяет окружение и подсказывает, чего не хватает.

---

## 3. Быстрый старт

```bash
katek new hello
cd hello
katek run
```

```text
hello/
├── katek.toml
├── katek.lock
├── src/
│   └── main.akt
├── tests/
│   └── main_test.akt
├── examples/
│   └── basic.akt
└── target/
```

Конвенция:

- `src/main.akt` — точка входа бинарника;
- `src/*.akt` — модули;
- `tests/*.akt` или `*_test.akt` — тесты;
- `examples/*.akt` — примеры;
- `target/` — артефакты, в `.gitignore`.

Если проект следует конвенции, `katek.toml` не обязателен:

```bash
katek new hello --bare
cd hello
printf 'fn main() { println("hi") }\n' > src/main.akt
katek run
```

---

## 4. Манифест `katek.toml`

### 4.1. Минимум

```toml
[package]
name = "hello"
version = "1.0.0-beta.1"
edition = "2026"
```

### 4.2. Полный пример

```toml
[package]
name = "myapp"
version = "1.0.0"
edition = "2026"
authors = ["Alice <alice@example.com>"]
description = "A tiny web server"
license = "MIT OR Apache-2.0"
repository = "https://github.com/alice/myapp"
homepage = "https://myapp.dev"
keywords = ["web", "http"]
categories = ["web", "cli"]

[build]
backend = "internal"          # internal | xmake | cmake
jobs = 0                      # 0 = число ядер
checks = "debug"              # debug | always | never
warnings = "all"              # none | default | all

[profile.release]
optimize = "speed"            # none | speed | size
debug = false
lto = true
march = "generic"             # generic | native | <cpu>
panic = "abort"               # abort | unwind (unwind только с C++ ABI)

[profile.debug]
optimize = "none"
debug = true
checks = "always"

# akt-зависимости
[dependencies]
json = "1.5"
http = { version = "2.0", features = ["tls"] }
local-util = { path = "../local-util" }
git-dep = { git = "https://github.com/x/y", tag = "v1.2" }

[dev-dependencies]
test-helpers = "0.3"

[features]
default = ["std"]
std = []
tls = ["dep:openssl"]

# C-зависимости
[native]
providers = ["xrepo", "vcpkg", "conan", "cmake", "pkg-config", "system"]
include = ["vendor/include"]
lib = ["vendor/lib"]
defines = ["MYAPP_VERSION=\"1.0.0\""]
link = ["m"]

[native.dependencies]
zlib = { provider = "xrepo", version = "1.3.1" }
sqlite3 = { provider = "cmake", package = "SQLite3", version = "3.45" }
curl = { provider = "pkg-config", package = "libcurl", version = ">=8.0" }
sdl2 = { provider = "vcpkg", version = "2.30.0", features = ["x11"] }
imgui = { provider = "xrepo", version = "1.90", features = ["docking"] }
mylib = { path = "vendor/mylib", build = "cmake" }
prebuilt = { path = "vendor/prebuilt", kind = "static" }

# Цели
[targets.app]
kind = "binary"
main = "src/main.akt"
profile = "release"

[targets.mylib]
kind = "library"
src = "src/lib.akt"

[targets.wasm]
kind = "binary"
main = "src/main.akt"
target = "wasm32"

[workspace]
members = ["packages/*", "apps/*"]
```

### 4.3. Схема `[native.dependencies]`

| Поле | Обязательно | Значение |
|---|---|---|
| `provider` | Да* | `xrepo`, `vcpkg`, `conan`, `cmake`, `pkg-config`, `system`, `path`, `prebuilt` |
| `version` | Нет | SemVer, диапазон или тег |
| `package` | Нет | Имя пакета у провайдера, если отличается |
| `features` | Нет | Фичи пакета |
| `optional` | Нет | `true` — не ошибка, если не найден |
| `static` | Нет | `true` — линковать статически |
| `link` | Нет | Дополнительные `-l` |
| `include` | Нет | Дополнительные include-пути |
| `lib` | Нет | Дополнительные lib-пути |
| `defines` | Нет | `-D` для C-заголовков |
| `path` | Для `path`/`prebuilt` | Локальный путь |
| `build` | Нет | `cmake`, `xmake`, `make`, `meson`, `none` |
| `triplet` | Нет | vcpkg triplet |
| `profile` | Нет | Conan profile |

`*` — если `provider` не указан: при наличии `path` выбирается
`path` (или `prebuilt` при `kind`), иначе первый доступный из
`[native].providers`.

---

## 5. `katek.lock`

Генерируется автоматически, коммитится в git, вручную не
редактируется.

```toml
version = 2

[[package]]
name = "json"
kind = "akt"
version = "1.5.1"
source = "registry+https://hub.akt.dev"
checksum = "sha256:..."
dependencies = ["serde"]

[[package]]
name = "serde"
kind = "akt"
version = "1.0.4"
source = "registry+https://hub.akt.dev"
checksum = "sha256:..."

[[package]]
name = "zlib"
kind = "native"
provider = "xrepo"
version = "1.3.1"
source = "xrepo:zlib/1.3.1"
checksum = "sha256:..."
features = []
link = ["z"]
include = ["/home/user/.xmake/packages/z/zlib/1.3.1/.../include"]
lib = ["/home/user/.xmake/packages/z/zlib/1.3.1/.../lib"]
defines = []

[[package]]
name = "sqlite3"
kind = "native"
provider = "cmake"
version = "3.45.0"
source = "cmake:SQLite3"
checksum = "sha256:..."
targets = ["SQLite::SQLite3"]
```

Правила:

- `katek build` использует **ровно** версии из lock.
- Если lock нет — `katek build` создаёт его (как `cargo`), но в CI
  рекомендуется `--locked`.
- `katek update` обновляет lock по SemVer.
- `katek update --precise pkg@1.2.3` фиксирует точную версию.
- `katek lock --check` проверяет, что lock соответствует манифесту.
- Для native-пакетов в lock попадают провайдер, версия, checksum,
  features и ссылки на include/lib. Абсолютные пути не считаются
  частью идентичности пакета; при переносе проекта они
  пересчитываются провайдером.

---

## 6. C-библиотеки: модель зависимостей

### 6.1. Провайдеры

| Провайдер | Что делает | Когда использовать |
|---|---|---|
| `xrepo` | Пакеты XMake (`xrepo install`) | Основной путь: широкая коллекция C/C++-пакетов |
| `vcpkg` | Пакеты vcpkg | Если проект уже использует vcpkg |
| `conan` | Пакеты Conan | Если проект уже использует Conan |
| `cmake` | `find_package` через CMake-пробу | Пакеты с `*Config.cmake` |
| `pkg-config` | `pkg-config --cflags --libs` | Системные библиотеки Unix |
| `system` | Заголовки/библиотеки в стандартных путях | `-lm`, `-lpthread`, `-lz` |
| `path` | Локальный исходник + его build system | Vendored/форк |
| `prebuilt` | Готовые `.a`/`.so`/`.dll` + include | Проприетарные SDK |

Порядок по умолчанию: `xrepo → pkg-config → system`. В манифесте
можно переопределить.

### 6.2. Разрешение

```text
1. Прочитать [native.dependencies].
2. Для каждой зависимости выбрать провайдера:
   - явный provider;
   - иначе первый доступный из [native].providers.
3. Если есть katek.lock и --locked — использовать его.
   Иначе — резолвить версию и писать lock (только update/add).
4. Скачать/собрать пакет (сеть — только fetch/update/add).
5. Получить include/lib/defines/link/targets.
6. Сгенерировать .katek/native.json.
7. Bindgen читает заголовки; aktc компилирует; cc линкует.
```

### 6.3. Пример: zlib через xrepo

```toml
[native.dependencies]
zlib = { provider = "xrepo", version = "1.3.1" }
```

```akt
use std.c

cinclude <zlib.h>
clink "z"

fn main() int {
    // const char* из C → cstring; для string нужна явная конверсия.
    string v = c.to_string(c.zlibVersion())
    println(f"zlib {v}")
    return 0
}
```

Что делает `katek`:

1. Проверяет lock; если нет — `xrepo install zlib 1.3.1`.
2. Получает `include` и `lib` пути.
3. Добавляет `-I.../include -L.../lib -lz`.
4. Запускает bindgen для `zlib.h`.
5. Компилирует и линкует.

### 6.4. Пример: SQLite через CMake

```toml
[native.dependencies]
sqlite3 = { provider = "cmake", package = "SQLite3", version = "3.45" }
```

`katek` вызывает CMake-пробу, получает target `SQLite::SQLite3`,
include-пути и библиотеку. Если пакет не найден — ошибка с
подсказкой (`apt install libsqlite3-dev`, `vcpkg install sqlite3`
и т.п.).

### 6.5. Пример: локальный vendored-пакет

```toml
[native.dependencies]
mylib = { path = "vendor/mylib", build = "cmake" }
```

`katek`:

1. Конфигурирует `vendor/mylib` через CMake в
   `.katek/native/mylib/build`.
2. Собирает (`cmake --build`).
3. Устанавливает в `.katek/native/mylib/install`.
4. Добавляет include/lib в сборку.

Для `build = "xmake"` используется `xmake`; для `make` — `make`;
для `none` — ожидается, что include/lib уже указаны.

### 6.6. Пример: prebuilt SDK

```toml
[native.dependencies]
sdk = { path = "vendor/sdk", kind = "static" }
```

Ожидаемая структура:

```text
vendor/sdk/
├── include/
└── lib/
    ├── libsdk.a       # Linux/macOS
    └── sdk.lib        # Windows
```

### 6.7. Транзитивные зависимости

Транзитивные C-зависимости разрешает провайдер:

- xrepo — сам;
- vcpkg — сам;
- Conan — сам;
- CMake — через `find_package` и targets;
- pkg-config — через `Requires.private`.

`katek` записывает в lock только прямые зависимости и итоговый
набор флагов; транзитивные пакеты фиксируются провайдером в его
собственном lock/манифесте. Для полной воспроизводимости
используйте `katek c vendor`.

### 6.8. `katek c vendor`

```bash
katek c vendor
```

Копирует исходники/артефакты всех native-зависимостей в
`vendor/` и переписывает lock на `provider = "path"`. После этого
сборка полностью offline и не зависит от сети/провайдера.

---

## 7. Привязки C: `cinclude`, `cfn`, `.akti`

### 7.1. `cinclude`

```akt
cinclude <zlib.h>
cinclude "vendor/foo.h"
```

- Заголовок ищется в include-путях из `[native]` и провайдеров.
- Bindgen генерирует `.akti` в кэш.
- Имена доступны как `c.name`.
- Если конфликта нет — и как `name`.

### 7.2. Ручные привязки

```akt
cfn deflateInit2_(*z_stream strm, i32 level, i32 method,
                  i32 windowBits, i32 memLevel, i32 strategy,
                  cstring version, i32 stream_size) i32

cstruct z_stream {
    *const u8 next_in
    u32 avail_in
    u32 total_in
    *u8 next_out
    u32 avail_out
    u32 total_out
    cstring msg
    *void state
    *void zalloc
    *void zfree
    *void opaque
    i32 data_type
    u64 adler
    u64 reserved
}
```

### 7.3. `.akti` — интерфейсный файл

`.akti` — это akt-файл с объявлениями C-API. Он может быть:

- сгенерирован bindgen'ом;
- написан вручную для сложных заголовков;
- распространён вместе с C-библиотекой.

```akt
// zlib.akti
cstruct z_stream { /* ... */ }
cfn deflateInit2_(...) i32
clink "z"
```

Использование:

```akt
use zlib
cinclude "zlib.h"   // или .akti, если заголовка нет
```

### 7.4. Ограничения bindgen

Не поддерживаются:

- макросы с вычисляемыми значениями (`#define X (a+b)`);
- `_Generic`;
- сложные `__attribute__`;
- вложенные анонимные структуры без имени;
- C++ (только C ABI).

Для них — `cfn`, `cstruct`, `cunion` или ручной `.akti`.
Bindgen сообщает о неподдержанных объявлениях с `file:line`.

---

## 8. Команды

### 8.1. Проект

```bash
katek new <name>              # новый бинарник
katek new <name> --lib        # библиотека
katek new <name> --bare       # без манифеста
katek init                    # инициализировать в текущей папке
```

### 8.2. Сборка и запуск

```bash
katek build                   # debug
katek build --release
katek build -p <pkg>
katek build --all
katek build --jobs 8
katek build --backend xmake
katek build --backend cmake
katek build --emit c
katek build --emit obj
katek build --time
katek build --reproducible
katek build --locked
katek run
katek run -- arg1 arg2
katek run --release
katek run --watch
katek run --script script.akt
```

### 8.3. Тесты и проверки

```bash
katek test
katek test --release
katek test --filter add
katek test --jobs 4
katek test --nocapture
katek check                   # типы/память без кодогенерации
katek fmt
katek fmt --check
katek lint
katek doc
katek doc --open
```

### 8.4. akt-зависимости

```bash
katek add json
katek add json@1.5
katek add json --features derive
katek add --dev test-helpers
katek add --path ../local
katek add --git https://github.com/x/y --tag v1.2
katek remove json
katek update
katek update json
katek update --precise json@1.5.1
katek tree
katek tree --depth 2
katek why json
katek outdated
katek audit
katek vendor
katek fetch
katek lock --check
```

### 8.5. C-зависимости

```bash
katek c add zlib --provider xrepo --version 1.3.1
katek c add sqlite3 --provider cmake --package SQLite3
katek c add curl --provider pkg-config --package libcurl
katek c remove zlib
katek c update
katek c tree
katek c sync                    # перегенерировать привязки
katek c vendor
katek c search zlib             # поиск у провайдера
katek c info zlib
katek c doctor                  # проверить провайдеры
```

`katek c add` изменяет `katek.toml` и `katek.lock`, но не
запускает полную сборку. `katek c sync` обновляет
`.katek/native.json` и `.akti`-кэш.

### 8.6. Публикация

```bash
katek publish
katek publish --dry-run
katek search <query>
katek info <pkg>
katek login
katek logout
```

### 8.7. Обслуживание

```bash
katek clean
katek clean --cache
katek size
katek bench
katek doctor
katek export xmake
katek export cmake
```

---

## 9. Backend'ы сборки

### 9.1. `internal` (по умолчанию)

`katek` сам вызывает `aktc`, `cc`, `ar` и линкер. Быстро, без
генерации файлов, полностью контролирует флаги и кэш.

### 9.2. `xmake`

```bash
katek build --backend xmake
```

или

```toml
[build]
backend = "xmake"
```

`katek` генерирует `.katek/build/xmake.lua`:

```lua
set_project("myapp")
set_version("1.0.0")
set_languages("c11")

add_rules("mode.debug", "mode.release")

-- C-зависимости из katek.lock
add_requires("zlib 1.3.1")
add_requires("sqlite3 3.45.0")

target("myapp")
    set_kind("binary")
    add_files(".katek/gen/*.gen.c")
    add_packages("zlib", "sqlite3")
    add_includedirs(".katek/gen")
    add_linkdirs(".katek/gen")
    set_targetdir("target/release")
```

Затем:

```bash
xmake f -m release --toolchain=... 
xmake build
```

Плюсы: XMake сам разрешает C-пакеты, умеет toolchain'ы и
cross-compilation. Минусы: меньше контроля над детерминизмом;
`katek --reproducible` проверяет итог и предупреждает, если
native-пакеты собраны недетерминированно.

### 9.3. `cmake`

```bash
katek build --backend cmake
```

`katek` генерирует `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(myapp C)

set(CMAKE_C_STANDARD 11)

find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(myapp .katek/gen/*.gen.c)
target_include_directories(myapp PRIVATE .katek/gen)
target_link_libraries(myapp PRIVATE SQLite::SQLite3 ZLIB::ZLIB)
```

Плюсы: знакомая интеграция, toolchain-файлы, IDE. Минусы: два
прохода конфигурации, медленнее.

### 9.4. Что выбрать

| Ситуация | Backend |
|---|---|
| Обычная разработка | `internal` |
| Уже есть XMake и нужны его пакеты/toolchain | `xmake` |
| Уже есть CMake и нужны `find_package`/IDE | `cmake` |
| CI и reproducible | `internal` + `--locked --reproducible` |
| Встраивание akt в C/C++-проект | `export cmake`/`export xmake` |

---

## 10. Кэш и инкрементальность

### 10.1. Расположение

```text
~/.cache/katek/
├── obj/                 # .o по content-hash
├── bindings/            # .akti по hash заголовков
├── native/              # собранные path-зависимости
├── registry/            # akt-пакеты
├── sysroots/            # cross-compile sysroot
└── scripts/             # AOT-кэш скриптов
```

### 10.2. Ключ

```text
hash = sha256(
  содержимое .akt +
  транзитивные akt-зависимости +
  native include/lib/defines/link +
  версия aktc +
  версия katek +
  флаги +
  target +
  sysroot +
  профиль
)
```

Не mtime. Причина: mtime ломается при `git checkout`, `touch`,
смене машины и CI. Content-hash даёт настоящую
инкрементальность и reproducible builds.

### 10.3. Общий кэш

Кэш общий между проектами. Если два проекта используют
`json@1.5.1` или `zlib@1.3.1` с одинаковыми флагами — объект
собирается один раз.

---

## 11. Детерминизм

### 11.1. Гарантии

Одинаковый вход → одинаковый выход (побайтово), если:

- версии akt-пакетов и native-пакетов зафиксированы;
- native-пакеты собраны детерминированно или vendored;
- C-компилятор и его версия совпадают;
- флаги совпадают;
- target/sysroot совпадают.

### 11.2. Что делается

- нет `__DATE__`/`__TIME__`;
- нет случайных имён;
- нет абсолютных путей в debug-info;
- сортировка файлов и флагов;
- фиксированный seed хэш-таблиц;
- детерминированный порядок линковки;
- `SOURCE_DATE_EPOCH` учитывается;
- `katek build --reproducible` собирает дважды и сравнивает.

### 11.3. Ограничение

Внешние native-пакеты (xrepo/vcpkg/Conan/CMake) могут встраивать
timestamps или абсолютные пути. `katek`:

- передаёт `SOURCE_DATE_EPOCH`, `-ffile-prefix-map`, `-fdebug-prefix-map`;
- фиксирует версии и checksum;
- предупреждает, если native-пакет собран недетерминированно;
- рекомендует `katek c vendor` для полной воспроизводимости.

Честно: **полная reproducible-сборка с произвольными C-пакетами
не гарантируется**. Гарантируется детерминизм akt-части и
фиксация native-части.

---

## 12. Кросс-компиляция

```bash
katek build --target x86_64-linux-gnu
katek build --target aarch64-linux-gnu
katek build --target aarch64-macos
katek build --target x86_64-windows-msvc
katek build --target wasm32
katek build --target arm-cortex-m4
katek build --target riscv64-linux-gnu
```

Как работает:

- `katek` скачивает sysroot в `~/.cache/katek/sysroots/<target>/`
  (если разрешено).
- `aktc` получает `--target` и `--sysroot`.
- C-компилятор выбирается автоматически
  (`aarch64-linux-gnu-gcc`, `clang --target=...` и т.п.).
- Native-провайдеры получают triplet/toolchain:
  - xrepo: `--toolchain=...`;
  - vcpkg: `--triplet`;
  - Conan: `-pr:b -pr:h`;
  - CMake: `-DCMAKE_TOOLCHAIN_FILE=...`.
- Если кросс-компилятора нет — `katek doctor` подсказывает пакет.

---

## 13. Workspace

```text
myproject/
├── katek.toml           # workspace
├── katek.lock           # один на workspace
├── packages/
│   ├── core/
│   │   ├── katek.toml
│   │   └── src/
│   ├── http/
│   └── json/
└── apps/
    └── server/
        ├── katek.toml
        └── src/
```

```toml
[workspace]
members = ["packages/*", "apps/*"]
resolver = "2"

[workspace.dependencies]
json = "1.5"
zlib = { provider = "xrepo", version = "1.3.1" }
```

```bash
katek build
katek build -p core
katek build -p core,http
katek build --exclude core
katek add -p core url
katek test --workspace
```

---

## 14. Реестр `hub.akt.dev`

- Центральный реестр akt-пакетов.
- Любой git-репозиторий может быть пакетом и без регистрации.
- Реестр — индекс для поиска и метаданных, не обязательное
  хранилище.
- C-пакеты **не** публикуются в `hub.akt.dev`; они ссылаются на
  xrepo/vcpkg/Conan/CMake/pkg-config/system.
- `katek audit` проверяет известные уязвимости akt-пакетов и
  предупреждает о native-пакетах без версии.

Публикация:

```bash
katek publish
# 1. Проверка katek.toml
# 2. katek test
# 3. katek build --release
# 4. git tag v<version>
# 5. git push --tags
# 6. POST метаданных в hub.akt.dev
```

---

## 15. Семантика версий

- `1.0.0` — exact.
- `^1.0.0` — `>=1.0.0, <2.0.0`.
- `~1.0.0` — `>=1.0.0, <1.1.0`.
- `>=1.0.0` — от 1.0.0.
- `*` — любая.
- `"1.0"` = `^1.0.0`.

Для native-пакетов версия зависит от провайдера:

- xrepo — версия пакета;
- vcpkg — версия порта;
- Conan — версия рецепта;
- CMake/pkg-config — версия из `find_package`/`pkg-config`,
  если доступна; иначе фиксируется checksum/путь.

---

## 16. CI

### 16.1. GitHub Actions

```yaml
name: CI
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: akt-lang/setup-katek@v1
        with:
          version: "0.1"
      - run: katek check
      - run: katek fmt --check
      - run: katek test --locked
      - run: katek build --release --locked
      - run: katek build --release --locked --reproducible
```

### 16.2. GitLab CI

```yaml
image: akt-lang/katek:0.1
build:
  script:
    - katek check
    - katek test --locked
    - katek build --release --locked
```

### 16.3. Docker

```dockerfile
FROM akt-lang/katek:0.1 AS builder
WORKDIR /app
COPY . .
RUN katek build --release --locked

FROM scratch
COPY --from=builder /app/target/release/app /app
ENTRYPOINT ["/app"]
```

---

## 17. Скрипты

Файл `.akt` с shebang не требует `katek.toml`:

```akt
#!/usr/bin/env akt
println("Hello from script!")
```

```bash
chmod +x script.akt
./script.akt
```

`akt`:

1. Считает hash файла.
2. Если в кэше нет — AOT-компилирует в
   `~/.cache/akt/scripts/<hash>`.
3. Запускает бинарник.
4. Кэширует.

Это **не** интерпретация. Это AOT-компиляция с кэшем. Первый
запуск — секунды (компиляция), последующие — мгновенно.

```bash
katek run script.akt
katek run --watch script.akt
akt script.akt
```

---

## 18. Производительность

| Операция | Цель |
|---|---|
| `katek check` (10k строк) | < 100 мс |
| `katek build` инкрементальный (10k) | < 200 мс |
| `katek build` с нуля (10k) | < 2 с |
| `katek test` (100 тестов) | < 1 с |
| `katek fmt` (10k) | < 50 мс |
| Резолв 100 akt-зависимостей | < 50 мс |
| Резолв native-зависимостей | зависит от провайдера |

Как достигается:

- один проход aktc;
- content-hash;
- параллельная компиляция;
- общий кэш;
- `tcc` в debug (если доступен);
- XMake backend для native-пакетов.

---

## 19. Отличия от других систем

### 19.1. Cargo

| | Cargo | katek |
|---|---|---|
| Язык манифеста | TOML | TOML |
| Lock | есть | есть, включая native |
| Build | `rustc` | `aktc` + `cc` |
| C-библиотеки | через `build.rs`/`-sys`-крейты | first-class через провайдеры |
| Repro | через флаги | по умолчанию для akt-части |
| Registry | crates.io | hub.akt.dev |
| Скорость | средняя | цель — быстрее |

### 19.2. CMake

| | CMake | katek |
|---|---|---|
| Язык | свой | TOML |
| Модель | генерация build-систем | прямой вызов + опциональные backend'ы |
| C-пакеты | `find_package` | `find_package` + xrepo/vcpkg/Conan/pkg-config |
| Lock | нет | есть |
| Repro | вручную | для akt-части |

### 19.3. XMake

| | XMake | katek |
|---|---|---|
| Язык | Lua | TOML |
| C-пакеты | xrepo | xrepo + другие провайдеры |
| Гибкость | высокая | ниже, но детерминированнее |
| Backend | сам | может использовать XMake |
| Порог входа | Lua | TOML |

`katek` не «убивает» XMake. Он использует XMake там, где тот
силён (C-пакеты, toolchain'ы), и остаётся простым там, где нужен
детерминизм.

---

## 20. Итог

`katek` — это:

- простая сборка akt;
- first-class C-библиотеки;
- xrepo/vcpkg/Conan/CMake/pkg-config/system;
- content-hash и offline;
- lock для akt **и** C;
- XMake/CMake как опциональные backend'ы;
- reproducible для akt-части и честная фиксация native-части.

Философия:

> Есть конвенция — нет конфига.
> Есть lock — нет сюрпризов.
> Есть провайдер — нет ручного `-I`/`-L`.
> Есть content-hash — нет пересборки.
