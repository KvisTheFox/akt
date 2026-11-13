# Тесты

- `lang/*.akt` — позитивные тесты. Если рядом есть `.out`, stdout
  сравнивается побайтово (с точностью до финальных переводов строк).
- `lang/opengl_probe.akt` — проверка линковки с `opengl32`.
- `negative/*.akt` — должны завершаться ошибкой компиляции.
- `c/*.c` — C-тесты рантайма.
- `run.ps1` — общий запуск.
- `fuzz.ps1` — случайные token-потоки в парсер (не должно быть
  crash/hang вне кодов 0/1/2).
- `asan.ps1` — ASan/UBSan прогон (`-fsanitize=address,undefined`). На
  Windows автоматически добавляет runtime LLVM в PATH.

```powershell
powershell -ExecutionPolicy Bypass -File tests/run.ps1
```

Требуется собранный `bin/aktc.exe` и `bin/akt_tests.exe`:

```bash
xmake build
```
