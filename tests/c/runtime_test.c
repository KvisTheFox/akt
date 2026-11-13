#include "akt_runtime.h"

#include <assert.h>

int main(void) {
    AktString a = akt_str_cstr("hello");
    AktString b = akt_str_cstr(" world");
    AktString c = akt_str_concat(a, b);
    assert(c.len == 11);
    assert(akt_str_eq(c, akt_str_cstr("hello world")));
    assert(akt_str_contains(c, akt_str_cstr("world")));
    assert(akt_str_starts_with(c, akt_str_cstr("hello")));
    assert(akt_str_ends_with(c, akt_str_cstr("world")));

    AktVec v = akt_vec_new(sizeof(int));
    for (int i = 0; i < 10; i++) akt_vec_push(&v, &i);
    assert(akt_vec_len(&v) == 10);
    assert(*(int *)akt_vec_get(&v, 3) == 3);
    akt_vec_free(&v);

    AktArena arena = akt_arena_new(128);
    void *p = akt_arena_alloc(&arena, 32);
    assert(p != NULL);
    akt_arena_free(&arena);
    akt_runtime_shutdown();
    return 0;
}
