# akt — C-интероп, C-библиотеки и провайдеры

Этот документ описывает, как akt работает с существующим C-кодом:
как объявлять зависимости, как генерируются привязки, как
линкуются библиотеки и как всё это остаётся воспроизводимым.

Нормативные детали манифеста — в [`04-katek.md`](04-katek.md),
синтаксис `cinclude`/`cfn`/`cstruct` — в
[`01-syntax.md`](01-syntax.md).

> **Текущая реализация (1.0):** `cinclude` разбирается компилятором на
> этапе сборки, объявления попадают в кэш проекта `.kache/cimport/`,
> сгенерированный C — в `.kache/build/`. Отдельный bindgen-шаг и файлы
> `.akti` не нужны: заголовок — единственный источник объявлений.
> Описанные ниже bindgen/`.akti`-сценарии — история дизайна.

---

## 1. Модель

akt не пытается заменить C-экосистему. Он использует её:

```text
katek.toml
   │
   ├── [dependencies]        akt-пакеты
   │
   └── [native.dependencies] C-библиотеки
            │
            ├── xrepo      ┐
            ├── vcpkg      │
            ├── conan      │  провайдеры
            ├── cmake      │  (резолвят include/lib/defines)
            ├── pkg-config │
            ├── system     │
            ├── path       │
            └── prebuilt   ┘
                     │
                     ▼
            .katek/native.json
                     │
                     ▼
              bindgen → .akti
                     │
                     ▼
         aktc → .gen.c + .gen.h
                     │
                     ▼
                  cc + linker
```

Правила:

1. **Только C ABI.** C++-интероп не поддерживается: нет name
   mangling, исключений, шаблонов. C++-библиотеку можно обернуть в
   C-фасад.
2. **Декларативность.** Зависимости описываются в `katek.toml`,
   а не в build-скриптах.
3. **Воспроизводимость.** Провайдер, версия, флаги и checksum
   попадают в `katek.lock`.
4. **Явная граница.** `cinclude`/`cfn`/`cstruct`/`cunion` — это
   единственный способ пересечь C-границу.
5. **Никаких неявных конверсий.** `string` не становится
   `cstring` без `.as_c_str()`.

---

## 2. Быстрый старт: zlib

### 2.1. Манифест

```toml
# katek.toml
[package]
name = "zlib-demo"
version = "1.0.0-beta.1"
edition = "2026"

[native]
providers = ["xrepo", "pkg-config", "system"]

[native.dependencies]
zlib = { provider = "xrepo", version = "1.3.1" }
```

### 2.2. Код

```akt
// src/main.akt
use std.c

cinclude <zlib.h>

fn main() int {
    // zlibVersion() → const char* → cstring
    string version = c.to_string(c.zlibVersion())
    println(f"zlib {version}")
    return 0
}
```

### 2.3. Сборка

```bash
katek build
katek run
```

Что происходит:

1. `katek` читает `[native.dependencies]`.
2. Выбирает `xrepo` (явно указан).
3. Если lock нет — `xrepo install zlib 1.3.1`.
4. Получает include/lib пути и флаги.
5. Пишет `.katek/native.json`.
6. Bindgen разбирает `zlib.h` и создаёт `.akti`.
7. `aktc` компилирует akt → C11.
8. `cc` компилирует C и линкует `-lz`.

### 2.4. Альтернатива без манифеста

```bash
katek c add zlib --provider xrepo --version 1.3.1
katek c sync
katek build
```

`katek c add` меняет `katek.toml` и `katek.lock`. Он не запускает
полную сборку.

---

## 3. Провайдеры

### 3.1. Общий интерфейс

Каждый провайдер обязан вернуть:

| Поле | Что |
|---|---|
| `include_dirs` | `-I` пути |
| `library_dirs` | `-L` пути |
| `libs` | имена `-l` |
| `link_files` | явные `.a`/`.so`/`.lib`/`.dll` |
| `defines` | `-D` для заголовков |
| `cflags` | дополнительные флаги компиляции C |
| `targets` | CMake-таргеты, если применимо |
| `version` | фактическая версия |
| `checksum` | хэш артефакта/исходников, если доступен |

`katek` нормализует это в `.katek/native.json` и в
`katek.lock`.

### 3.2. `xrepo`

Основной провайдер: коллекция C/C++-пакетов XMake.

```toml
[native.dependencies]
zlib = { provider = "xrepo", version = "1.3.1" }
sqlite3 = { provider = "xrepo", version = "3.45.0", static = true }
```

```bash
katek c add zlib --provider xrepo --version 1.3.1
```

Плюсы:

- широкая коллекция;
- умеет статические/динамические конфигурации;
- сам разрешает транзитивные зависимости;
- работает на Linux, macOS, Windows.

Минусы:

- версии пакетов отстают от upstream;
- собственные сборочные скрипты пакета могут быть
  недетерминированными.

Для полной воспроизводимости: `katek c vendor`.

### 3.3. `vcpkg`

```toml
[native.dependencies]
sdl2 = { provider = "vcpkg", version = "2.30.0", triplet = "x64-windows" }
```

```bash
katek c add sdl2 --provider vcpkg --version 2.30.0 \
  --triplet x64-windows
```

`katek`:

1. Проверяет `VCPKG_ROOT` или `--vcpkg-root`.
2. Вызывает `vcpkg install sdl2:x64-windows`.
3. Читает `vcpkg_installed/<triplet>/include` и `lib`.
4. Добавляет `-I`, `-L`, `-l`.
5. Для CMake-пакетов может использовать `find_package` из vcpkg
   toolchain-файла.

Triplet обязателен для кросс-компиляции.

### 3.4. `conan`

```toml
[native.dependencies]
fmt = { provider = "conan", version = "10.2.1", profile = "default" }
```

```bash
katek c add fmt --provider conan --version 10.2.1
```

`katek`:

1. Проверяет `conan` в `PATH`.
2. Генерирует `conanfile.txt`/`conanfile.py` во временном каталоге.
3. Вызывает `conan install ... --build=missing`.
4. Читает `conanbuildinfo`/`conan_toolchain.cmake`.
5. Извлекает include/lib/defines.

Conan 2 и Conan 1 различаются форматом; `katek` поддерживает
Conan 2 как основной, Conan 1 — через `--conan1`.

### 3.5. `cmake`

Провайдер для пакетов с `*Config.cmake` или `Find*.cmake`.

```toml
[native.dependencies]
sqlite3 = { provider = "cmake", package = "SQLite3", version = "3.45" }
```

```bash
katek c add sqlite3 --provider cmake --package SQLite3
```

`katek`:

1. Создаёт временный CMake-проект.
2. Вызывает `find_package(SQLite3 CONFIG REQUIRED)`.
3. Читает свойства target `SQLite::SQLite3`:
   `INTERFACE_INCLUDE_DIRECTORIES`, `IMPORTED_LOCATION`,
   `INTERFACE_LINK_LIBRARIES`, `INTERFACE_COMPILE_DEFINITIONS`.
4. Возвращает нормализованный набор флагов.

Если пакет не найден, ошибка содержит подсказки:

```text
error[E-C-FIND]: package SQLite3 not found
help: install it:
  apt install libsqlite3-dev
  vcpkg install sqlite3
  xrepo install sqlite3
help: or set CMAKE_PREFIX_PATH=/path/to/sqlite3
```

### 3.6. `pkg-config`

```toml
[native.dependencies]
curl = { provider = "pkg-config", package = "libcurl", version = ">=8.0" }
```

```bash
katek c add curl --provider pkg-config --package libcurl
```

`katek` вызывает:

```bash
pkg-config --cflags --libs libcurl
pkg-config --modversion libcurl
```

Учитывается `Requires.private` для статической линковки. На
Windows pkg-config обычно нет; используйте `vcpkg`/`cmake`/
`prebuilt`.

### 3.7. `system`

Для библиотек, которые уже есть в стандартных путях.

```toml
[native.dependencies]
m = { provider = "system" }
pthread = { provider = "system" }
```

```toml
[native]
link = ["m", "pthread"]
```

`system` не ищет include-пути: он предполагает, что заголовки
доступны компилятору. Это подходит для `libc`, `libm`,
`libpthread`, `dl`, `z` на типичных Unix-системах.

### 3.8. `path`

Локальный исходник с собственной системой сборки.

```toml
[native.dependencies]
mylib = { path = "vendor/mylib", build = "cmake" }
```

Допустимые `build`:

| Значение | Что делает `katek` |
|---|---|
| `cmake` | configure + build + install в `.katek/native/<name>/install` |
| `xmake` | `xmake f -o ...` + `xmake build` + install |
| `make` | `make` + `make install PREFIX=...` |
| `meson` | `meson setup` + `meson compile` + install |
| `none` | ничего не собирает; ждёт готовые include/lib |

Пример структуры:

```text
vendor/mylib/
├── CMakeLists.txt
├── include/
│   └── mylib.h
└── src/
    └── mylib.c
```

### 3.9. `prebuilt`

Готовый SDK без сборки.

```toml
[native.dependencies]
sdk = { path = "vendor/sdk", kind = "static" }
```

```text
vendor/sdk/
├── include/
│   └── sdk.h
└── lib/
    ├── libsdk.a       # Linux/macOS
    └── sdk.lib        # Windows
```

Для динамической версии:

```toml
sdk = { path = "vendor/sdk", kind = "shared" }
```

`katek` добавит `rpath`/`PATH` для динамической библиотеки.

---

## 4. Разрешение зависимостей

### 4.1. Алгоритм

```text
1. Прочитать [native.dependencies].
2. Для каждой зависимости:
   a. явный provider;
   b. иначе первый доступный из [native].providers;
   c. иначе ошибка E-C-NO-PROVIDER.
3. Если есть katek.lock и --locked:
   использовать точную версию и флаги из lock.
   Иначе — резолвить (только add/update/fetch).
4. Скачать/собрать пакет.
5. Нормализовать include/lib/defines/link/targets.
6. Записать .katek/native.json.
7. Bindgen → .akti.
8. aktc → C11.
9. cc + linker.
```

### 4.2. Приоритет

По умолчанию:

```text
xrepo → pkg-config → system
```

Переопределение:

```toml
[native]
providers = ["vcpkg", "cmake", "system"]
```

Явный `provider` всегда выигрывает.

### 4.3. Конфликты

- Две зависимости требуют разные версии одной библиотеки —
  ошибка `E-C-VERSION-CONFLICT`.
- Две зависимости дают разные include-пути с одинаковым
  заголовком — warning + порядок из lock.
- Статическая и динамическая версии одной библиотеки —
  ошибка, если не указан `static = true`.

### 4.4. Optional

```toml
[native.dependencies]
readline = { provider = "pkg-config", optional = true }
```

Если не найдено:

- warning;
- `#define AKT_HAVE_READLINE 0` не появляется автоматически;
- код обязан проверять наличие через feature/`cfg`-механизм
  (см. `04-katek.md`).

---

## 5. `katek.lock` для native-пакетов

```toml
[[package]]
name = "zlib"
kind = "native"
provider = "xrepo"
version = "1.3.1"
source = "xrepo:zlib/1.3.1"
checksum = "sha256:..."
features = []
static = false
link = ["z"]
include = ["/home/user/.xmake/packages/z/zlib/1.3.1/.../include"]
lib = ["/home/user/.xmake/packages/z/zlib/1.3.1/.../lib"]
defines = []
cflags = []
```

Правила:

- `version` и `checksum` обязательны.
- Абсолютные пути не являются частью идентичности: при переносе
  проекта провайдер пересчитывает их.
- `--locked` запрещает менять lock.
- `katek c vendor` переписывает `provider` на `path` и
  фиксирует локальные артефакты.

---

## 6. Bindgen

### 6.1. Что делает

`cinclude <header.h>` запускает bindgen:

1. Находит заголовок в include-путях.
2. Разбирает подмножество C11.
3. Генерирует `.akti` с `cfn`, `cstruct`, `cunion`, `enum`,
   `const`.
4. Кэширует результат по хэшу заголовка и флагов.
5. Делает имена доступными как `c.name`.

### 6.2. Поддерживаемое подмножество

| C | akt |
|---|---|
| `typedef` | `type` |
| `struct S` | `cstruct S` |
| `union U` | `cunion U` |
| `enum E` | `enum E` (unit-only) |
| `fn(...)` | `cfn ...` |
| `const T*` | `*const T` |
| `char*` | `*cchar` |
| `const char*` | `cstring` |
| `void*` | `*void` |
| `size_t` | `size` |
| `ptrdiff_t` | `isize` |
| `_Bool`/`bool` | `bool` |
| массивы в параметрах | `*T` |
| `...` | variadic `cfn` |

### 6.3. Не поддерживается

- макросы с вычисляемыми значениями (`#define X (a+b)`);
- `_Generic`;
- сложные `__attribute__`;
- вложенные анонимные структуры без имени;
- C++;
- `setjmp`/`longjmp` как механизм ошибок;
- битовые поля (пока).

Для них — ручные `cfn`/`cstruct`/`cunion` или `.akti`.

### 6.4. Ручной `.akti`

```akt
// bindings/zlib.akti
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

cfn deflateInit2_(*z_stream strm, i32 level, i32 method,
                  i32 windowBits, i32 memLevel, i32 strategy,
                  cstring version, i32 stream_size) i32
cfn deflate(*z_stream strm, i32 flush) i32
cfn deflateEnd(*z_stream strm) i32
cfn zlibVersion() cstring

clink "z"
```

Использование:

```akt
use zlib
```

`use zlib` импортирует объявления из `.akti`. Если заголовок
доступен, `cinclude <zlib.h>` предпочтительнее: bindgen
поддерживает его в актуальном состоянии.

### 6.5. Имена и конфликты

- `c.name` — всегда доступно.
- `name` — доступно, если нет конфликта с akt-именем.
- При конфликте используйте `c.name`.
- Bindgen не переименовывает C-символы: ABI должен совпадать.

### 6.6. Кэш

```text
~/.cache/katek/bindings/<sha256>.akti
```

Ключ:

```text
sha256(
  содержимое заголовка +
  include-пути +
  defines +
  версия bindgen
)
```

Кэш общий между проектами. `katek c sync --force` сбрасывает
его для текущего проекта.

---

## 7. Линковка

### 7.1. Порядок

`katek` линкует в порядке:

1. объекты akt-модулей (детерминированная сортировка);
2. `prebuilt`/`path` библиотеки;
3. провайдерные библиотеки;
4. системные библиотеки из `[native].link`;
5. `clink` из исходников.

Порядок стабилен и попадает в lock/native.json.

### 7.2. Статическая и динамическая линковка

```toml
zlib = { provider = "xrepo", version = "1.3.1", static = true }
```

- `static = true` — искать `.a`/`.lib`.
- `static = false` — предпочитать `.so`/`.dylib`/`.dll`.
- Если доступно только одно — используется оно, с warning.

### 7.3. `clink`

```akt
clink "m"                       // -lm
clink "pthread"                 // -lpthread
clink path "vendor/libfoo.a"    // явный файл
clink framework "Cocoa"         // macOS framework
```

`clink` нужен для ручных случаев. Предпочтительный способ —
`[native]`/`[native.dependencies]`.

### 7.4. Windows

- `.lib` — import library или static library.
- `.dll` копируется рядом с `.exe` или добавляется в `PATH`.
- Для MSVC `katek` передаёт `/I`, `/LIBPATH:`, `/DEFAULTLIB:`.
- `__declspec(dllimport)` должен быть в заголовке; bindgen его
  игнорирует.

### 7.5. macOS

- `.dylib` + `@rpath`.
- `clink framework "Cocoa"` → `-framework Cocoa`.
- Учитывается `MACOSX_DEPLOYMENT_TARGET`.

---

## 8. Вендоринг и воспроизводимость

### 8.1. Зачем

Провайдеры могут:

- скачивать разные версии;
- встраивать timestamps;
- зависеть от системных путей;
- менять флаги между запусками.

`katek c vendor` фиксирует всё локально.

### 8.2. Команда

```bash
katek c vendor
```

Результат:

```text
vendor/
├── zlib/
│   ├── include/
│   ├── lib/
│   └── LICENSE
├── sqlite3/
│   ├── include/
│   ├── lib/
│   └── LICENSE
└── vendor.lock
```

`katek.lock` переписывается:

```toml
[[package]]
name = "zlib"
kind = "native"
provider = "path"
path = "vendor/zlib"
checksum = "sha256:..."
```

После этого:

```bash
katek build --locked --offline
```

не требует сети и провайдера.

### 8.3. Лицензии

`katek c vendor` копирует `LICENSE`/`COPYING`, если файл есть.
Проверка лицензий — `katek audit`.

### 8.4. Ограничение

Полная побайтовая воспроизводимость сторонних C-пакетов **не
гарантируется**. Гарантируется:

- фиксация версии и checksum;
- детерминизм akt-части;
- возможность vendoring;
- предупреждение при недетерминированной native-сборке.

---

## 9. XMake

### 9.1. Роль

XMake используется в трёх ролях:

1. сборка самого toolchain (`aktc`, `katek`);
2. backend `katek build --backend xmake`;
3. провайдер C-пакетов (`xrepo`).

### 9.2. Сборка toolchain

```lua
-- xmake.lua
set_project("akt")
set_version("1.0.0-beta.1")
set_languages("c11")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")

target("aktc")
    set_kind("binary")
    add_files("aktc/src/**.c")
    add_includedirs("aktc/src", "aktc/include")
    add_defines("AKT_VERSION=\"$(version)\"")
    add_cxflags("-Wall", "-Wextra", "-Wpedantic")
    set_targetdir("build/bin")

target("katek")
    set_kind("binary")
    add_files("katek/src/**.c")
    add_includedirs("katek/src", "katek/include")
    set_targetdir("build/bin")
```

```bash
xmake build
xmake build -m release
xmake install
```

### 9.3. Backend

```bash
katek build --backend xmake
```

`katek` генерирует `.katek/build/xmake.lua`. Если зависимости
пришли из xrepo:

```lua
add_requires("zlib 1.3.1", { configs = { shared = false } })
add_requires("sqlite3 3.45.0")

target("myapp")
    set_kind("binary")
    add_files(".katek/gen/**.gen.c")
    add_packages("zlib", "sqlite3")
    add_includedirs(".katek/gen")
    set_targetdir("target/release")
```

Если зависимости пришли из `pkg-config`/`cmake`/`prebuilt`,
`katek` не вызывает `add_requires`, а передаёт явные пути:

```lua
target("myapp")
    set_kind("binary")
    add_files(".katek/gen/**.gen.c")
    add_includedirs(".katek/gen", "/usr/include/curl")
    add_linkdirs("/usr/lib/x86_64-linux-gnu")
    add_links("curl")
```

### 9.4. Кросс-компиляция

```bash
katek build --backend xmake --target aarch64-linux-gnu
```

`katek` генерирует toolchain-блок:

```lua
set_toolchains("gcc")
set_toolset("cc", "aarch64-linux-gnu-gcc")
set_toolset("ld", "aarch64-linux-gnu-ld")
```

### 9.5. Ограничения

- XMake не обязателен для `internal`.
- Генерируемый `xmake.lua` не содержит абсолютных путей.
- Порядок целей детерминирован.
- `katek build --reproducible --backend xmake` проверяет итог, но
  может предупредить о недетерминизме native-пакетов.

---

## 10. CMake

### 10.1. Роль

1. провайдер C-пакетов (`provider = "cmake"`);
2. backend `katek build --backend cmake`;
3. экспорт akt-библиотеки в CMake-проект.

### 10.2. Провайдер

```toml
[native.dependencies]
sqlite3 = { provider = "cmake", package = "SQLite3", version = "3.45" }
```

`katek` создаёт временный проект:

```cmake
cmake_minimum_required(VERSION 3.20)
project(akt_probe C)

find_package(SQLite3 CONFIG REQUIRED)

get_target_property(INC SQLite::SQLite3 INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(LOC SQLite::SQLite3 IMPORTED_LOCATION)
get_target_property(LIBS SQLite::SQLite3 INTERFACE_LINK_LIBRARIES)
```

и читает результат через `--trace-expand`/`file(WRITE ...)`.

### 10.3. Backend

```bash
katek build --backend cmake
```

Генерирует:

```cmake
cmake_minimum_required(VERSION 3.20)
project(myapp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(myapp
    .katek/gen/main.gen.c
    .katek/gen/math.gen.c
)

target_include_directories(myapp PRIVATE .katek/gen)
target_link_libraries(myapp PRIVATE SQLite::SQLite3 ZLIB::ZLIB)
```

### 10.4. Экспорт akt-библиотеки

```cmake
cmake_minimum_required(VERSION 3.20)
project(consumer C)

find_package(akt REQUIRED)

akt_add_library(mylib
    SOURCES src/lib.akt
    DEPENDS ZLIB::ZLIB
)

add_executable(app main.c)
target_link_libraries(app PRIVATE mylib)
```

`akt_add_library`:

- вызывает `aktc` для генерации C;
- добавляет include-директории;
- линкует зависимости;
- экспортирует target `mylib` для C-проекта.

### 10.5. Toolchain

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
  -DCMAKE_PREFIX_PATH=/opt/sysroot/usr
```

`katek` уважает `CMAKE_TOOLCHAIN_FILE`, `CMAKE_PREFIX_PATH`,
`CMAKE_BUILD_TYPE`.

---

## 11. Примеры

### 11.1. SQLite

```toml
[native.dependencies]
sqlite3 = { provider = "xrepo", version = "3.45.0" }
```

```akt
use std.c

cinclude <sqlite3.h>

fn main() int {
    string v = c.to_string(c.sqlite3_libversion())
    println(f"sqlite {v}")
    return 0
}
```

### 11.2. OpenSSL через pkg-config

```toml
[native.dependencies]
openssl = { provider = "pkg-config", package = "openssl", version = ">=3.0" }
```

```akt
use std.c

cinclude <openssl/crypto.h>

fn main() int {
    string v = c.to_string(c.OpenSSL_version(0))
    println(f"openssl {v}")
    return 0
}
```

### 11.3. SDL2 через vcpkg

```toml
[native.dependencies]
sdl2 = { provider = "vcpkg", version = "2.30.0", triplet = "x64-windows" }
```

```akt
cinclude <SDL2/SDL.h>

fn main() int {
    if (c.SDL_Init(0) != 0) {
        println("SDL init failed")
        return 1
    }
    defer c.SDL_Quit()
    println("SDL initialized")
    return 0
}
```

### 11.4. Vendored CMake-проект

```toml
[native.dependencies]
mylib = { path = "vendor/mylib", build = "cmake" }
```

```bash
katek build
# конфигурирует vendor/mylib в .katek/native/mylib/build
# устанавливает в .katek/native/mylib/install
```

### 11.5. Prebuilt SDK

```toml
[native.dependencies]
sdk = { path = "vendor/sdk", kind = "static" }
```

```akt
cinclude "sdk.h"

fn main() int {
    if (c.sdk_init() != 0) {
        return 1
    }
    defer c.sdk_shutdown()
    string v = c.to_string(c.sdk_version())
    println(f"sdk {v}")
    return 0
}
```

`cinclude "sdk.h"` ищет заголовок в `vendor/sdk/include`,
потому что `prebuilt` добавляет этот путь.

### 11.6. Полностью offline

```bash
katek c vendor
katek build --locked --offline
```

---

## 12. ABI и владение

### 12.1. Правила

1. **`cstruct` — точный layout.** Порядок полей и выравнивание
   должны совпадать с C.
2. **`enum` — unit-only.** Для C-enum с payload используйте
   `cstruct` + константы.
3. **`cfn` — точная сигнатура.** Включая `const`, `volatile`,
   variadic.
4. **`string` ≠ `cstring`.** Для C нужен `.as_c_str()`.
5. **Владение `*T`** документируется в комментарии к `cfn`:
   кто выделяет, кто освобождает.
6. **C не соблюдает проверки akt.** Компилятор проверяет типы, но
   не валидность указателей из C.
7. **`null`** допустим только для `*T`/`cstring`; проверяйте до
   разыменования.

### 12.2. Кто освобождает

| Источник | Кто освобождает |
|---|---|
| `malloc`/`calloc` в akt | `std.mem.free` / `c.free` |
| Функция C вернула `*T` | По контракту функции (см. документацию) |
| `cstring` из `.as_c_str()` | Регион, в котором создан |
| `cstring` из C | Обычно C-библиотека; не вызывайте `free` без контракта |
| `manual` | `defer`/`free`/`close` |

### 12.3. Callback

```akt
cfn qsort(*void base, size n, size size,
          fn(*const void, *const void) i32 cmp) void

fn cmp_int(*const void a, *const void b) i32 {
    i32 x = *(a as *const i32)
    i32 y = *(b as *const i32)
    return x - y
}

fn main() int {
    [i32; 5] values = [5, 3, 1, 4, 2]
    c.qsort(&values[0] as *void, 5, 4, &cmp_int)
    return 0
}
```

Замыкание с захватами в C передавать нельзя. Для контекста
используйте `*void userdata`.

---

## 13. Диагностика

| Код | Причина | Что делать |
|---|---|---|
| `E-C-NO-PROVIDER` | нет доступного провайдера | установите xrepo/vcpkg/pkg-config или укажите `system` |
| `E-C-NOT-FOUND` | пакет не найден | `katek c add ... --provider ...` или установите вручную |
| `E-C-VERSION` | версия не совпала | `--version`, `--precise` или обновите lock |
| `E-C-ABI` | несовпадение layout | проверьте `cstruct`, выравнивание, `#pragma pack` |
| `E-C-LINK` | undefined symbol | проверьте порядок `clink`/`link`, static/shared |
| `E-C-HEADER` | заголовок не найден | добавьте `include` в `[native]` |
| `E-C-BINDGEN` | bindgen не разобрал объявление | ручной `cfn`/`cstruct`/`.akti` |
| `E-C-CSTRING` | `string` передан как `cstring` | `s.as_c_str()` |
| `E-C-THREAD` | C-объект передан между потоками | используйте `shared`/`Mutex` |

---

## 14. Безопасность

1. **Не выполняйте произвольные build-скрипты.** Только
   декларативные провайдеры.
2. **Фиксируйте checksum.** `katek.lock` обязателен в CI.
3. **Проверяйте лицензии.** `katek audit`.
4. **Не доверяйте заголовкам из сети.** Vendored-заголовки
   ревьюятся как код.
5. **Изолируйте C-границу.** Не передавайте `auto`/`arena` в C,
   если C может сохранить указатель.
6. **Не отключайте проверки.** `panic = "abort"` не означает
   «без проверок»: overflow/div-by-zero проверяются всегда, если
   не используется явный `std.math.wrapping_*`.

---

## 15. Чек-лист

- [ ] Зависимость объявлена в `[native.dependencies]`, а не в
      скрипте.
- [ ] Указан `provider` или настроен порядок в `[native]`.
- [ ] Версия зафиксирована в `katek.lock`.
- [ ] `katek build --locked --offline` проходит после vendoring.
- [ ] `cinclude`/`cfn`/`cstruct` соответствуют ABI.
- [ ] `string` → `cstring` только через `.as_c_str()`.
- [ ] Владение `*T` задокументировано.
- [ ] C-callback без замыканий с захватами.
- [ ] `katek c doctor` не показывает ошибок.
- [ ] `katek build --backend xmake` и `--backend cmake` проходят
      (если используются).
- [ ] Лицензии скопированы в `vendor/`.
