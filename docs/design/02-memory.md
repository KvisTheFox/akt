# akt — модель памяти

Синтаксис модификаторов — в [`01-syntax.md`](01-syntax.md). Здесь —
семантика и правила проверки.

---

## 1. Главный принцип

**В akt нет сборщика мусора. Никогда, ни в каком виде.**

Каждое значение освобождается в известный момент известным способом:

| Модификатор | Когда освобождается | Стоимость |
|---|---|---|
| `auto` (по умолчанию) | При выходе из региона | O(1) на регион |
| `arena` | При выходе из арены или `arena_free` | O(1) на арену |
| `shared` | Когда atomic refcount достигает 0 | O(1) на объект |
| `weak` | Не владеет; обнуляется при смерти объекта | O(1) |
| `manual` | Явно: `free`, `close`, `defer` | O(1) на объект |

Детерминизм означает: программист может назвать точку, в которой
память освобождается, не запуская программу.

---

## 2. Регионы

### 2.1. Что такое регион

**Регион** — лексическая область жизни значений. Регион создаётся
при входе в блок и освобождается целиком при выходе.

```akt
fn example() {
    // регион функции
    i32 x = 50
    dynstr s = "hello"
}   // регион завершён: x и s освобождены
```

Регион — абстракция, не обязательно стек. Компилятор может
реализовать его как:

- стек — для мелких значений с известным размером;
- `alloca` — для средних значений вне циклов;
- `malloc`/`free` — для крупных или динамических;
- пул — для повторяющихся аллокаций.

Семантика одна: **регион освобождается целиком при выходе**.

### 2.2. Дерево регионов

Регионы образуют дерево. У каждого региона один родитель.

```text
program
├── main_frame
│   ├── loop_iter_1
│   ├── loop_iter_2
│   └── block_1
├── thread_1
│   └── task_1
└── thread_2
```

Ссылка `^T` может указывать только на значение, которое живёт
дольше неё:

- на значение из того же региона;
- на значение из родительского региона;
- на значение из `shared`/`manual`, которое переживает регион.

Ссылка на дочерний регион, который уже завершился, — ошибка
компиляции. Ссылки «вбок» между соседними регионами запрещены,
кроме случаев, когда оба живут в общем родителе и проверка
доказывает порядок.

### 2.3. Автоматический регион

Каждая функция, каждый блок и каждая итерация цикла — свой регион.

```akt
fn process() {
    Vector<i32> a = [1, 2, 3]
    {
        Vector<i32> b = [4, 5, 6]
        // b живёт в регионе блока
    }   // b освобождён
    // a всё ещё жив
}   // a освобождён
```

Цикл — регион на итерацию:

```akt
for (i in 0..1_000_000) {
    dynstr tmp = f"iteration {i}"
    // tmp освобождается в конце каждой итерации
}
```

Это ключевое свойство для долгоживущих циклов: память не
накапливается.

### 2.4. Арены

Арена — именованный регион, которым управляет программист.

```akt
arena ar
i32 ar.x = 50
dynstr ar.s = "hello"
Vector<Vec2> ar.points = []
```

Синтаксис: `Type ar.name = expr` — объявить переменную в арене
`ar`. Доступ — `ar.name`.

Освобождение:

```akt
{
    arena ar
    i32 ar.x = 50
    // ...
}   // ar.x освобождён
```

Явное освобождение:

```akt
arena ar
i32 ar.x = 50
arena_free(ar)      // освободить сейчас
```

Вложенные арены:

```akt
arena outer
i32 outer.x = 1
{
    arena inner
    i32 inner.y = 2
}   // inner освобождена
// outer.x жив
```

Правила:

- Арена — не значение: её нельзя присвоить, вернуть или передать
  в функцию (кроме `arena_free`/`arena_stats`).
- Значения из арены нельзя вернуть из функции, если арена умрёт.
- Арена может быть вложена в регион, но не наоборот.
- `arena_free` идемпотентен: повторный вызов — no-op.

### 2.5. Регион vs `defer`

Это разные инструменты:

| | Регион | `defer` |
|---|---|---|
| Что освобождает | Память | Ресурс (файл, мьютекс, сокет) |
| Когда | При выходе из региона | При выходе из scope, LIFO |
| Стоимость | O(1) на регион | O(1) на `defer` |
| Для чего | `auto`, `arena` | любые ресурсы |

```akt
fn read_file() Result<void, std.error.Error> {
    auto f = std.io.open("data.txt")?
    defer f.close()
    // f закроется при выходе, даже при `?`
    return Ok(())
}
```

```akt
use std.collections

fn compute() {
    arena ar
    Vector<i32> ar.buf = []
    // ar.buf освободится целиком
}
```

---

## 3. `auto`

Переменная без модификатора живёт в текущем регионе.

```akt
fn example() {
    i32 x = 50
    dynstr s = "hello"
    Vector<i32> v = [1, 2, 3]
}
```

Правила:

- Освобождается при выходе из региона.
- Не может быть возвращена по ссылке.
- Может быть возвращена **по значению**: `return x` создаёт копию
  в регионе вызывающего.
- Может быть передана по ссылке `^T`, если вызываемый не переживёт
  регион.
- Не может быть передана в другой поток.

---

## 4. `shared` и `weak`

### 4.1. `shared`

`shared` — объект с атомарным счётчиком ссылок. Живёт, пока есть
хотя бы одна сильная ссылка.

```akt
shared Cache cache = Cache { /* ... */ }
// refcount == 1

shared Cache copy = cache
// refcount == 2

// copy выходит из scope: refcount == 1
// cache выходит из scope: refcount == 0 → destroy
```

### 4.2. Заголовок объекта

```c
struct AktSharedHeader {
    _Atomic uint32_t refcount;
    void (*destroy)(void*);
    uint32_t flags;
};
```

Заголовок находится перед пользовательскими данными. Выравнивание
гарантируется runtime.

### 4.3. Операции

| Операция | Что делает |
|---|---|
| `shared T b = a` | `a.refcount += 1`; `b` указывает на тот же объект |
| `b = a` | `a.refcount += 1`; старый `b.refcount -= 1` |
| выход из scope | `refcount -= 1` |
| `refcount == 0` | Вызвать `destroy`, освободить память |

Стоимость — O(1), атомарно, без пауз.

### 4.4. `weak`

`weak T` не увеличивает refcount. Нужен для обратных ссылок,
которые не должны создавать цикл.

```akt
struct Node {
    shared Node next
    weak Node parent
}

weak Node w = node
switch (w.upgrade()) {
    case Some(strong): { println("alive") }
    case None:         { println("freed") }
}
```

- `weak` не продлевает жизнь объекта.
- `upgrade()` возвращает `Option<shared T>`.
- После смерти объекта `upgrade()` возвращает `None`.
- `weak` можно копировать; это атомарная операция.

### 4.5. Запрет циклов через `shared`

Компилятор строит граф владения по полям структур. Ребро
`shared T` считает сильным; ребро `weak T` — слабым. Если в графе
есть цикл, содержащий хотя бы одно сильное ребро, — ошибка.

```akt
// ❌ ошибка: цикл Node -> Node
struct Node {
    shared Node next
}
```

```akt
// ✅ OK: обратная ссылка слабая
struct Node {
    shared Node next
    weak Node parent
}
```

```akt
// ✅ OK: нет владения
struct Node {
    Node next
}
```

Циклы через `manual` не проверяются: это ответственность
программиста и C-границы.

### 4.6. Потокобезопасность

`shared` делает атомарным **только refcount**. Данные внутри
объекта не становятся потокобезопасными автоматически.

```akt
use std.thread
use std.sync
use std.atomic

fn example() {
    shared i32 counter = 0

    // ❌ гонка данных: counter += 1 не атомарен
    thread.spawn(shared || { counter += 1 })

    // ✅ правильно: мьютекс
    shared Mutex mtx = Mutex::new()
    thread.spawn(shared || {
        mtx.lock()
        defer mtx.unlock()
        counter += 1
    })

    // ✅ или атомик
    shared Atomic<i32> atomic = Atomic<i32>::new(0)
    thread.spawn(shared || { atomic.fetch_add(1) })
}
```

Правила передачи между потоками:

| Тип | Можно передать |
|---|---|
| `auto` | ❌ нет: регион привязан к потоку |
| `arena` | ❌ нет |
| `shared T` | ✅ да, если `T` не содержит `auto`/`arena`-ссылок |
| `weak T` | ✅ да |
| `manual` | ⚠️ только если программист гарантирует безопасность |
| `^T` | ❌ нет, если владелец не `shared` |

Компилятор проверяет, что замыкание для `thread.spawn` не
захватывает `auto`/`arena` по ссылке. Гонки данных он **не**
доказывает — для этого используйте `std.sync`/`std.atomic`.

---

## 5. `manual`

### 5.1. Что это

`manual` — значение, которое программист создаёт и освобождает
сам. Это нужно для C-ресурсов, точного контроля и типов, которые
нельзя выразить через регионы.

```akt
manual std.io.File f = std.io.open("data.txt")?
defer f.close()
```

```akt
manual *i32 p = std.mem.alloc(4) as *i32
defer std.mem.free(p)
*p = 42
```

### 5.2. `defer` на объявлении

```akt
manual T x = create() defer destroy(x)
```

Порядок:

1. `create()` выделяет ресурс.
2. `defer destroy(x)` регистрирует деструктор.
3. При выходе из scope `destroy(x)` вызывается автоматически.

Это предпочтительный способ: ручное создание, автоматическое
освобождение.

### 5.3. `defer` как инструкция

```akt
fn example() {
    defer println("cleanup 1")
    defer println("cleanup 2")
    defer println("cleanup 3")
}
// Вывод:
// cleanup 3
// cleanup 2
// cleanup 1
```

### 5.4. Без `defer`

```akt
manual *i32 p = std.mem.alloc(4) as *i32
*p = 42
std.mem.free(p)
```

Если забыть освободить — предупреждение (в `--strict` — ошибка).
В `--lenient` — предупреждение.

### 5.5. `defer` в циклах

```akt
fn write_files() Result<void, std.error.Error> {
    for (i in 0..3) {
        manual std.io.File f = std.io.create(f"file_{i}.txt")?
        defer f.close()
        // закроется в конце итерации
    }
    return Ok(())
}
```

### 5.6. `defer` и ошибки

`defer` выполняется при любом выходе: `return`, `break`,
`continue`, `?`, паника.

```akt
fn risky(bool ok) Result<i32, string> {
    auto f = std.io.open("data.txt")?
    defer f.close()
    if (!ok) {
        return Err("invalid input")
        // f.close() всё равно вызовется
    }
    return Ok(42)
}
```

### 5.7. `manual` и C

`manual` не делает C безопасным. Указатель может быть `null`,
висячим или указывать на чужую память. Компилятор проверяет
только то, что можно доказать статически. Всё остальное — C-граница.

---

## 6. Комбинации

У переменной один модификатор памяти, но `defer` работает с любым:

```akt
// auto + defer (defer на что угодно)
i32 x = 50 defer println("x out")

// arena + defer
arena ar defer arena_free(ar)

// shared + defer
shared Cache c = Cache {} defer println("cache out")

// manual + defer
manual std.io.File f = std.io.open("data.txt")? defer f.close()
```

`shared` и `weak` — не «модификаторы хранения» в том же смысле:
это типы владения. `weak` не может быть единственным владельцем.

---

## 7. Порядок выхода из scope

1. `defer`-инструкции — LIFO.
2. `shared`/`weak` — decrement refcount в порядке, обратном
   объявлению.
3. `manual` — если был `defer`, он уже отработал в шаге 1.
   Иначе ответственность программиста.
4. `arena` — освобождается целиком.
5. `auto`-регион — освобождается целиком.

Пример:

```akt
struct Cache { i32 hits = 0 }

fn example() Result<void, std.error.Error> {
    shared Cache c = Cache {}
    defer println("c out")
    manual std.io.File f = std.io.open("x.txt")?
    defer f.close()
    arena ar
    i32 ar.x = 1
    i32 y = 50
    defer println("y out")
    return Ok(())
}
```

Порядок:

1. `println("y out")`
2. `f.close()`
3. `println("c out")`
4. `ar` освобождается
5. `c.refcount -= 1`

---

## 8. Что проверяет компилятор

### 8.1. `auto` не переживает регион

```akt
// ❌ ошибка: x живёт в регионе make()
fn make() ^Vec2 {
    Vec2 x = { x: 1.0, y: 2.0 }
    return ^x
}

// ✅ OK: возврат по значению копирует
fn make() Vec2 {
    return Vec2 { x: 1.0, y: 2.0 }
}

// ✅ OK: shared переживает регион
fn make_shared() shared Vec2 {
    shared Vec2 x = Vec2 { x: 1.0, y: 2.0 }
    return x
}
```

### 8.2. Ссылки не переживают владельцев

```akt
// ❌ ошибка
fn bad() ^i32 {
    i32 x = 50
    return ^x
}

// ✅ OK
fn good() i32 {
    i32 x = 50
    return x
}
```

### 8.3. Циклы через `shared` запрещены

```akt
// ❌ ошибка
struct Node {
    shared Node next
}
```

### 8.4. `manual` без освобождения

```akt
// ⚠️ warning: manual variable 'x' is not freed
fn example() {
    manual *i32 x = std.mem.alloc(4) as *i32
    *x = 42
}

// ✅ OK
fn example() {
    manual *i32 x = std.mem.alloc(4) as *i32
    defer std.mem.free(x)
    *x = 42
}
```

### 8.5. Инициализация

- Использование до инициализации — ошибка.
- Все поля структуры должны быть инициализированы, если нет
  значений по умолчанию.
- `cstruct`-литерал `{}` обнуляет все поля явно.

### 8.6. Границы

- Индексы массивов фиксированного размера проверяются статически,
  где возможно.
- В debug проверяются все индексы и срезы.
- В release статические проверки остаются; динамические — по
  профилю (`checks = "debug" | "always" | "never"`). `never`
  запрещён в `--strict`.

---

## 9. Сравнение с другими языками

| Свойство | C | C++ | Rust | Zig | Go | akt |
|---|---|---|---|---|---|---|
| Авто-память | ❌ | RAII | RAII | defer | GC | регион |
| Ручная | ✅ | ✅ | ❌ | ✅ | ❌ | `manual` |
| Refcount | ❌ | `shared_ptr` | `Rc` | ❌ | ❌ | `shared` |
| Weak | ❌ | `weak_ptr` | `Weak` | ❌ | ❌ | `weak` |
| GC | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ |
| `defer` | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ |
| Арены | ❌ | ❌ | ❌ | ✅ | ❌ | ✅ first-class |
| Циклы запрещены | ❌ | ❌ | ✅ | ❌ | ❌ | ✅ |

---

## 10. Производительность

| Операция | Стоимость | Примечание |
|---|---|---|
| Выделение в регионе | O(1) | Сдвиг указателя |
| Освобождение региона | O(1) | Один сдвиг |
| Выделение `shared` | O(1) | `malloc` + init |
| Копия `shared` | O(1) | Атомарный инкремент |
| Уничтожение `shared` | O(1) | При refcount == 0 |
| `manual` alloc/free | O(1) | `malloc`/`free` |
| `defer` | O(1) | Один вызов |
| Поиск циклов | нет | Структурно запрещены |

Паузы: ноль. Сканирование: ноль. Stop-the-world: никогда.

---

## 11. Полный пример

```akt
use std.io
use std.collections
use std.math.vector
use std.math.statistics

struct Cache {
    i32 hits = 0

    static fn new() shared Cache {
        shared Cache c = Cache {}
        return c
    }
}

struct Particle {
    Vec2 pos
    Vec2 vel
    f32 mass

    fn energy() f64 {
        return 0.5 * (self.mass as f64) * self.vel.dot(self.vel)
    }
}

fn simulate() Result<void, std.error.Error> {
    // Вся симуляция — в арене
    arena sim
    Vector<Particle> sim.particles = []

    for (i in 0..1000) {
        sim.particles.push({
            pos: { x: i as f64, y: 0.0 }
            vel: { x: 0.0, y: 0.0 }
            mass: 1.0
        })
    }

    // Общий кэш — shared
    shared Cache cache = Cache::new()

    // Ручной файл — с гарантированным закрытием
    manual io.File f = io.create("sim.log")?
    defer f.close()

    for (step in 0..100) {
        for (i, p in sim.particles) {
            sim.particles[i].vel.y += 0.1
            sim.particles[i].pos.y += sim.particles[i].vel.y
        }

        {
            Vector<f64> energies = []
            for (p in sim.particles) {
                energies.push(p.energy())
            }
            f.write_line(f"step {step}: mean E = {mean(energies)}")?
        }   // energies освобождён
    }

    return Ok(())
}   // sim, cache, f освобождены
```

---

## 12. Итог

1. Регионы — дерево, ациклично, O(1).
2. `auto` — по умолчанию, живёт в регионе.
3. `arena` — именованные регионы для группового освобождения.
4. `shared` — atomic refcount для данных, переживающих регион.
5. `weak` — обратные ссылки без цикла.
6. `manual` — ручное управление для C и точного контроля.
7. `defer` — ресурсы, LIFO, при любом выходе.
8. Без GC — никогда.
9. Детерминированно — всегда.
10. Гонки данных не проверяются; для них есть `std.sync` и
    `std.atomic`.
