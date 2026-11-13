# Examples

Run any example with:

```bash
aktc run examples/hello.akt
# or, inside a katek project:
katek run
```

| File | What it shows |
|---|---|
| `hello.akt` | entry point, `println` |
| `tour.akt` | quick tour: variables, loops, structs, strings, arrays |
| `arithmetic.akt` | operators, integer and float math |
| `collections.akt` | `Vector`, `Map`, `Set`, `Deque` |
| `structs.akt` | structs, fields, methods, `static fn` |
| `errors.akt` | `Option`, `Result`, `?`, `??`, pattern matching |
| `json.akt` | parse and serialize JSON |
| `files.akt` | write/read files |
| `threads.akt` | `thread.spawn/join`, shared counters, `Mutex` |
| `stdlib.akt` | hash, base64, random, time, paths |
| `c_interop.akt` | calling C through `cinclude` |
| `modules/` | multi-file project |
| `game3d/` | small 3D game on GLFW + OpenGL (katek project) |

The 3D game needs GLFW 3.4; it is installed through `katek.toml`
(`xrepo`) — there is no `vendor/` directory.
