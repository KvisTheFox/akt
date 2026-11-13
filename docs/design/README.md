# akt — системный язык с детерминированной памятью

**akt** — компилируемый системный язык: C-подобный синтаксис, явная
модель памяти без сборщика мусора, компиляция в C11.
**aktc** — компилятор akt, написанный на C11.
**katek** — система сборки, управления зависимостями и пакетами.

> **Статус:** это спецификация (design-first), а не описание готовой
> реализации. Всё, что здесь написано, — проект языка и инструментов.
> Если реализация расходится со спецификацией, правится реализация
> или спецификация, но не «как получилось».

---

## 1. Что такое akt

akt — язык для задач, где нужны одновременно:

- предсказуемая память без пауз и без GC;
- скорость и размер, сравнимые с C;
- читаемый синтаксис без `->`, `::` (кроме вариантов enum и
  ассоциированных функций), `'a` и препроцессора;
- простая интеграция с существующим C-кодом и C-библиотеками;
- быстрая сборка и воспроизводимые артефакты.

Ключевая идея: **фронтенд на C генерирует качественный C-код**, а
оптимизацию, ABI и кодогенерацию для платформы делает системный
C-компилятор (GCC, Clang, MSVC, TCC). Это резко уменьшает размер и
сложность компилятора и даёт доступ ко всей C-экосистеме.

```akt
// src/main.akt
fn main() int {
    println("Hello, World!")
    return 0
}
```

```bash
katek run
# Hello, World!
```

---

## 2. Ключевые свойства

| Свойство | Как реализовано |
|---|---|
| Память | Регионы, `arena`, `shared` + `weak`, `manual`, `defer` |
| GC | Отсутствует. Никаких исключений и «режимов с GC» |
| Компиляция | `akt` → C11 → системный C-компилятор → бинарник |
| Компилятор | C11, без LLVM/MLIR, без внешних библиотек кроме libc |
| Синтаксис | C-подобный, без `->`, без `::` (кроме вариантов enum и ассоциированных функций), без `#define` |
| Модули | Файл = модуль, `use a.b`, без заголовочных файлов |
| C-интероп | `cinclude`, `cfn`, `cstruct`, `cunion`, `clink` |
| C-библиотеки | Первоклассные зависимости через xrepo, vcpkg, Conan, CMake, pkg-config |
| Сборка | `katek`: декларативный TOML, content-hash, offline, reproducible |
| XMake | Официальный альтернативный backend и способ сборки самого toolchain |
| Потоки | `shared` + `std.thread` / `std.sync` / `std.async` |

Подробности: [`00-philosophy.md`](00-philosophy.md).

---

## 3. Быстрый старт

### 3.1. Установка

```bash
# Linux / macOS
curl -fsSL https://katek.akt.dev/install.sh | sh

# Windows (PowerShell)
winget install akt.katek
# или, если winget недоступен:
# irm https://katek.akt.dev/install.ps1 | iex

# Или из исходников (XMake)
git clone https://github.com/akt-lang/akt
cd akt
xmake build
xmake install
```

> Установка через `curl | sh` удобна, но для production-сред
> проверяйте подпись/контрольную сумму скрипта. Инструкции лежат
> рядом с релизом.

### 3.2. Новый проект

```bash
katek new hello
cd hello
katek run
```

Структура проекта:

```text
hello/
├── katek.toml        # манифест (можно не создавать — работает конвенция)
├── katek.lock        # lock-файл (коммитится в git)
├── src/
│   └── main.akt      # точка входа
├── tests/
│   └── main_test.akt
├── examples/
│   └── basic.akt
└── target/           # артефакты сборки (в .gitignore)
```

### 3.3. Подключение C-библиотеки

```toml
# katek.toml
[package]
name = "hello-zlib"
version = "1.0.0-beta.1"
edition = "2026"

[native]
providers = ["xrepo", "pkg-config", "system"]

[native.dependencies]
zlib = { provider = "xrepo", version = "1.3.1" }
```

```akt
// src/main.akt
use std.c

cinclude <zlib.h>
clink "z"

fn main() int {
    // zlibVersion() возвращает const char*, то есть cstring.
    string version = c.to_string(c.zlibVersion())
    println(f"zlib {version}")
    return 0
}
```

`katek` сам скачает/соберёт `zlib` через xrepo, добавит include-пути,
сгенерирует привязки к `zlib.h` и слинкует библиотеку.
Подробности: [`12-c-interop.md`](12-c-interop.md).

---

## 4. Расширения файлов

| Расширение | Назначение |
|---|---|
| `.akt` | Исходный файл akt (модуль, скрипт, тест) |
| `.akti` | Устаревший формат привязок; текущая реализация импортирует заголовки сама |
| `.aktir` | Дамп IR для отладки компилятора (не вход для сборки) |
| `.gen.c` | Сгенерированный C-код (для отладки, в git не коммитится) |
| `.gen.h` | Сгенерированный C-заголовок модуля для межмодульной проверки типов |
| `katek.toml` | Манифест проекта/пакета/workspace |
| `katek.lock` | Зафиксированные версии akt- и C-зависимостей |

Устаревшее расширение `.cdr` **не используется**. Если оно встретилось
в старых материалах — это ошибка, читайте `.akt`.

---

## 5. Документация

| Файл | Содержание |
|---|---|
| [`00-philosophy.md`](00-philosophy.md) | Зачем язык, принципы, границы, честные компромиссы |
| [`01-syntax.md`](01-syntax.md) | Полный синтаксис и семантика ядра |
| [`02-memory.md`](02-memory.md) | Регионы, arena, shared/weak, manual, defer, проверки |
| [`03-compiler.md`](03-compiler.md) | Устройство `aktc`, стадии, кодогенерация, диагностика |
| [`04-katek.md`](04-katek.md) | Сборка, манифест, lock, кэш, workspace, XMake/CMake backend |
| [`05-stdlib.md`](05-stdlib.md) | Стандартная библиотека: уровни, каталог, правила |
| [`06-examples-basic.md`](06-examples-basic.md) | Базовые примеры |
| [`07-examples-full.md`](07-examples-full.md) | Полный тур по ядру языка |
| [`08-examples-stdlib.md`](08-examples-stdlib.md) | Примеры модулей std |
| [`09-negative.md`](09-negative.md) | Жёсткие запреты и чек-лист ревью |
| [`10-prompts.md`](10-prompts.md) | Промпты для ИИ-агента, реализующего akt |
| [`11-roadmap.md`](11-roadmap.md) | Поэтапный план реализации и критерии готовности |
| [`12-c-interop.md`](12-c-interop.md) | C-библиотеки: провайдеры, привязки, линковка, воспроизводимость |

---

## 6. Сборка самого toolchain

Основной поддерживаемый способ — **XMake**. Он же используется для
управления C-зависимостями самого компилятора.

```lua
-- xmake.lua (корень репозитория akt)
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
    add_syslinks("m")
    set_targetdir("build/bin")

target("katek")
    set_kind("binary")
    add_files("katek/src/**.c")
    add_includedirs("katek/src", "katek/include")
    add_syslinks("m")
    set_targetdir("build/bin")
```

```bash
xmake build            # debug
xmake build -m release # release
xmake install          # установка
xmake run aktc -- --help
```

Резервные способы (для окружений без XMake):

```bash
# Make
make -C aktc
make -C katek

# CMake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Сборка через XMake не делает XMake обязательным для пользователей
`katek`: это инструмент сборки toolchain и один из backend'ов.

---

## 7. Совместимость и версии

- Версия языка задаётся `edition` в `katek.toml`.
- `aktc` и `katek` версионируются по SemVer.
- `katek.lock` фиксирует версии akt-пакетов **и** C-зависимостей,
  включая провайдера, версию, флаги и контрольные суммы.
- Изменения, ломающие совместимость, возможны только в новой
  `edition`.

---

## 8. Лицензия

Двойная лицензия: **MIT OR Apache-2.0**, как у Rust. Пакеты
экосистемы могут выбирать любую из совместимых лицензий.

---

## 9. Правила для документов

Чтобы документация не расходилась с кодом:

1. Любой пример обязан компилироваться текущим `aktc` (или быть
   помечен как псевдокод).
2. Любое правило синтаксиса существует ровно в одном месте —
   `01-syntax.md`; остальные файлы ссылаются на него.
3. Любой запрет существует ровно в одном месте — `09-negative.md`.
4. Термины: **регион**, **арена**, **shared**, **weak**, **manual**,
   **C-граница**. Не использовать «borrow checker», «lifetime»,
   «GC» в описании akt, кроме явных сравнений.
5. Расширение исходников — только `.akt`.
