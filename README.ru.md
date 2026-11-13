# akt (по-русски)

**akt** — компилируемый системный язык: C-подобный синтаксис, C11-бэкенд,
предсказуемая память без GC, `defer`, арены (в разработке) и простая
интеграция с C.

Этот репозиторий — рабочая реализация языка, а не только спецификация.
Спецификация и дизайн лежат в [`docs/design/`](docs/design/README.md).

## Статус

Текущая версия — **1.0.0**.

- `aktc` компилирует `.akt` → C11 → exe/so через `clang`/`gcc`/`cc`.
- Язык: переменные, функции, методы, `static fn`, структуры,
  `cstruct`/`cunion`, `enum` с payload, `switch`-выражения, массивы,
  `Vector`/`Map`/`Set`/`Deque`, `Option`/`Result`, `?`, `defer`,
  арены, модули, замыкания, `async`/`await` (задачи на потоках),
  `thread.spawn/join`, `shared`/`weak` с `upgrade()`, `Mutex`,
  `Atomic<T>`, `type`-алиасы, f-строки с `{x:.2}`.
- C-интероп: `cinclude` и `cimport` с нативным импортом объявлений
  (и автоматической компиляцией соседнего `.c`), ручные
  `cfn`/`cstruct`/`cunion`, `_Static_assert` проверки ABI;
- зависимости — только через `katek.toml` (xrepo/pkg-config/system/…);
  сборка и кэш заголовков в `.kache/`, исходники остаются чистыми;
- Std: JSON, regex, HTTP (curl), SQLite, файлы, env, path, время,
  random, hash, base64.
- `katek`: `new`, `build`, `run`, `check`, `test`, `clean`, `doctor`,
  `lock`, `jni`, `c add/sync/install`, `export xmake|cmake`,
  Android NDK.
- Release-кодогенерация: `-O3 -march=native -flto -fuse-ld=lld`.
- Release-кодогенерация: `-O3 -march=native -flto -fuse-ld=lld`.

Ограничения 1.0: нет дженериков кроме `Vector`/`Map`/`Set`/`Deque`,
async выполняется на потоках (без отдельного executor), нет полного
анализа времени жизни `^T`/`weak`, импортёр заголовков не разбирает
битфилды и сложные declarator'ы, нет реестра пакетов. Подробнее — [`docs/ru/ROADMAP.md`](docs/ru/ROADMAP.md)
и [`PLAN.md`](docs/ru/PLAN.md).

## Установка

Windows: скачайте `akt-0.9.0-setup.exe` и запустите.

Установщик проверит Clang и, если его нет, спросит, скачать ли LLVM.
Всё ставится в `%LOCALAPPDATA%\akt`, `bin` добавляется в PATH.

```powershell
akt-0.9.0-setup.exe
akt-0.9.0-setup.exe --quiet --no-llvm --prefix D:\akt
```

Linux/macOS: `akt-0.9.0-src.tar.gz` + `scripts/install.sh`.

Пример 3D-игры на GLFW: [`examples/game3d`](examples/game3d).

Из исходников:

```bash
# Windows
xmake f -c --toolchain=clang-cl -m release
xmake build

# Linux/macOS
sh scripts/install.sh --prefix "$HOME/.local"
```

`scripts/install.sh` соберёт toolchain и поставит `aktc`/`katek` в
`$PREFIX/bin`. Артефакты сборки и кэш заголовков лежат в `.kache/`,
исходники проекта остаются чистыми.

Или CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Компилятор и `katek` появятся в `bin/`.

```bash
bin/aktc run examples/hello.akt
```

Или через систему сборки:

```bash
cd examples
../bin/katek new demo
cd demo
../../bin/katek run
```

## Пример

```akt
struct Vec2 {
    f64 x
    f64 y

    fn length() f64 {
        return sqrt(self.x * self.x + self.y * self.y)
    }

    static fn zero() Vec2 {
        return Vec2 { x: 0.0, y: 0.0 }
    }
}

fn fib(int n) int {
    if (n < 2) { return n }
    return fib(n - 1) + fib(n - 2)
}

fn main() int {
    Vec2 v = Vec2 { x: 3.0, y: 4.0 }
    println(f"length = {v.length()}")
    for (i in 0..10) {
        print(f"{fib(i)} ")
    }
    println("")
    return 0
}
```

## Структура

```text
akt/
├── aktc/        компилятор: lexer, parser, checker, codegen
├── katek/       сборка, запуск, тесты, пакеты
├── runtime/     C-рантайм (строки, vector, arena, IO)
├── std/         будущие akt-модули std
├── examples/    примеры
├── docs/        документация
├── docs/design/ спецификация языка
├── xmake.lua
└── CMakeLists.txt
```

## Документация

- [Для новичков](docs/ru/GETTING_STARTED.md)
- [Язык: синтаксис и семантика](docs/ru/LANGUAGE.md)
- [Архитектура компилятора](docs/ru/ARCHITECTURE.md)
- [Стандартная библиотека](docs/ru/STDLIB.md)
- [CLI: aktc и katek](docs/ru/CLI.md)
- [C-библиотеки и зависимости](docs/ru/C_LIBRARIES.md)
- [Android NDK](docs/ru/ANDROID.md)
- [JNI bindings](docs/ru/JNI.md)
- [План до 1.0](docs/ru/ROADMAP.md)
- [Как контрибьютить](docs/ru/CONTRIBUTING.md)
- [Спецификация](docs/design/README.md)
- [English documentation](README.md)

## Статус и честность

- Язык написан с активным использованием нейросети.
- Язык молодой: 1.0 — это стабильный релиз, но ошибки возможны.
  Пожалуйста, сообщайте о них в issues.
- Язык будет улучшаться: диагностика, импорт заголовков, async и
  стандартная библиотека развиваются.

## Лицензия

GPL-3.0-or-later с [исключением для программ](LICENSE-EXCEPTION):
программы, написанные на akt, можно лицензировать как угодно, включая
закрытый код; сам akt и его форки остаются открытыми (GPL).

## Сообщество

- Telegram: [@bykvis](https://t.me/bykvis)
- Баги и предложения: issues в репозитории.
