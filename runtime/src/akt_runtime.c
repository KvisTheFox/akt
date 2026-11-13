#include "akt_runtime.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

struct AktArenaBlock {
    AktArenaBlock *next;
    size_t used;
    size_t cap;
    unsigned char data[];
};

typedef struct AktAllocNode {
    void *ptr;
    struct AktAllocNode *next;
} AktAllocNode;

static AktAllocNode *akt_allocs = NULL;

static void akt_track(void *p) {
    if (!p) return;
    AktAllocNode *n = (AktAllocNode *)malloc(sizeof(AktAllocNode));
    if (!n) return;
    n->ptr = p;
    n->next = akt_allocs;
    akt_allocs = n;
}

static void akt_untrack(void *p) {
    AktAllocNode **pp = &akt_allocs;
    while (*pp) {
        if ((*pp)->ptr == p) {
            AktAllocNode *dead = *pp;
            *pp = dead->next;
            free(dead);
            return;
        }
        pp = &(*pp)->next;
    }
}

void *akt_alloc(size_t size) {
    void *p = malloc(size ? size : 1);
    if (!p) akt_panic("out of memory");
    akt_track(p);
    return p;
}

void *akt_calloc(size_t count, size_t size) {
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p) akt_panic("out of memory");
    akt_track(p);
    return p;
}

void *akt_realloc(void *ptr, size_t size) {
    if (ptr) akt_untrack(ptr);
    void *p = realloc(ptr, size ? size : 1);
    if (!p) akt_panic("out of memory");
    akt_track(p);
    return p;
}

void akt_free(void *ptr) {
    if (!ptr) return;
    akt_untrack(ptr);
    free(ptr);
}

void akt_runtime_shutdown(void) {
    AktAllocNode *n = akt_allocs;
    while (n) {
        AktAllocNode *next = n->next;
        free(n->ptr);
        free(n);
        n = next;
    }
    akt_allocs = NULL;
}

AktArena akt_arena_new(size_t block_size) {
    AktArena a;
    a.head = NULL;
    a.block_size = block_size ? block_size : 64 * 1024;
    return a;
}

void *akt_arena_alloc(AktArena *arena, size_t size) {
    if (!arena) return akt_alloc(size);
    size = (size + 15u) & ~(size_t)15u;
    if (!arena->head || arena->head->used + size > arena->head->cap) {
        size_t cap = size > arena->block_size ? size : arena->block_size;
        AktArenaBlock *b = (AktArenaBlock *)malloc(sizeof(AktArenaBlock) + cap);
        if (!b) akt_panic("out of memory");
        b->next = arena->head;
        b->used = 0;
        b->cap = cap;
        arena->head = b;
    }
    void *p = arena->head->data + arena->head->used;
    arena->head->used += size;
    return p;
}

char *akt_arena_strdup(AktArena *arena, const char *s) {
    size_t n = strlen(s);
    char *p = (char *)akt_arena_alloc(arena, n + 1);
    memcpy(p, s, n + 1);
    return p;
}

void akt_arena_free(AktArena *arena) {
    if (!arena) return;
    AktArenaBlock *b = arena->head;
    while (b) {
        AktArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    arena->head = NULL;
}

AktString akt_str(const char *data, size_t len) {
    AktString s;
    s.data = data;
    s.len = len;
    return s;
}

AktString akt_str_cstr(const char *data) {
    return akt_str(data, data ? strlen(data) : 0);
}

AktString akt_str_from_int(akt_i64 value) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%" PRId64, value);
    char *p = (char *)akt_alloc((size_t)n + 1);
    memcpy(p, buf, (size_t)n + 1);
    return akt_str(p, (size_t)n);
}

AktString akt_str_from_float(akt_f64 value) {
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "%.15g", value);
    char *p = (char *)akt_alloc((size_t)n + 1);
    memcpy(p, buf, (size_t)n + 1);
    return akt_str(p, (size_t)n);
}

AktString akt_str_from_float_prec(akt_f64 value, int prec) {
    char buf[512];
    if (prec < 0) prec = 0;
    if (prec > 64) prec = 64;
    int n = snprintf(buf, sizeof(buf), "%.*f", prec, value);
    char *p = (char *)akt_alloc((size_t)n + 1);
    memcpy(p, buf, (size_t)n + 1);
    return akt_str(p, (size_t)n);
}

AktString akt_str_from_bool(akt_bool value) {
    return value ? akt_str_cstr("true") : akt_str_cstr("false");
}

AktString akt_str_from_char(uint32_t value) {
    char buf[4];
    size_t n = 0;
    if (value < 0x80) {
        buf[n++] = (char)value;
    } else if (value < 0x800) {
        buf[n++] = (char)(0xC0 | (value >> 6));
        buf[n++] = (char)(0x80 | (value & 0x3F));
    } else if (value < 0x10000) {
        buf[n++] = (char)(0xE0 | (value >> 12));
        buf[n++] = (char)(0x80 | ((value >> 6) & 0x3F));
        buf[n++] = (char)(0x80 | (value & 0x3F));
    } else {
        buf[n++] = (char)(0xF0 | (value >> 18));
        buf[n++] = (char)(0x80 | ((value >> 12) & 0x3F));
        buf[n++] = (char)(0x80 | ((value >> 6) & 0x3F));
        buf[n++] = (char)(0x80 | (value & 0x3F));
    }
    char *p = (char *)akt_alloc(n + 1);
    memcpy(p, buf, n);
    p[n] = 0;
    return akt_str(p, n);
}

AktString akt_str_concat(AktString a, AktString b) {
    size_t n = a.len + b.len;
    char *p = (char *)akt_alloc(n + 1);
    if (a.len) memcpy(p, a.data, a.len);
    if (b.len) memcpy(p + a.len, b.data, b.len);
    p[n] = 0;
    return akt_str(p, n);
}

AktString akt_str_slice(AktString s, size_t start, size_t end) {
    if (start > s.len) start = s.len;
    if (end > s.len) end = s.len;
    if (end < start) end = start;
    return akt_str(s.data + start, end - start);
}

int akt_str_eq(AktString a, AktString b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.data, b.data, a.len) == 0);
}

int akt_str_cmp(AktString a, AktString b) {
    size_t n = a.len < b.len ? a.len : b.len;
    int c = n ? memcmp(a.data, b.data, n) : 0;
    if (c) return c;
    return (a.len > b.len) - (a.len < b.len);
}

size_t akt_str_len(AktString s) {
    return s.len;
}

uint32_t akt_str_char_at(AktString s, size_t index) {
    if (AKT_UNLIKELY(index >= s.len)) akt_panic("string index out of bounds");
    const unsigned char *p = (const unsigned char *)s.data + index;
    unsigned char c = *p;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && index + 1 < s.len)
        return ((uint32_t)(c & 0x1F) << 6) | (p[1] & 0x3F);
    if ((c & 0xF0) == 0xE0 && index + 2 < s.len)
        return ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    if ((c & 0xF8) == 0xF0 && index + 3 < s.len)
        return ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
               ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    return c;
}

akt_i64 akt_str_to_int(AktString s, int *ok) {
    char buf[128];
    size_t n = s.len < sizeof(buf) - 1 ? s.len : sizeof(buf) - 1;
    memcpy(buf, s.data, n);
    buf[n] = 0;
    char *end = NULL;
    errno = 0;
    long long v = strtoll(buf, &end, 10);
    if (ok) *ok = errno == 0 && end && *end == 0 && n > 0;
    return (akt_i64)v;
}

akt_f64 akt_str_to_float(AktString s, int *ok) {
    char buf[256];
    size_t n = s.len < sizeof(buf) - 1 ? s.len : sizeof(buf) - 1;
    memcpy(buf, s.data, n);
    buf[n] = 0;
    char *end = NULL;
    errno = 0;
    double v = strtod(buf, &end);
    if (ok) *ok = errno == 0 && end && *end == 0 && n > 0;
    return v;
}

static char *akt_cstr_dup(AktString s) {
    char *p = (char *)akt_alloc(s.len + 1);
    if (s.len) memcpy(p, s.data, s.len);
    p[s.len] = 0;
    return p;
}

AktString akt_str_upper(AktString s) {
    char *p = (char *)akt_alloc(s.len + 1);
    for (size_t i = 0; i < s.len; i++) p[i] = (char)toupper((unsigned char)s.data[i]);
    p[s.len] = 0;
    return akt_str(p, s.len);
}

AktString akt_str_lower(AktString s) {
    char *p = (char *)akt_alloc(s.len + 1);
    for (size_t i = 0; i < s.len; i++) p[i] = (char)tolower((unsigned char)s.data[i]);
    p[s.len] = 0;
    return akt_str(p, s.len);
}

AktString akt_str_trim(AktString s) {
    size_t a = 0, b = s.len;
    while (a < b && isspace((unsigned char)s.data[a])) a++;
    while (b > a && isspace((unsigned char)s.data[b - 1])) b--;
    return akt_str_slice(s, a, b);
}

AktString akt_str_reverse(AktString s) {
    char *p = (char *)akt_alloc(s.len + 1);
    for (size_t i = 0; i < s.len; i++) p[i] = s.data[s.len - 1 - i];
    p[s.len] = 0;
    return akt_str(p, s.len);
}

AktString akt_str_capitalize(AktString s) {
    char *p = (char *)akt_alloc(s.len + 1);
    for (size_t i = 0; i < s.len; i++) p[i] = (char)tolower((unsigned char)s.data[i]);
    if (s.len) p[0] = (char)toupper((unsigned char)p[0]);
    p[s.len] = 0;
    return akt_str(p, s.len);
}

AktString akt_str_replace(AktString s, AktString from, AktString to) {
    if (from.len == 0) return s;
    size_t count = 0;
    for (size_t i = 0; i + from.len <= s.len;) {
        if (memcmp(s.data + i, from.data, from.len) == 0) {
            count++;
            i += from.len;
        } else {
            i++;
        }
    }
    size_t n = s.len + count * (to.len > from.len ? to.len - from.len : 0) -
               (count * (from.len > to.len ? from.len - to.len : 0));
    char *p = (char *)akt_alloc(n + 1);
    size_t o = 0;
    for (size_t i = 0; i < s.len;) {
        if (i + from.len <= s.len && memcmp(s.data + i, from.data, from.len) == 0) {
            if (to.len) memcpy(p + o, to.data, to.len);
            o += to.len;
            i += from.len;
        } else {
            p[o++] = s.data[i++];
        }
    }
    p[o] = 0;
    return akt_str(p, o);
}

AktString akt_str_repeat(AktString s, akt_i64 count) {
    if (count < 0) count = 0;
    size_t n = s.len * (size_t)count;
    char *p = (char *)akt_alloc(n + 1);
    for (akt_i64 i = 0; i < count; i++)
        if (s.len) memcpy(p + (size_t)i * s.len, s.data, s.len);
    p[n] = 0;
    return akt_str(p, n);
}

int akt_str_contains(AktString s, AktString needle) {
    if (needle.len == 0) return 1;
    if (needle.len > s.len) return 0;
    for (size_t i = 0; i + needle.len <= s.len; i++)
        if (memcmp(s.data + i, needle.data, needle.len) == 0) return 1;
    return 0;
}

int akt_str_starts_with(AktString s, AktString prefix) {
    return prefix.len <= s.len && memcmp(s.data, prefix.data, prefix.len) == 0;
}

int akt_str_ends_with(AktString s, AktString suffix) {
    return suffix.len <= s.len &&
           memcmp(s.data + s.len - suffix.len, suffix.data, suffix.len) == 0;
}

akt_i64 akt_str_find(AktString s, AktString needle) {
    if (needle.len == 0) return 0;
    if (needle.len > s.len) return -1;
    for (size_t i = 0; i + needle.len <= s.len; i++)
        if (memcmp(s.data + i, needle.data, needle.len) == 0) return (akt_i64)i;
    return -1;
}

AktVec akt_str_split(AktString s, AktString sep) {
    AktVec out = akt_vec_new(sizeof(AktString));
    if (sep.len == 0) {
        for (size_t i = 0; i < s.len; i++) {
            AktString ch = akt_str_slice(s, i, i + 1);
            akt_vec_push(&out, &ch);
        }
        return out;
    }
    size_t start = 0;
    while (start <= s.len) {
        size_t i = start;
        while (i + sep.len <= s.len && memcmp(s.data + i, sep.data, sep.len) != 0) i++;
        AktString part = akt_str_slice(s, start, i);
        akt_vec_push(&out, &part);
        if (i + sep.len > s.len) break;
        start = i + sep.len;
    }
    return out;
}

AktString akt_str_pad_left(AktString s, akt_i64 width, uint32_t ch) {
    if (width <= (akt_i64)s.len) return s;
    size_t pad = (size_t)width - s.len;
    char *p = (char *)akt_alloc((size_t)width + 1);
    memset(p, (int)(ch < 128 ? ch : ' '), pad);
    if (s.len) memcpy(p + pad, s.data, s.len);
    p[width] = 0;
    return akt_str(p, (size_t)width);
}

AktString akt_str_pad_right(AktString s, akt_i64 width, uint32_t ch) {
    if (width <= (akt_i64)s.len) return s;
    char *p = (char *)akt_alloc((size_t)width + 1);
    if (s.len) memcpy(p, s.data, s.len);
    memset(p + s.len, (int)(ch < 128 ? ch : ' '), (size_t)width - s.len);
    p[width] = 0;
    return akt_str(p, (size_t)width);
}

akt_i64 akt_str_parse_int(AktString s) {
    int ok = 0;
    akt_i64 v = akt_str_to_int(s, &ok);
    return ok ? v : 0;
}

akt_f64 akt_str_parse_float(AktString s) {
    int ok = 0;
    akt_f64 v = akt_str_to_float(s, &ok);
    return ok ? v : 0.0;
}

AktString akt_path_join(AktString a, AktString b) {
    if (a.len == 0) return b;
    if (b.len == 0) return a;
    if (b.data[0] == '/' || b.data[0] == '\\' ||
        (b.len >= 2 && b.data[1] == ':'))
        return b;
    int need_sep = a.data[a.len - 1] != '/' && a.data[a.len - 1] != '\\';
    size_t n = a.len + (size_t)need_sep + b.len;
    char *p = (char *)akt_alloc(n + 1);
    memcpy(p, a.data, a.len);
    size_t o = a.len;
#ifdef _WIN32
    if (need_sep) p[o++] = '\\';
#else
    if (need_sep) p[o++] = '/';
#endif
    memcpy(p + o, b.data, b.len);
    p[n] = 0;
    return akt_str(p, n);
}

AktString akt_path_basename(AktString p) {
    size_t i = p.len;
    while (i > 0 && p.data[i - 1] != '/' && p.data[i - 1] != '\\') i--;
    return akt_str_slice(p, i, p.len);
}

AktString akt_path_dirname(AktString p) {
    size_t i = p.len;
    while (i > 0 && p.data[i - 1] != '/' && p.data[i - 1] != '\\') i--;
    if (i == 0) return akt_str_cstr(".");
    while (i > 1 && (p.data[i - 1] == '/' || p.data[i - 1] == '\\')) i--;
    return akt_str_slice(p, 0, i);
}

AktString akt_path_extension(AktString p) {
    size_t dot = p.len;
    size_t i = p.len;
    while (i > 0 && p.data[i - 1] != '/' && p.data[i - 1] != '\\') {
        if (p.data[i - 1] == '.' && dot == p.len) dot = i - 1;
        i--;
    }
    if (dot == p.len) return akt_str_cstr("");
    return akt_str_slice(p, dot + 1, p.len);
}

AktString akt_path_absolute(AktString p) {
    if (p.len && (p.data[0] == '/' || p.data[0] == '\\')) return p;
    if (p.len >= 2 && p.data[1] == ':') return p;
    return akt_path_join(akt_get_cwd(), p);
}

void akt_print_str(AktString s) {
    if (s.len) fwrite(s.data, 1, s.len, stdout);
}

void akt_println_str(AktString s) {
    akt_print_str(s);
    fputc('\n', stdout);
}

void akt_print_int(akt_i64 v) { printf("%" PRId64, v); }
void akt_println_int(akt_i64 v) { printf("%" PRId64 "\n", v); }
void akt_print_float(akt_f64 v) { printf("%.15g", v); }
void akt_println_float(akt_f64 v) { printf("%.15g\n", v); }
void akt_print_bool(akt_bool v) { fputs(v ? "true" : "false", stdout); }
void akt_println_bool(akt_bool v) { fputs(v ? "true\n" : "false\n", stdout); }
void akt_print_char(uint32_t v) { akt_print_str(akt_str_from_char(v)); }
void akt_println_char(uint32_t v) { akt_print_char(v); fputc('\n', stdout); }
void akt_print_newline(void) { fputc('\n', stdout); }

static void akt_vec_grow(AktVec *v, size_t need) {
    if (v->cap >= need) return;
    size_t cap = v->cap ? v->cap : 8;
    while (cap < need) cap *= 2;
    v->data = (unsigned char *)akt_realloc(v->data, cap * v->elem_size);
    v->cap = cap;
}

AktVec akt_vec_new(size_t elem_size) {
    AktVec v;
    v.data = NULL;
    v.len = 0;
    v.cap = 0;
    v.elem_size = elem_size ? elem_size : 1;
    return v;
}

void akt_vec_free(AktVec *v) {
    if (!v) return;
    akt_free(v->data);
    v->data = NULL;
    v->len = v->cap = 0;
}

void akt_vec_reserve(AktVec *v, size_t need) { akt_vec_grow(v, need); }

AKT_HOT void akt_vec_push(AktVec *v, const void *elem) {
    akt_vec_grow(v, v->len + 1);
    memcpy(v->data + v->len * v->elem_size, elem, v->elem_size);
    v->len++;
}

void akt_vec_push_front(AktVec *v, const void *elem) {
    akt_vec_grow(v, v->len + 1);
    memmove(v->data + v->elem_size, v->data, v->len * v->elem_size);
    memcpy(v->data, elem, v->elem_size);
    v->len++;
}

void *akt_vec_front(const AktVec *v) {
    if (v->len == 0) akt_panic("front of empty vector");
    return v->data;
}

void *akt_vec_back(const AktVec *v) {
    if (v->len == 0) akt_panic("back of empty vector");
    return v->data + (v->len - 1) * v->elem_size;
}

void *akt_vec_get(const AktVec *v, size_t index) {
    if (AKT_UNLIKELY(index >= v->len)) akt_panic("vector index out of bounds");
    return v->data + index * v->elem_size;
}

void akt_vec_set(AktVec *v, size_t index, const void *elem) {
    if (index >= v->len) akt_panic("vector index out of bounds");
    memcpy(v->data + index * v->elem_size, elem, v->elem_size);
}

void akt_vec_pop(AktVec *v, void *out) {
    if (v->len == 0) akt_panic("pop from empty vector");
    v->len--;
    if (out) memcpy(out, v->data + v->len * v->elem_size, v->elem_size);
}

size_t akt_vec_len(const AktVec *v) { return v->len; }

void akt_vec_clear(AktVec *v) { v->len = 0; }

AktOption akt_option_some(const void *value, size_t size) {
    AktOption o;
    o.is_some = true;
    o.size = size;
    o.value = size ? akt_alloc(size) : NULL;
    if (size) memcpy(o.value, value, size);
    return o;
}

AktOption akt_option_none(void) {
    AktOption o;
    o.is_some = false;
    o.value = NULL;
    o.size = 0;
    return o;
}

AktResult akt_result_ok(const void *value, size_t size) {
    AktResult r;
    r.is_ok = true;
    r.size = size;
    r.value = size ? akt_alloc(size) : NULL;
    if (size) memcpy(r.value, value, size);
    return r;
}

AktResult akt_result_err(const void *value, size_t size) {
    AktResult r;
    r.is_ok = false;
    r.size = size;
    r.value = size ? akt_alloc(size) : NULL;
    if (size) memcpy(r.value, value, size);
    return r;
}

AktResult akt_result_ok_unit(void) {
    AktResult r;
    r.is_ok = true;
    r.value = NULL;
    r.size = 0;
    return r;
}

AktMap akt_map_new(size_t value_size) {
    AktMap m;
    m.entries = NULL;
    m.len = 0;
    m.cap = 0;
    m.value_size = value_size ? value_size : 1;
    return m;
}

void akt_map_free(AktMap *m) {
    if (!m) return;
    for (size_t i = 0; i < m->len; i++) {
        akt_free((void *)m->entries[i].key.data);
        akt_free(m->entries[i].value);
    }
    akt_free(m->entries);
    m->entries = NULL;
    m->len = m->cap = 0;
}

static void akt_map_grow(AktMap *m) {
    if (m->len < m->cap) return;
    size_t cap = m->cap ? m->cap * 2 : 8;
    m->entries = (AktMapEntry *)akt_realloc(m->entries, cap * sizeof(AktMapEntry));
    m->cap = cap;
}

void akt_map_set(AktMap *m, AktString key, const void *value) {
    for (size_t i = 0; i < m->len; i++) {
        if (akt_str_eq(m->entries[i].key, key)) {
            memcpy(m->entries[i].value, value, m->value_size);
            return;
        }
    }
    akt_map_grow(m);
    char *kc = (char *)akt_alloc(key.len + 1);
    memcpy(kc, key.data, key.len);
    kc[key.len] = 0;
    m->entries[m->len].key = akt_str(kc, key.len);
    m->entries[m->len].value = akt_alloc(m->value_size);
    memcpy(m->entries[m->len].value, value, m->value_size);
    m->len++;
}

AktOption akt_map_get(const AktMap *m, AktString key) {
    for (size_t i = 0; i < m->len; i++) {
        if (akt_str_eq(m->entries[i].key, key)) {
            AktOption o;
            o.is_some = true;
            o.size = m->value_size;
            o.value = akt_alloc(m->value_size);
            memcpy(o.value, m->entries[i].value, m->value_size);
            return o;
        }
    }
    return akt_option_none();
}

int akt_map_contains(const AktMap *m, AktString key) {
    for (size_t i = 0; i < m->len; i++)
        if (akt_str_eq(m->entries[i].key, key)) return 1;
    return 0;
}

void akt_map_remove(AktMap *m, AktString key) {
    for (size_t i = 0; i < m->len; i++) {
        if (akt_str_eq(m->entries[i].key, key)) {
            akt_free((void *)m->entries[i].key.data);
            akt_free(m->entries[i].value);
            for (size_t j = i + 1; j < m->len; j++) m->entries[j - 1] = m->entries[j];
            m->len--;
            return;
        }
    }
}

size_t akt_map_len(const AktMap *m) { return m->len; }

AktSet akt_set_new(size_t elem_size) {
    AktSet s;
    s.items = akt_vec_new(elem_size);
    return s;
}

void akt_set_free(AktSet *s) {
    if (s) akt_vec_free(&s->items);
}

static int akt_set_find(const AktSet *s, const void *value) {
    for (size_t i = 0; i < s->items.len; i++) {
        if (memcmp(s->items.data + i * s->items.elem_size, value, s->items.elem_size) == 0)
            return (int)i;
    }
    return -1;
}

void akt_set_add(AktSet *s, const void *value) {
    if (akt_set_find(s, value) >= 0) return;
    akt_vec_push(&s->items, value);
}

int akt_set_contains(const AktSet *s, const void *value) {
    return akt_set_find(s, value) >= 0;
}

void akt_set_remove(AktSet *s, const void *value) {
    int i = akt_set_find(s, value);
    if (i < 0) return;
    for (size_t j = (size_t)i + 1; j < s->items.len; j++)
        memcpy(s->items.data + (j - 1) * s->items.elem_size,
               s->items.data + j * s->items.elem_size, s->items.elem_size);
    s->items.len--;
}

size_t akt_set_len(const AktSet *s) { return s->items.len; }

static AktJson akt_json_make(AktJsonType t) {
    AktJson j;
    memset(&j, 0, sizeof(j));
    j.type = t;
    j.array = akt_vec_new(sizeof(AktJson));
    j.keys = akt_vec_new(sizeof(AktString));
    j.values = akt_vec_new(sizeof(AktJson));
    return j;
}

AktJson akt_json_null(void) { return akt_json_make(AKT_JSON_NULL); }

typedef struct {
    const char *p;
    size_t n;
    size_t i;
    int ok;
} AktJParser;

static void jp_ws(AktJParser *p) {
    while (p->i < p->n && (p->p[p->i] == ' ' || p->p[p->i] == '\n' ||
                           p->p[p->i] == '\r' || p->p[p->i] == '\t')) p->i++;
}

static AktString jp_string(AktJParser *p) {
    if (p->i >= p->n || p->p[p->i] != '"') { p->ok = 0; return akt_str_cstr(""); }
    p->i++;
    size_t cap = 32, len = 0;
    char *out = (char *)akt_alloc(cap);
    while (p->i < p->n && p->p[p->i] != '"') {
        char c = p->p[p->i++];
        if (c == '\\' && p->i < p->n) {
            char e = p->p[p->i++];
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': {
                    unsigned v = 0;
                    for (int k = 0; k < 4 && p->i < p->n; k++) {
                        char h = p->p[p->i++];
                        v = v * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0' :
                                      h >= 'a' && h <= 'f' ? h - 'a' + 10 :
                                      h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
                    }
                    c = (char)(v < 128 ? v : '?');
                    break;
                }
                default: c = e; break;
            }
        }
        if (len + 2 >= cap) { cap *= 2; out = (char *)akt_realloc(out, cap); }
        out[len++] = c;
    }
    if (p->i >= p->n || p->p[p->i] != '"') p->ok = 0;
    else p->i++;
    out[len] = 0;
    return akt_str(out, len);
}

static AktJson jp_value(AktJParser *p, int depth);

static AktJson jp_array(AktJParser *p, int depth) {
    AktJson j = akt_json_make(AKT_JSON_ARRAY);
    p->i++;
    jp_ws(p);
    if (p->i < p->n && p->p[p->i] == ']') { p->i++; return j; }
    while (p->ok && p->i < p->n) {
        AktJson v = jp_value(p, depth + 1);
        akt_vec_push(&j.array, &v);
        jp_ws(p);
        if (p->i < p->n && p->p[p->i] == ',') { p->i++; continue; }
        if (p->i < p->n && p->p[p->i] == ']') { p->i++; break; }
        p->ok = 0;
    }
    return j;
}

static AktJson jp_object(AktJParser *p, int depth) {
    AktJson j = akt_json_make(AKT_JSON_OBJECT);
    p->i++;
    jp_ws(p);
    if (p->i < p->n && p->p[p->i] == '}') { p->i++; return j; }
    while (p->ok && p->i < p->n) {
        AktString k = jp_string(p);
        jp_ws(p);
        if (p->i >= p->n || p->p[p->i] != ':') { p->ok = 0; break; }
        p->i++;
        AktJson v = jp_value(p, depth + 1);
        akt_vec_push(&j.keys, &k);
        akt_vec_push(&j.values, &v);
        jp_ws(p);
        if (p->i < p->n && p->p[p->i] == ',') { p->i++; continue; }
        if (p->i < p->n && p->p[p->i] == '}') { p->i++; break; }
        p->ok = 0;
    }
    return j;
}

static AktJson jp_value(AktJParser *p, int depth) {
    if (depth > 128) { p->ok = 0; return akt_json_null(); }
    jp_ws(p);
    if (p->i >= p->n) { p->ok = 0; return akt_json_null(); }
    char c = p->p[p->i];
    if (c == '"') {
        AktJson j = akt_json_make(AKT_JSON_STRING);
        j.string = jp_string(p);
        return j;
    }
    if (c == '[') return jp_array(p, depth);
    if (c == '{') return jp_object(p, depth);
    if (c == 't' && p->i + 4 <= p->n && memcmp(p->p + p->i, "true", 4) == 0) {
        p->i += 4; AktJson j = akt_json_make(AKT_JSON_BOOL); j.boolean = true; return j;
    }
    if (c == 'f' && p->i + 5 <= p->n && memcmp(p->p + p->i, "false", 5) == 0) {
        p->i += 5; AktJson j = akt_json_make(AKT_JSON_BOOL); j.boolean = false; return j;
    }
    if (c == 'n' && p->i + 4 <= p->n && memcmp(p->p + p->i, "null", 4) == 0) {
        p->i += 4; return akt_json_null();
    }
    char *end = NULL;
    char buf[128];
    size_t start = p->i;
    while (p->i < p->n && p->i - start < sizeof(buf) - 1 && p->p[p->i] != ',' &&
           p->p[p->i] != ']' && p->p[p->i] != '}' && p->p[p->i] != ' ' &&
           p->p[p->i] != '\n' && p->p[p->i] != '\t' && p->p[p->i] != '\r')
        p->i++;
    size_t len = p->i - start;
    memcpy(buf, p->p + start, len);
    buf[len] = 0;
    double d = strtod(buf, &end);
    if (end == buf) { p->ok = 0; return akt_json_null(); }
    AktJson j = akt_json_make(AKT_JSON_NUMBER);
    j.number = d;
    return j;
}

AktJson akt_json_parse(AktString text) {
    AktJParser p;
    p.p = text.data;
    p.n = text.len;
    p.i = 0;
    p.ok = 1;
    AktJson v = jp_value(&p, 0);
    jp_ws(&p);
    if (!p.ok || p.i != p.n) return akt_json_null();
    return v;
}

AktJson akt_json_get(AktJson value, AktString key) {
    if (value.type != AKT_JSON_OBJECT) return akt_json_null();
    for (size_t i = 0; i < value.keys.len; i++) {
        AktString k = *(AktString *)akt_vec_get(&value.keys, i);
        if (akt_str_eq(k, key)) return *(AktJson *)akt_vec_get(&value.values, i);
    }
    return akt_json_null();
}

AktJson akt_json_at(AktJson value, size_t index) {
    if (value.type != AKT_JSON_ARRAY || index >= value.array.len) return akt_json_null();
    return *(AktJson *)akt_vec_get(&value.array, index);
}

size_t akt_json_len(AktJson value) {
    if (value.type == AKT_JSON_ARRAY) return value.array.len;
    if (value.type == AKT_JSON_OBJECT) return value.keys.len;
    if (value.type == AKT_JSON_STRING) return value.string.len;
    return 0;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} AktJBuf;

static void jb_put(AktJBuf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->data = (char *)akt_realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}
static void jb_cstr(AktJBuf *b, const char *s) { jb_put(b, s, strlen(s)); }

static void jb_string(AktJBuf *b, AktString s) {
    jb_cstr(b, "\"");
    for (size_t i = 0; i < s.len; i++) {
        char c = s.data[i];
        if (c == '"' || c == '\\') { char e[3] = {'\\', c, 0}; jb_put(b, e, 2); }
        else if (c == '\n') jb_cstr(b, "\\n");
        else if (c == '\r') jb_cstr(b, "\\r");
        else if (c == '\t') jb_cstr(b, "\\t");
        else jb_put(b, &c, 1);
    }
    jb_cstr(b, "\"");
}

static void jb_value(AktJBuf *b, AktJson v) {
    char tmp[64];
    switch (v.type) {
        case AKT_JSON_NULL: jb_cstr(b, "null"); break;
        case AKT_JSON_BOOL: jb_cstr(b, v.boolean ? "true" : "false"); break;
        case AKT_JSON_NUMBER:
            snprintf(tmp, sizeof(tmp), "%.15g", v.number);
            jb_cstr(b, tmp);
            break;
        case AKT_JSON_STRING: jb_string(b, v.string); break;
        case AKT_JSON_ARRAY:
            jb_cstr(b, "[");
            for (size_t i = 0; i < v.array.len; i++) {
                if (i) jb_cstr(b, ",");
                jb_value(b, *(AktJson *)akt_vec_get(&v.array, i));
            }
            jb_cstr(b, "]");
            break;
        case AKT_JSON_OBJECT:
            jb_cstr(b, "{");
            for (size_t i = 0; i < v.keys.len; i++) {
                if (i) jb_cstr(b, ",");
                jb_string(b, *(AktString *)akt_vec_get(&v.keys, i));
                jb_cstr(b, ":");
                jb_value(b, *(AktJson *)akt_vec_get(&v.values, i));
            }
            jb_cstr(b, "}");
            break;
    }
}

AktString akt_json_stringify(AktJson value) {
    AktJBuf b = {0};
    jb_value(&b, value);
    return akt_str(b.data ? b.data : "", b.len);
}

AktString akt_json_to_string(AktJson value) {
    if (value.type == AKT_JSON_STRING) return value.string;
    if (value.type == AKT_JSON_BOOL) return value.boolean ? akt_str_cstr("true") : akt_str_cstr("false");
    if (value.type == AKT_JSON_NUMBER) return akt_str_from_float(value.number);
    return akt_json_stringify(value);
}

static int rx_atom_len(const char *re) {
    if (re[0] == '\\' && re[1]) return 2;
    return 1;
}
static int rx_atom_match(const char *re, char c) {
    if (re[0] == '.') return c != 0;
    if (re[0] == '\\') {
        if (re[1] == 'd') return c >= '0' && c <= '9';
        if (re[1] == 'w') return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') || c == '_';
        if (re[1] == 's') return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        return c == re[1];
    }
    if (re[0] == '[') {
        const char *p = re + 1;
        int neg = 0;
        if (*p == '^') { neg = 1; p++; }
        int hit = 0;
        while (*p && *p != ']') {
            if (p[1] == '-' && p[2] && p[2] != ']') {
                if (c >= p[0] && c <= p[2]) hit = 1;
                p += 3;
            } else {
                if (c == *p) hit = 1;
                p++;
            }
        }
        return neg ? !hit : hit;
    }
    return c == re[0];
}
static int rx_atom_end(const char *re) {
    if (re[0] == '[') {
        const char *p = re + 1;
        while (*p && *p != ']') p++;
        return *p ? (int)(p - re + 1) : 1;
    }
    return rx_atom_len(re);
}
static int rx_here(const char *re, const char *text) {
    if (!*re) return *text == 0;
    if (*re == '^') return rx_here(re + 1, text);
    if (*re == '$' && re[1] == 0) return *text == 0;
    int alen = rx_atom_end(re);
    char q = re[alen];
    if (q == '*') {
        const char *p = text;
        while (*p && rx_atom_match(re, *p)) p++;
        while (p >= text) {
            if (rx_here(re + alen + 1, p)) return 1;
            p--;
        }
        return 0;
    }
    if (q == '+') {
        const char *p = text;
        if (!*p || !rx_atom_match(re, *p)) return 0;
        while (*p && rx_atom_match(re, *p)) p++;
        while (p > text) {
            if (rx_here(re + alen + 1, p)) return 1;
            p--;
        }
        return 0;
    }
    if (q == '?') {
        if (*text && rx_atom_match(re, *text) && rx_here(re + alen + 1, text + 1)) return 1;
        return rx_here(re + alen + 1, text);
    }
    if (*text && rx_atom_match(re, *text)) return rx_here(re + alen, text + 1);
    return 0;
}

int akt_regex_match(AktString pattern, AktString text) {
    char *p = akt_cstr_dup(pattern);
    char *t = akt_cstr_dup(text);
    int ok = 0;
    if (p[0] == '^') ok = rx_here(p, t);
    else for (char *s = t; ; s++) {
        if (rx_here(p, s)) { ok = 1; break; }
        if (!*s) break;
    }
    akt_free(p); akt_free(t);
    return ok;
}

AktString akt_regex_find(AktString pattern, AktString text) {
    char *p = akt_cstr_dup(pattern);
    char *t = akt_cstr_dup(text);
    size_t start = 0, end = 0;
    int found = 0;
    if (p[0] == '^') {
        for (end = 0; t[end]; end++) {
            char save = t[end + 1];
            t[end + 1] = 0;
            if (rx_here(p, t)) { found = 1; break; }
            t[end + 1] = save;
        }
    } else {
        for (char *s = t; *s; s++) {
            for (char *e = s; ; e++) {
                char save = *e;
                *e = 0;
                if (rx_here(p, s)) { start = (size_t)(s - t); end = (size_t)(e - t); found = 1; }
                *e = save;
                if (!*e) break;
            }
            if (found) break;
        }
    }
    AktString out = found ? akt_str_slice(text, start, end) : akt_str("", 0);
    akt_free(p); akt_free(t);
    return out;
}

AktString akt_regex_replace(AktString pattern, AktString text, AktString repl) {
    AktJBuf b = {0};
    size_t pos = 0;
    while (pos <= text.len) {
        AktString rest = akt_str_slice(text, pos, text.len);
        AktString m = akt_regex_find(pattern, rest);
        if (m.len == 0) {
            if (pos < text.len) jb_put(&b, text.data + pos, 1);
            pos++;
            continue;
        }
        size_t off = (size_t)(m.data - rest.data);
        if (off) jb_put(&b, rest.data, off);
        jb_put(&b, repl.data, repl.len);
        pos += off + m.len;
        if (m.len == 0) break;
    }
    return akt_str(b.data ? b.data : "", b.len);
}

static int akt_url_safe(const char *s) {
    if (strncmp(s, "http://", 7) != 0 && strncmp(s, "https://", 8) != 0) return 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == ':' || c == '/' || c == '?' || c == '&' || c == '=' ||
            c == '.' || c == '_' || c == '%' || c == '+' || c == '-' || c == '#' ||
            c == ';' || c == ',' || c == '~' || c == '@' || c == '!' || c == '$' ||
            c == '\'' || c == '(' || c == ')' || c == '*') continue;
        return 0;
    }
    return 1;
}

static AktString akt_run_curl(const char *cmd) {
#ifdef _WIN32
    FILE *p = _popen(cmd, "rb");
#else
    FILE *p = popen(cmd, "r");
#endif
    if (!p) return akt_str("", 0);
    size_t cap = 4096, len = 0;
    char *out = (char *)akt_alloc(cap);
    size_t n;
    while ((n = fread(out + len, 1, cap - len - 1, p)) > 0) {
        len += n;
        if (len + 1 >= cap) { cap *= 2; out = (char *)akt_realloc(out, cap); }
    }
    out[len] = 0;
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    return akt_str(out, len);
}

AktString akt_http_get(AktString url) {
    char *u = akt_cstr_dup(url);
    if (!akt_url_safe(u)) { akt_free(u); return akt_str("", 0); }
    char *cmd = (char *)akt_alloc(url.len + 64);
    snprintf(cmd, url.len + 64, "curl -sL --max-time 30 \"%s\"", u);
    AktString out = akt_run_curl(cmd);
    akt_free(cmd); akt_free(u);
    return out;
}

AktString akt_http_post(AktString url, AktString body) {
    char *u = akt_cstr_dup(url);
    char *b = akt_cstr_dup(body);
    if (!akt_url_safe(u)) { akt_free(u); akt_free(b); return akt_str("", 0); }
    char *cmd = (char *)akt_alloc(url.len + body.len + 96);
    snprintf(cmd, url.len + body.len + 96,
             "curl -sL --max-time 30 -X POST --data-binary \"%s\" \"%s\"", b, u);
    AktString out = akt_run_curl(cmd);
    akt_free(cmd); akt_free(u); akt_free(b);
    return out;
}

typedef void sqlite3;
typedef int (*sqlite3_open_fn)(const char *, sqlite3 **);
typedef int (*sqlite3_close_fn)(sqlite3 *);
typedef int (*sqlite3_exec_fn)(sqlite3 *, const char *, void *, void *, char **);
typedef int (*sqlite3_prepare_fn)(sqlite3 *, const char *, int, void **, const char **);
typedef int (*sqlite3_step_fn)(void *);
typedef int (*sqlite3_finalize_fn)(void *);
typedef int (*sqlite3_column_count_fn)(void *);
typedef const unsigned char *(*sqlite3_column_text_fn)(void *, int);

struct AktSqlite {
    void *handle;
    sqlite3 *db;
    sqlite3_exec_fn exec;
    sqlite3_prepare_fn prepare;
    sqlite3_step_fn step;
    sqlite3_finalize_fn finalize;
    sqlite3_column_count_fn column_count;
    sqlite3_column_text_fn column_text;
    sqlite3_close_fn close;
};

AktSqlite *akt_sqlite_open(AktString path) {
    void *h = NULL;
#ifdef _WIN32
    h = (void *)LoadLibraryA("winsqlite3.dll");
#else
    h = dlopen("libsqlite3.so.0", RTLD_LAZY);
    if (!h) h = dlopen("libsqlite3.so", RTLD_LAZY);
#endif
    if (!h) return NULL;
    AktSqlite *s = (AktSqlite *)akt_alloc(sizeof(AktSqlite));
    memset(s, 0, sizeof(*s));
    s->handle = h;
#ifdef _WIN32
#define AKT_SYM(name) GetProcAddress((HMODULE)h, name)
#else
#define AKT_SYM(name) dlsym(h, name)
#endif
    sqlite3_open_fn open_fn = (sqlite3_open_fn)AKT_SYM("sqlite3_open");
    s->exec = (sqlite3_exec_fn)AKT_SYM("sqlite3_exec");
    s->prepare = (sqlite3_prepare_fn)AKT_SYM("sqlite3_prepare_v2");
    if (!s->prepare) s->prepare = (sqlite3_prepare_fn)AKT_SYM("sqlite3_prepare");
    s->step = (sqlite3_step_fn)AKT_SYM("sqlite3_step");
    s->finalize = (sqlite3_finalize_fn)AKT_SYM("sqlite3_finalize");
    s->column_count = (sqlite3_column_count_fn)AKT_SYM("sqlite3_column_count");
    s->column_text = (sqlite3_column_text_fn)AKT_SYM("sqlite3_column_text");
    s->close = (sqlite3_close_fn)AKT_SYM("sqlite3_close");
#undef AKT_SYM
    if (!open_fn || !s->exec || !s->prepare || !s->step || !s->finalize ||
        !s->column_count || !s->column_text || !s->close) {
        akt_free(s);
        return NULL;
    }
    char *p = akt_cstr_dup(path);
    int rc = open_fn(p, &s->db);
    akt_free(p);
    if (rc != 0) { akt_free(s); return NULL; }
    return s;
}

int akt_sqlite_exec(AktSqlite *db, AktString sql) {
    if (!db || !db->db) return 0;
    char *q = akt_cstr_dup(sql);
    int rc = db->exec(db->db, q, NULL, NULL, NULL);
    akt_free(q);
    return rc == 0;
}

AktVec akt_sqlite_query(AktSqlite *db, AktString sql) {
    AktVec rows = akt_vec_new(sizeof(AktString));
    if (!db || !db->db) return rows;
    char *q = akt_cstr_dup(sql);
    void *stmt = NULL;
    if (db->prepare(db->db, q, -1, &stmt, NULL) != 0) { akt_free(q); return rows; }
    while (db->step(stmt) == 100) {
        int n = db->column_count(stmt);
        size_t cap = 64, len = 0;
        char *line = (char *)akt_alloc(cap);
        for (int i = 0; i < n; i++) {
            const unsigned char *v = db->column_text(stmt, i);
            const char *sv = v ? (const char *)v : "";
            size_t vl = strlen(sv);
            if (len + vl + 3 >= cap) {
                while (len + vl + 3 >= cap) cap *= 2;
                line = (char *)akt_realloc(line, cap);
            }
            if (i) line[len++] = '|';
            memcpy(line + len, sv, vl);
            len += vl;
        }
        line[len] = 0;
        AktString row = akt_str(line, len);
        akt_vec_push(&rows, &row);
    }
    db->finalize(stmt);
    akt_free(q);
    return rows;
}

void akt_sqlite_close(AktSqlite *db) {
    if (!db) return;
    if (db->db && db->close) db->close(db->db);
    akt_free(db);
}

struct AktMutex {
#ifdef _WIN32
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t m;
#endif
};

struct AktAtomic {
#ifdef _WIN32
    volatile LONG64 v;
#else
    _Atomic long long v;
#endif
};

struct AktShared {
#ifdef _WIN32
    volatile LONG refs;
    volatile LONG weaks;
#else
    _Atomic int refs;
    _Atomic int weaks;
#endif
    void *data;
    size_t size;
};

AktShared *akt_shared_new(size_t size, const void *value) {
    AktShared *s = (AktShared *)akt_alloc(sizeof(AktShared));
#ifdef _WIN32
    s->refs = 1;
    s->weaks = 1;
#else
    atomic_init(&s->refs, 1);
    atomic_init(&s->weaks, 1);
#endif
    s->size = size;
    s->data = size ? akt_alloc(size) : NULL;
    if (size && value) memcpy(s->data, value, size);
    else if (size) memset(s->data, 0, size);
    return s;
}

void *akt_shared_get(AktShared *s) { return s ? s->data : NULL; }

void akt_shared_ref(AktShared *s) {
    if (!s) return;
#ifdef _WIN32
    InterlockedIncrement(&s->refs);
#else
    atomic_fetch_add(&s->refs, 1);
#endif
}

void akt_shared_unref(AktShared *s) {
    if (!s) return;
#ifdef _WIN32
    long r = InterlockedDecrement(&s->refs);
#else
    int r = atomic_fetch_sub(&s->refs, 1) - 1;
#endif
    if (r == 0) {
        akt_free(s->data);
        s->data = NULL;
        akt_shared_weak_unref(s);
    }
}

void akt_shared_weak_ref(AktShared *s) {
    if (!s) return;
#ifdef _WIN32
    InterlockedIncrement(&s->weaks);
#else
    atomic_fetch_add(&s->weaks, 1);
#endif
}

void akt_shared_weak_unref(AktShared *s) {
    if (!s) return;
#ifdef _WIN32
    long w = InterlockedDecrement(&s->weaks);
#else
    int w = atomic_fetch_sub(&s->weaks, 1) - 1;
#endif
    if (w == 0) akt_free(s);
}

int akt_shared_try_get(AktShared *s, void *out, size_t size) {
    if (!s || !out) return 0;
    for (;;) {
#ifdef _WIN32
        LONG r = s->refs;
        if (r <= 0) return 0;
        akt_shared_weak_ref(s);
        if (InterlockedCompareExchange(&s->refs, r + 1, r) == r) {
#else
        int r = atomic_load(&s->refs);
        if (r <= 0) return 0;
        akt_shared_weak_ref(s);
        if (atomic_compare_exchange_strong(&s->refs, &r, r + 1)) {
#endif
            akt_shared_weak_unref(s);
            break;
        }
        akt_shared_weak_unref(s);
    }
    if (s->data && size <= s->size) memcpy(out, s->data, size);
    else memset(out, 0, size);
    akt_shared_unref(s);
    return 1;
}

AktMutex *akt_mutex_new(void) {
    AktMutex *m = (AktMutex *)akt_alloc(sizeof(AktMutex));
#ifdef _WIN32
    InitializeCriticalSection(&m->cs);
#else
    pthread_mutex_init(&m->m, NULL);
#endif
    return m;
}

void akt_mutex_free(AktMutex *m) {
    if (!m) return;
#ifdef _WIN32
    DeleteCriticalSection(&m->cs);
#else
    pthread_mutex_destroy(&m->m);
#endif
    akt_free(m);
}

void akt_mutex_lock(AktMutex *m) {
#ifdef _WIN32
    EnterCriticalSection(&m->cs);
#else
    pthread_mutex_lock(&m->m);
#endif
}

void akt_mutex_unlock(AktMutex *m) {
#ifdef _WIN32
    LeaveCriticalSection(&m->cs);
#else
    pthread_mutex_unlock(&m->m);
#endif
}

AktAtomic *akt_atomic_new(akt_i64 value) {
    AktAtomic *a = (AktAtomic *)akt_alloc(sizeof(AktAtomic));
#ifdef _WIN32
    a->v = value;
#else
    atomic_init(&a->v, value);
#endif
    return a;
}

void akt_atomic_free(AktAtomic *a) { akt_free(a); }

akt_i64 akt_atomic_load(AktAtomic *a) {
#ifdef _WIN32
    return (akt_i64)InterlockedCompareExchange64(&a->v, 0, 0);
#else
    return (akt_i64)atomic_load(&a->v);
#endif
}

void akt_atomic_store(AktAtomic *a, akt_i64 value) {
#ifdef _WIN32
    InterlockedExchange64(&a->v, value);
#else
    atomic_store(&a->v, value);
#endif
}

akt_i64 akt_atomic_add(AktAtomic *a, akt_i64 value) {
#ifdef _WIN32
    return (akt_i64)InterlockedExchangeAdd64(&a->v, value);
#else
    return (akt_i64)atomic_fetch_add(&a->v, value);
#endif
}

#ifdef _WIN32
typedef struct {
    AktThreadFn fn;
    void *arg;
} AktThreadStart;
static DWORD WINAPI akt_thread_entry(LPVOID p) {
    AktThreadStart *s = (AktThreadStart *)p;
    s->fn(s->arg);
    akt_free(s);
    return 0;
}
struct AktThread {
    HANDLE h;
};
#else
typedef struct {
    AktThreadFn fn;
    void *arg;
} AktThreadStart;
static void *akt_thread_entry(void *p) {
    AktThreadStart *s = (AktThreadStart *)p;
    s->fn(s->arg);
    akt_free(s);
    return NULL;
}
struct AktThread {
    pthread_t t;
};
#endif

AktThread *akt_thread_spawn(AktThreadFn fn, void *arg) {
    AktThread *t = (AktThread *)akt_alloc(sizeof(AktThread));
    AktThreadStart *s = (AktThreadStart *)akt_alloc(sizeof(AktThreadStart));
    s->fn = fn;
    s->arg = arg;
#ifdef _WIN32
    t->h = CreateThread(NULL, 0, akt_thread_entry, s, 0, NULL);
    if (!t->h) akt_panic("cannot create thread");
#else
    if (pthread_create(&t->t, NULL, akt_thread_entry, s) != 0)
        akt_panic("cannot create thread");
#endif
    return t;
}

void akt_thread_join(AktThread *t) {
    if (!t) return;
#ifdef _WIN32
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
#else
    pthread_join(t->t, NULL);
#endif
    akt_free(t);
}

void akt_thread_sleep_ms(akt_i64 ms) { akt_sleep_ms(ms); }

struct AktTask {
    AktThread *thread;
    void *result;
    volatile int done;
};

typedef struct {
    AktTaskFn fn;
    void *arg;
    AktTask *task;
} AktTaskStart;

static void akt_task_entry(void *p) {
    AktTaskStart *s = (AktTaskStart *)p;
    s->task->result = s->fn(s->arg);
    s->task->done = 1;
    akt_free(s);
}

AktTask *akt_task_start(AktTaskFn fn, void *arg) {
    AktTask *t = (AktTask *)akt_alloc(sizeof(AktTask));
    t->result = NULL;
    t->done = 0;
    AktTaskStart *s = (AktTaskStart *)akt_alloc(sizeof(AktTaskStart));
    s->fn = fn;
    s->arg = arg;
    s->task = t;
    t->thread = akt_thread_spawn(akt_task_entry, s);
    return t;
}

void *akt_task_wait(AktTask *t) {
    if (!t) return NULL;
    akt_thread_join(t->thread);
    return t->result;
}

void *akt_task_wait_timeout(AktTask *t, akt_i64 ms) {
    if (!t) return NULL;
    akt_i64 start = akt_now_ms();
    while (!t->done) {
        if (ms >= 0 && akt_now_ms() - start >= ms) return NULL;
        akt_sleep_ms(1);
    }
    akt_thread_join(t->thread);
    return t->result;
}

int akt_task_wait_any(AktTask **tasks, int count) {
    for (;;) {
        for (int i = 0; i < count; i++)
            if (tasks[i] && tasks[i]->done) return i;
        akt_sleep_ms(1);
    }
}

void akt_task_free(AktTask *t) { if (t) akt_free(t); }

AktString akt_read_file(const char *path, int *ok) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (ok) *ok = 0;
        return akt_str_cstr("");
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (ok) *ok = 0;
        return akt_str_cstr("");
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        if (ok) *ok = 0;
        return akt_str_cstr("");
    }
    rewind(f);
    char *p = (char *)akt_alloc((size_t)n + 1);
    size_t got = fread(p, 1, (size_t)n, f);
    fclose(f);
    p[got] = 0;
    if (ok) *ok = 1;
    return akt_str(p, got);
}

int akt_write_file(const char *path, AktString data) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = fwrite(data.data, 1, data.len, f);
    fclose(f);
    return n == data.len;
}

int akt_file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

AktVec akt_fs_list_dir(AktString path) {
    AktVec out = akt_vec_new(sizeof(AktString));
    char *p = akt_cstr_dup(path);
#ifdef _WIN32
    char pat[4096];
    snprintf(pat, sizeof(pat), "%s\\*", p);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            size_t n = strlen(fd.cFileName);
            char *copy = (char *)akt_alloc(n + 1);
            memcpy(copy, fd.cFileName, n + 1);
            AktString s = akt_str(copy, n);
            akt_vec_push(&out, &s);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(p);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            size_t n = strlen(e->d_name);
            char *copy = (char *)akt_alloc(n + 1);
            memcpy(copy, e->d_name, n + 1);
            AktString s = akt_str(copy, n);
            akt_vec_push(&out, &s);
        }
        closedir(d);
    }
#endif
    akt_free(p);
    return out;
}

static void akt_walk_into(AktVec *out, AktString base, int depth) {
    if (depth > 32) return;
    AktVec entries = akt_fs_list_dir(base);
    for (size_t i = 0; i < entries.len; i++) {
        AktString name = *(AktString *)akt_vec_get(&entries, i);
        AktString full = akt_path_join(base, name);
        char *c = akt_cstr_dup(full);
        int is_dir = 0;
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(c);
        is_dir = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
        struct stat st;
        is_dir = stat(c, &st) == 0 && S_ISDIR(st.st_mode);
#endif
        akt_free(c);
        if (is_dir) {
#ifdef _WIN32
            if (name.len && name.data[0] == '.') continue;
#endif
            akt_walk_into(out, full, depth + 1);
        } else {
            akt_vec_push(out, &full);
        }
    }
    akt_vec_free(&entries);
}

AktVec akt_fs_walk(AktString path) {
    AktVec out = akt_vec_new(sizeof(AktString));
    akt_walk_into(&out, path, 0);
    return out;
}

akt_i64 akt_process_pid(void) {
#ifdef _WIN32
    return (akt_i64)GetCurrentProcessId();
#else
    return (akt_i64)getpid();
#endif
}

void akt_process_exit(int code) { exit(code); }

static int akt_rand_seeded = 0;
static void akt_rand_seed(void) {
    if (!akt_rand_seeded) {
        srand((unsigned)(akt_now_ms() ^ (akt_i64)time(NULL)));
        akt_rand_seeded = 1;
    }
}

akt_i64 akt_random_int(akt_i64 max) {
    akt_rand_seed();
    if (max <= 0) return 0;
    return (akt_i64)(rand() % (int)max);
}

akt_f64 akt_random_float(void) {
    akt_rand_seed();
    return (akt_f64)rand() / (akt_f64)RAND_MAX;
}

akt_u64 akt_hash_fnv1a(AktString s) {
    akt_u64 h = 14695981039346656037ULL;
    for (size_t i = 0; i < s.len; i++) {
        h ^= (unsigned char)s.data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static const char b64tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

AktString akt_base64_encode(AktString s) {
    size_t n = ((s.len + 2) / 3) * 4;
    char *p = (char *)akt_alloc(n + 1);
    size_t o = 0;
    for (size_t i = 0; i < s.len; i += 3) {
        unsigned v = (unsigned char)s.data[i] << 16;
        if (i + 1 < s.len) v |= (unsigned char)s.data[i + 1] << 8;
        if (i + 2 < s.len) v |= (unsigned char)s.data[i + 2];
        p[o++] = b64tab[(v >> 18) & 63];
        p[o++] = b64tab[(v >> 12) & 63];
        p[o++] = i + 1 < s.len ? b64tab[(v >> 6) & 63] : '=';
        p[o++] = i + 2 < s.len ? b64tab[v & 63] : '=';
    }
    p[o] = 0;
    return akt_str(p, o);
}

static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

AktString akt_base64_decode(AktString s) {
    char *p = (char *)akt_alloc(s.len + 1);
    size_t o = 0;
    unsigned v = 0;
    int bits = 0;
    for (size_t i = 0; i < s.len; i++) {
        if (s.data[i] == '=') break;
        int d = b64val(s.data[i]);
        if (d < 0) continue;
        v = (v << 6) | (unsigned)d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            p[o++] = (char)((v >> bits) & 0xFF);
        }
    }
    p[o] = 0;
    return akt_str(p, o);
}

AktString akt_env_get(const char *name, int *ok) {
    const char *v = getenv(name);
    if (!v) {
        if (ok) *ok = 0;
        return akt_str_cstr("");
    }
    if (ok) *ok = 1;
    return akt_str_cstr(v);
}

AktString akt_get_cwd(void) {
    char buf[4096];
#ifdef _WIN32
    if (_getcwd(buf, sizeof(buf))) return akt_str_cstr(buf);
#else
    if (getcwd(buf, sizeof(buf))) return akt_str_cstr(buf);
#endif
    return akt_str_cstr("");
}

akt_i64 akt_now_ms(void) {
#ifdef _WIN32
    return (akt_i64)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (akt_i64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

void akt_sleep_ms(akt_i64 ms) {
    if (ms <= 0) return;
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000);
    nanosleep(&ts, NULL);
#endif
}

void akt_panic(const char *message) {
    fflush(stdout);
    fprintf(stderr, "akt panic: %s\n", message ? message : "unknown error");
    exit(1);
}

void akt_assert_fail(const char *expr, const char *file, int line) {
    fflush(stdout);
    fprintf(stderr, "%s:%d: assertion failed: %s\n", file ? file : "<unknown>", line,
            expr ? expr : "?");
    exit(1);
}
