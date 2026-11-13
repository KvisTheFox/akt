# Memory model

> Status: **design**. The current preview (0.9) uses the manual model
> described below. The automatic system is the main goal for 1.0.

## Goals

- no tracing garbage collector, no stop-the-world pauses;
- no use-after-free and no leaks by construction;
- no annotations for ordinary code;
- deterministic destruction, suitable for systems and games;
- manual control remains available for hot paths and C interop.

## Current model (0.9)

- `defer` — LIFO cleanup on block exit and before `return`;
- `arena` — bulk allocation freed at scope exit;
- `shared T` — heap cell with an atomic refcount; a `defer` releases it;
- `weak T` — non-owning reference, `upgrade()` returns `Option<T>`;
- `manual` — plain ownership;
- `^T` — borrowed reference; returning a reference to a local is
  rejected, but there is no full borrow checker.

This is C-style ownership with helpers. It is fast and explicit, but the
programmer has to think about memory.

## Automatic model (target for 1.0)

The model combines three techniques that are all compile-time or
refcount-based, never tracing:

### 1. Escape analysis and stack allocation

Every heap value starts on the stack. If it does not escape the function
(not returned, not stored in a global, field, container or closure, not
passed to an unknown function), it lives on the stack and is destroyed
at scope exit. No refcount, no allocation.

### 2. Reference counting for escaping values

Values that escape are promoted to heap objects with an atomic reference
count. The compiler inserts `retain`/`release`:

- on copy, assignment and capture;
- at the last use of a value (NLL-style), so memory is freed as early as
  possible;
- on every exit path, including `?`, `return`, `break`, `switch` and
  panics.

Destruction is immediate and deterministic: when the last strong
reference dies, the object is destroyed.

### 3. Cycles are rejected, not collected

A tracing collector is not allowed, and a separate cycle collector is
complex and slow. Instead the compiler enforces a simple rule:

- a managed object may own other managed objects;
- back-references must be `weak`;
- the type graph of strong references must be acyclic.

This is checked at compile time (SCC analysis of the managed type graph).
If a cycle is possible, the compiler reports it and suggests `weak`.
Result: **no cycles, therefore no leaks** — without a collector.

### 4. Optional manual control

`arena`, `manual` and raw pointers stay. Data-oriented code, C
libraries and tight loops can bypass the automatic model explicitly.

### 5. Debug safety net

In debug builds the allocator:

- fills freed memory with a poison pattern;
- keeps a registry of live blocks and detects double free and
  use-after-free with a clear error;
- reports leaks at program exit with allocation sites.

In release builds the checks compile out.

## Semantics summary

| Value | Where | Freed |
|---|---|---|
| does not escape | stack | at scope exit |
| escapes | heap + refcount | at last strong release |
| `weak` reference | no ownership | never (control block dies with last weak) |
| `arena` | region | at region exit |
| `manual` | anywhere | by the programmer |

There is no GC and no hidden thread. Worst-case latency is bounded by
destructor chains, same as C++ RAII.

## Implementation plan

1. Type-level ownership flags (`owned`, `borrowed`, `weak`) in the
   checker; `copy`/`move` rules.
2. Escape analysis pass on the AST.
3. Codegen: emit `akt_rc_retain` / `akt_rc_release` and
   `akt_rc_new`, reuse the existing atomic refcount runtime.
4. Strong-reference graph check for managed types.
5. Debug allocator (poison, registry, leak report).
6. Migration: `shared` becomes the explicit form of the automatic heap
   type; existing code keeps working with `manual`/`arena`.

The feature is on the 1.0 roadmap. Until it lands, the release stays a
preview.
