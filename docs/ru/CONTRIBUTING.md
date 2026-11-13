# Как контрибьютить в akt

## Сборка

```bash
xmake f -c --toolchain=clang-cl -m debug   # Windows
xmake build
```

или

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Тесты

```bash
powershell -ExecutionPolicy Bypass -File tests/run.ps1
```

Тесты:

- `tests/lang/*.akt` + `.out` — ожидаемый stdout;
- `tests/negative/*.akt` — должны завершаться ошибкой;
- `tests/c/*.c` — C-тесты рантайма.

## Стиль C-кода

- C11, без внешних зависимостей.
- 4 пробела, скобки на той же строке.
- Имена: `snake_case`, типы — `PascalCase`, макросы — `UPPER_CASE`.
- Никаких глобальных изменяемых состояний без необходимости.
- Ошибки компилятора — через `fatal_at(file, line, col, ...)`.
- Память компилятора можно не освобождать: процесс короткоживущий.

## Как добавить синтаксис

1. Токен в `lexer.c` и `aktc.h`.
2. Поле в `Expr`/`Stmt`.
3. Разбор в `parser.c`.
4. Тип в `checker.c`.
5. Генерация C в `codegen.c`.
6. Тест в `tests/lang` или `tests/negative`.
7. Документация в `docs/LANGUAGE.md`.
8. Отметка в `PLAN.md`/`docs/ROADMAP.md`.

## Как добавить std-функцию

1. Реализация в `runtime/src/akt_runtime.c`.
2. Прототип в `runtime/include/akt_runtime.h`.
3. Проверка типов в `checker.c`.
4. Кодогенерация в `codegen.c`.
5. C-тест в `tests/c/runtime_test.c`.
6. Пример в `examples/`.
7. Описание в `docs/STDLIB.md`.

## Правила совместимости

- Не ломать существующий синтаксис без `edition`.
- Новые ключевые слова — только через RFC/обсуждение.
- `katek.lock` должен быть воспроизводим.
- Никаких build-hooks и произвольного кода в зависимостях.

## Полезные команды

```bash
bin/aktc check file.akt
bin/aktc build file.akt --keep-c
bin/aktc run file.akt
bin/katek test
```

## Отладка

- `AKT_DEBUG=1` — печать команд `katek`.
- `AKT_DEBUG_PARSE=1` — печать разбора инструкций.
- `--keep-c` — сохранить `.gen.c`.
- Сгенерированный C можно собрать вручную через `clang`.
