# C-библиотеки и зависимости

akt задуман как язык, который не прячется от C, а дружит с ним.
Цель: `katek c add zlib` должен скачать/найти zlib, импортировать
объявления из заголовков и слинковать проект.

## 1. Текущее состояние

Сейчас реализовано:

- `cinclude <header>` подключает заголовок: `aktc` разбирает его сам;
- C-объявления (struct/union/typedef/enum/#define/прототипы) импортируются
  автоматически — отдельный bindgen не нужен;
- сгенерированный C, кэш заголовков и временные файлы лежат в `.kache/`;
- `cfn name(params) ret` / `cstruct` / `cunion` — ручные объявления,
  если заголовка нет;
- вызовы `c.func(args)` работают как вызовы C-функций;
- строковые аргументы передаются как `const char*` (`.data`);
- `c.to_string(ptr)` / `c.cstring(ptr)` дают `string`;
- `katek c add` дописывает `[native.dependencies]` в `katek.toml`;
- `katek.toml` хранит провайдера, версию, пакет и флаги;
- `katek` читает `[native]` include/lib/link/defines и передаёт их
  в `aktc`;
- `system` и `pkg-config` зависимости добавляют флаги;
- `katek c install` ставит xrepo/vcpkg/Conan, проверяет pkg-config и
  CMake `find_package`;
- `katek export xmake|cmake` генерирует нативную сборку;
- `katek c sync` создаёт `.katek/native.json`.
- XMake/CMake могут собирать сам toolchain;
- рантайм и `aktc` написаны на C11.

Пока **не** реализовано:

- автоматическое разрешение провайдеров (path/prebuilt и checksum);
- передача include/lib/defines из `katek.toml` в C-компилятор;
- lock-файл с checksum;
- проверка C-сигнатур и выравнивания (`_Static_assert`).

Это следующий крупный этап. Ниже — целевой дизайн.

## 1a. Нативная поддержка C

C-заголовки подключаются напрямую, без отдельного bindgen-шага:

```akt
cinclude <string.h>

fn main() int {
    int n = strlen("hello") as int
    return 0
}
```

`aktc` сам находит и разбирает заголовок, регистрирует:

- `typedef struct/union` — как `cstruct`/`cunion` (opaque-типы тоже);
- `typedef`-алиасы — как `type Name = ...`;
- прототипы функций — как `cfn`;
- числовые `#define` и `enum` — как `const i64`.

Поэтому `strlen`, `div`, `glfwCreateWindow` и `GLFW_KEY_W` доступны
сразу. Сгенерированные объявления и сам C-код лежат в кэше проекта
`.kache/` — в исходниках не появляется ни `.c`, ни `.akti`.

Для стандартной библиотеки C (`stdio.h`, `string.h`, `stdlib.h`,
`math.h`) объявления встроены в компилятор. Остальные заголовки
берутся из include-путей манифеста (`[native] include` / провайдеры) и
из каталога исходника.

`cfn`/`cstruct`/`cunion` остаются как ручной escape hatch для случаев,
когда заголовка нет вообще.

## 1a2. Библиотеки «заголовок + исходник»: `cimport`

```akt
cimport "libs/sokol/sokol_app.h"
```

`aktc` находит заголовок, импортирует объявления и, если рядом лежит
`sokol_app.c`, компилирует его вместе с проектом. Каталог `vendor/` не
нужен: положите файлы библиотеки в любое место проекта (или скачайте
через `katek.toml`) и подключите одной строкой.

## 1a3. Зависимости из xrepo

```toml
[native]
link = ["glfw3", "opengl32", "gdi32", "user32", "shell32"]

[native.dependencies]
glfw = { provider = "xrepo", version = "3.4" }
```

`katek build` находит установленный пакет в
`%LOCALAPPDATA%\.xmake\packages` (или `~/.xmake/packages`) и сам
добавляет `-I`/`-L`. `link` перечисляет имена библиотек.

## 1b. Алиасы типов

```akt
type Count = i64
type Handle = *void
type Name = cstring
```

Алиас — другое имя типа, а не новый тип; он подставляется на этапе
разбора, поэтому работает в полях, параметрах и `auto`.

## 1c. Проверка ABI

Для вручную объявленных `cstruct`, которые уже есть в подключённом
заголовке, компилятор считает размер по правилам C и вставляет:

```c
_Static_assert(sizeof(div_t) == 8, "akt ABI: div_t size mismatch");
```

Несовпадение layout (packing, `#pragma`, неверный тип поля) ловится
на этапе компиляции C, а не в рантайме. Для импортированных из
заголовка структур assert не нужен — layout один и тот же.

## 2. Манифест

```toml
[package]
name = "hello-zlib"
version = "1.0.0-beta.1"
edition = "2026"

[native]
providers = ["xrepo", "pkg-config", "system"]
include = ["vendor/include"]
lib = ["vendor/lib"]
defines = ["MYAPP_VERSION=\"1.0.0\""]
link = ["m"]

[native.dependencies]
zlib = { provider = "xrepo", version = "1.3.1" }
sqlite3 = { provider = "cmake", package = "SQLite3", version = "3.45" }
curl = { provider = "pkg-config", package = "libcurl", version = ">=8.0" }
sdl2 = { provider = "vcpkg", version = "2.30.0", features = ["x11"] }
imgui = { provider = "xrepo", version = "1.90" }
mylib = { path = "vendor/mylib", build = "cmake" }
sdk = { path = "vendor/sdk", kind = "static" }
```

## 3. Провайдеры

| Провайдер | Что делает |
|---|---|
| `xrepo` | Пакеты XMake (`xrepo install`) |
| `vcpkg` | Пакеты vcpkg |
| `conan` | Пакеты Conan |
| `cmake` | `find_package` через CMake-пробу |
| `pkg-config` | `pkg-config --cflags --libs` |
| `system` | Стандартные пути (`-lm`, `-lz`) |
| `path` | Локальный исходник + его build system |
| `prebuilt` | Готовые `.a`/`.so`/`.dll` + include |

Порядок по умолчанию: `xrepo → pkg-config → system`.

## 4. Как это должно работать

```text
1. Прочитать [native.dependencies].
2. Для каждой зависимости выбрать провайдера.
3. Проверить katek.lock (если --locked).
4. Скачать/собрать пакет.
5. Получить include/lib/defines/link.
6. Записать .katek/native.json и .kache/native.json.
7. aktc читает cinclude, разбирает заголовки в .kache/cimport.
8. aktc компилирует с -I/-D, C-код кладёт в .kache/build.
9. C-компилятор линкует с -L/-l.
```

## 5. Ручной обходной путь сейчас

Пока провайдеры не готовы, можно:

1. Собрать библиотеку вручную.
2. Добавить `include`/`lib`/`link` в `[native]`.
3. Сгенерировать C и дописать `#include`/линковку.
4. Или линковать сгенерированный C вручную:

```bash
aktc build src/main.akt --keep-c
clang -std=c11 -I runtime/include src/main.gen.c runtime/src/akt_runtime.c \
    -I /path/to/zlib/include -L /path/to/zlib/lib -lz -o app
```

## 6. XMake и CMake репозитории

`katek export xmake` и `katek export cmake` создают нативные файлы
сборки для сгенерированного C. Это позволяет подключить akt-проект к
XMake/CMake и линковать C-библиотеки штатными средствами.

Сейчас сам toolchain собирается XMake и CMake; автоматическое
разрешение чужих xrepo/vcpkg/Conan-репозиториев — следующий этап.

## 7. Безопасность

- Никаких build-hooks и плагинов, исполняющих произвольный код.
- Только декларативные провайдеры.
- `katek.lock` фиксирует версии и checksum.
- `--locked` запрещает менять lock.
- Сетевые операции — только `fetch`/`update`/`add`.

## 8. Проверки

- [x] `katek c add zlib --provider xrepo` обновляет манифест.
- [x] `katek c sync` создаёт `.katek/native.json`.
- [x] `cinclude` импортирует объявления без bindgen-шага.
- [x] `type`-алиасы и `_Static_assert` для ABI.
- [x] `aktc build` передаёт `-I/-D` из манифеста.
- [x] Кодогенерация и кэш заголовков в `.kache/`.
- [ ] `--locked` проверяет checksum.
- [ ] Работает на Windows/Linux/macOS.
