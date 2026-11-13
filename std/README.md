# std

Здесь будут `.akt`-модули стандартной библиотеки. Пока std реализован
как встроенные функции и C-рантайм (`runtime/`), потому что импорт
между файлами ещё не реализован.

Текущий каталог функций — в [`../docs/STDLIB.md`](../docs/STDLIB.md).

Планируемая структура:

```text
std/
├── string.akt
├── math.akt
├── io.akt
├── fs.akt
├── path.akt
├── env.akt
├── collections/
│   ├── vector.akt
│   ├── map.akt
│   └── set.akt
├── json.akt
├── regex.akt
├── http.akt
├── sqlite.akt
├── thread.akt
└── sync.akt
```
