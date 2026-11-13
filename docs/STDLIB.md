# Standard library

## Built-ins

`print`, `println`, `len`, `str`, `int`, `float`, `assert`,
`assert_eq`, `assert_ne`, `panic`.

## Math

`sqrt`, `sin`, `cos`, `tan`, `floor`, `ceil`, `round`, `abs`, `pow`,
`min`, `max`; constants `PI`, `E`, `TAU`, `PHI`, `INF`, `NAN`.

## Strings

`len`, `byte_len`, `char_len`, `trim`, `to_upper`, `to_lower`, `reverse`,
`capitalize`, `contains`, `starts_with`, `ends_with`, `find`, `replace`,
`repeat`, `pad_left`, `pad_right`, `split`, `parse_int`, `parse_float`.

## IO / FS / path

```akt
io.read_to_string(path)
io.write_to_string(path, s)
io.exists(path)
fs.exists(path)
fs.read_to_string(path)
fs.list_dir(path)   // Vector<string>
fs.walk(path)       // Vector<string>
path.join(a, b)
path.basename(p)
path.dirname(p)
path.extension(p)
path.absolute(p)
```

## env / time / process

```akt
env.get(name)
env.cwd()
time.now_ms()
time.sleep_ms(n)
process.pid()
process.exit(code)
```

## random / hash / base64 / json / regex / http / sqlite

```akt
random.int(max)
random.float()
hash.fnv1a(s)
base64.encode(s)
base64.decode(s)

Json j = json.parse("{\"a\":1}")
json.stringify(j)
j["a"]

regex.match("\\d+", "abc123")
regex.find("\\d+", "abc123")
regex.replace("\\d+", "a1b2", "#")

string body = http.get("https://example.com")
string echo = http.post("https://example.com/api", "{}")

Sqlite db = sqlite.open(":memory:")
db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)")
for (row in db.query("SELECT * FROM t")) { println(row) }
db.close()
```

## Sync / atomic / thread

```akt
Mutex m = Mutex::new()
m.lock()
m.unlock()

Atomic<i32> a = Atomic::new(0)
a.fetch_add(1)
a.load()
a.store(10)

fn worker() { println("thread") }
Handle h = thread.spawn(worker)
thread.join(h)

shared i32 counter = 0
shared Mutex mtx = Mutex::new()
Handle h2 = thread.spawn(|| {
    mtx.lock()
    defer mtx.unlock()
    counter += 1
})
thread.join(h2)
```

`thread.spawn` accepts a global function or a `|| { ... }` closure.
Captured `shared` variables are passed as refcount cells.

## Collections

`Vector<T>`: `push`, `pop`, `clear`, `len`, indexing, iteration.
`Map<string,T>`: `set`, `get`, `contains`, `remove`, `len`.
`Set<T>`: `add`, `contains`, `remove`, `len`.
`Deque<T>`: vector alias with `push_back`, `push_front`, `front`, `back`.

## C interop

```akt
cinclude <stdio.h>
clink "m"
c.printf("hello\n")
```

## Tiers

Tier 0: core strings/io/fs/path/env/time/process/collections.
Tier 1: json/regex/http/sqlite/crypto/compress/net (planned).
Ecosystem: akt-ui/akt-gfx/akt-ml (planned).
