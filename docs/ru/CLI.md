# CLI: `aktc` и `katek`

## `aktc`

```text
aktc build <file.akt> [-o output] [--release] [--keep-c] [--cc compiler]
                      [--shared] [--target triple] [--sysroot dir]
aktc run <file.akt> [--release] [--cc compiler]
aktc check <file.akt>
aktc --version
```

### `aktc build`

Компилирует `.akt` в C и вызывает C-компилятор.

```bash
aktc build src/main.akt
aktc build src/main.akt -o target/app.exe
aktc build src/main.akt --release
aktc build src/main.akt --keep-c
aktc build src/main.akt -I vendor/include -L vendor/lib -lz
aktc build src/main.akt -DAPP_VERSION=1
```

- По умолчанию C-файл называется `<name>.gen.c` и удаляется после
  успешной сборки.
- `--keep-c` оставляет его для отладки.
- `--cc clang` / `--cc gcc` выбирает C-компилятор.
- Переменная `AKT_CC` задаёт компилятор по умолчанию.
- Переменная `AKT_ROOT` задаёт корень toolchain (где лежит `runtime/`).

### `aktc run`

Собирает и сразу запускает.

```bash
aktc run examples/hello.akt
```

### `aktc check`

Только лексер, парсер и checker. C-компилятор не вызывается.

```bash
aktc check src/main.akt
# src/main.akt: ok
```

### Коды возврата

| Код | Значение |
|---|---|
| 0 | успех |
| 1 | ошибка компиляции/сборки/программы |
| 2 | ошибка использования CLI |

## `katek`

```text
katek new <name> [--bare] [--lib]
katek build [--release] [--shared] [--target android-*|triple] [-p member] [--locked]
katek run [file.akt] [--release]
katek check
katek test
katek clean
katek doctor
katek lock
katek export xmake|cmake
katek c add <name> [--provider p] [--version v] [--package pkg]
katek --version
```

### `katek new`

```bash
katek new hello
cd hello
katek run
```

Создаёт:

```text
hello/
├── katek.toml
├── src/main.akt
└── tests/
```

`--bare` не создаёт `katek.toml` (работает конвенция `src/main.akt`).

### `katek build`

Ищет корень проекта вверх по дереву (`katek.toml` или `src/main.akt`),
собирает `src/main.akt` в `target/app.exe` (Windows) или `target/app`.
С `--shared` / `katek new --lib` — `target/app.dll` на Windows или
`target/libapp.so` на Unix и Android.

### `katek run`

То же плюс запуск.

### `katek test`

Ищет `tests/*.akt`, собирает и запускает каждый файл. Если файл
завершается с кодом 0 — тест пройден.

### Кэш

Все артефакты сборки — сгенерированный C, кэш импортированных
заголовков, пробники и исполняемый файл по умолчанию — лежат в
`.kache/` проекта. В `src/` не появляется ничего лишнего.

### `katek c install [--dry-run]`

Устанавливает `xrepo`/`vcpkg`/`Conan`-зависимости и проверяет
`pkg-config`-пакеты из `[native.dependencies]`. `system` — no-op;
`cmake` проверяется через `cmake --find-package`.

### `katek c sync`

Создаёт `.katek/native.json` из `[native.dependencies]`. Сейчас это
декларативный снимок; полное скачивание/сборка через
xrepo/vcpkg/Conan/CMake/pkg-config — следующий этап.

### `katek c add`

Дописывает native-зависимость в `katek.toml`:

```bash
katek c add zlib --provider xrepo --version 1.3.1
katek c add sqlite3 --provider cmake --package SQLite3
katek c add curl --provider pkg-config --package libcurl
```

Сейчас команда только обновляет манифест. Автоматическое скачивание и
линковка — следующий этап (см. [`C_LIBRARIES.md`](C_LIBRARIES.md)).

### `katek clean`

Удаляет `target/`.

### `katek lock`

Создаёт `katek.lock` с версией и именем пакета. `katek build --locked`
требует наличия lock-файла.

### `katek doctor`

Проверяет `clang`, `aktc`, `runtime` и печатает корень проекта.

### `katek run file.akt`

Если передан файл, сразу вызывает `aktc run file.akt` без проекта.

### `katek build --target android-*`

Собирает shared library через Android NDK:

```bash
katek build --target android-arm64 --release
# target/libapp.so
```

Подробности — в [`ANDROID.md`](ANDROID.md).

### `katek export xmake|cmake`

Генерирует `xmake.lua` или `CMakeLists.txt`, которые собирают
сгенерированный `src/main.gen.c` и рантайм. Перед экспортом
автоматически вызывает `aktc build --keep-c`.

```bash
katek export xmake
xmake f -c --toolchain=clang-cl -m release
xmake build

katek export cmake
cmake -S . -B build
cmake --build build
```

### `katek fmt` / `lint`

Пока не реализованы; появятся после стабилизации AST-форматтера.

## Установщик

```powershell
bin\akt-setup.exe --dry-run
bin\akt-setup.exe
```

Копирует toolchain в `%LOCALAPPDATA%\akt`, добавляет `bin` в PATH,
проверяет Clang и пытается установить LLVM через winget.

## Переменные окружения

| Переменная | Значение |
|---|---|
| `AKT_ROOT` | Корень toolchain с `runtime/` и `bin/` |
| `AKT_CC` | C-компилятор по умолчанию |
| `AKT_DEBUG` | Печатать команды `katek` |
| `AKT_DEBUG_PARSE` | Печатать разбор инструкций (отладка компилятора) |

## Пример полного цикла

```bash
xmake build
export AKT_ROOT="$PWD"        # Linux/macOS
# $env:AKT_ROOT = "C:\path\to\akt"   # PowerShell

bin/katek new demo
cd demo
../bin/katek run
../bin/katek test
../bin/katek build --release
```
