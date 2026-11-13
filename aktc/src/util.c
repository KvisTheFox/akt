#include "aktc.h"

#include <stdarg.h>

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "aktc: out of memory\n");
        exit(1);
    }
    return p;
}

void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n ? n : 1, s ? s : 1);
    if (!p) {
        fprintf(stderr, "aktc: out of memory\n");
        exit(1);
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fprintf(stderr, "aktc: out of memory\n");
        exit(1);
    }
    return q;
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = (char *)xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

char *xstrndup(const char *s, size_t n) {
    char *p = (char *)xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

char *xasprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return xstrdup("");
    }
    char *p = (char *)xmalloc((size_t)n + 1);
    vsnprintf(p, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return p;
}

void fatal_at(const char *file, int line, int col, const char *fmt, ...) {
    fprintf(stderr, "%s:%d:%d: error: ", file ? file : "<input>", line, col);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void fatal(const char *fmt, ...) {
    fprintf(stderr, "aktc: error: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

Type *type_new(TypeKind kind, const char *name) {
    Type *t = (Type *)xcalloc(1, sizeof(Type));
    t->kind = kind;
    t->name = name ? xstrdup(name) : NULL;
    return t;
}

Type *type_named(const char *name) {
    struct { const char *name; TypeKind kind; } m[] = {
        {"void", TY_VOID}, {"int", TY_INT}, {"i8", TY_I8}, {"i16", TY_I16},
        {"i32", TY_I32}, {"i64", TY_I64}, {"u8", TY_U8}, {"u16", TY_U16},
        {"u32", TY_U32}, {"u64", TY_U64}, {"f32", TY_F32}, {"f64", TY_F64},
        {"bool", TY_BOOL}, {"string", TY_STRING}, {"char", TY_CHAR},
        {"cchar", TY_CCHAR}, {NULL, TY_UNKNOWN}
    };
    for (int i = 0; m[i].name; i++) {
        if (strcmp(m[i].name, name) == 0) return type_new(m[i].kind, name);
    }
    Type *alias = type_alias_find(name);
    if (alias) return alias;
    return type_new(TY_NAMED, name);
}

typedef struct {
    char *name;
    Type *type;
} TypeAlias;

static TypeAlias *g_aliases = NULL;
static int g_naliases = 0;

void type_alias_define(const char *name, Type *type) {
    if (!name || !type) return;
    for (int i = 0; i < g_naliases; i++)
        if (strcmp(g_aliases[i].name, name) == 0) { g_aliases[i].type = type; return; }
    g_aliases = (TypeAlias *)xrealloc(g_aliases, (size_t)(g_naliases + 1) * sizeof(TypeAlias));
    g_aliases[g_naliases].name = xstrdup(name);
    g_aliases[g_naliases].type = type;
    g_naliases++;
}

Type *type_alias_find(const char *name) {
    for (int i = g_naliases - 1; i >= 0; i--)
        if (strcmp(g_aliases[i].name, name) == 0) return g_aliases[i].type;
    return NULL;
}

char *type_c_text(Type *t) {
    if (!t) return xstrdup("void");
    switch (t->kind) {
        case TY_VOID: return xstrdup("void");
        case TY_INT: return xstrdup("int");
        case TY_I8: return xstrdup("int8_t");
        case TY_I16: return xstrdup("int16_t");
        case TY_I32: return xstrdup("int32_t");
        case TY_I64: return xstrdup("int64_t");
        case TY_U8: return xstrdup("uint8_t");
        case TY_U16: return xstrdup("uint16_t");
        case TY_U32: return xstrdup("uint32_t");
        case TY_U64: return xstrdup("uint64_t");
        case TY_F32: return xstrdup("float");
        case TY_F64: return xstrdup("double");
        case TY_BOOL: return xstrdup("bool");
        case TY_STRING: return xstrdup("AktString");
        case TY_CHAR: return xstrdup("uint32_t");
        case TY_CCHAR: return xstrdup("char");
        case TY_CFUNC: return xstrdup(t->name ? t->name : "void (*)(void)");
        case TY_PTR:
        case TY_REF: {
            char *inner = type_c_text(t->elem);
            char *s = xasprintf("%s%s*", t->is_const ? "const " : "", inner);
            free(inner);
            return s;
        }
        case TY_NAMED:
            if (t->name && strcmp(t->name, "cstring") == 0) return xstrdup("const char*");
            if (t->name && strcmp(t->name, "size") == 0) return xstrdup("size_t");
            if (t->name && strcmp(t->name, "isize") == 0) return xstrdup("ptrdiff_t");
            return xstrdup(t->name ? t->name : "void");
        default:
            return xstrdup("void");
    }
}

Type *type_cfunc(Type *ret, Type **params, int nparams) {
    char *args = NULL;
    for (int i = 0; i < nparams; i++) {
        char *pt = type_c_text(params[i]);
        char *n = xasprintf("%s%s%s", args ? args : "", i ? ", " : "", pt);
        free(pt);
        free(args);
        args = n;
    }
    char *rt = type_c_text(ret);
    char *sig = xasprintf("%s (*)(%s)", rt, nparams > 0 ? args : "void");
    Type *t = type_new(TY_CFUNC, sig);
    t->elem = ret;
    free(rt);
    free(args);
    free(sig);
    return t;
}

char *type_c_decl(Type *t, const char *name) {
    if (t && t->kind == TY_CFUNC && t->name) {
        const char *p = strstr(t->name, "(*)");
        if (p) return xasprintf("%.*s(*%s)%s", (int)(p - t->name), t->name, name, p + 3);
    }
    char *ct = type_c_text(t);
    char *r = xasprintf("%s %s", ct, name);
    free(ct);
    return r;
}

Type *type_array(Type *elem, long long len) {
    Type *t = type_new(TY_ARRAY, NULL);
    t->elem = elem;
    t->array_len = len;
    return t;
}

Type *type_slice(Type *elem) {
    Type *t = type_new(TY_SLICE, NULL);
    t->elem = elem;
    return t;
}

Type *type_ptr(Type *elem) {
    Type *t = type_new(TY_PTR, NULL);
    t->elem = elem;
    return t;
}

Type *type_ref(Type *elem) {
    Type *t = type_new(TY_REF, NULL);
    t->elem = elem;
    return t;
}

Type *type_vector(Type *elem) {
    Type *t = type_new(TY_VECTOR, "Vector");
    t->elem = elem;
    return t;
}

Type *type_map(Type *key, Type *value) {
    Type *t = type_new(TY_MAP, "Map");
    t->elem = key;
    t->elem2 = value;
    return t;
}

Type *type_set(Type *elem) {
    Type *t = type_new(TY_SET, "Set");
    t->elem = elem;
    return t;
}

Type *type_shared(Type *elem) {
    Type *t = type_new(TY_SHARED, "shared");
    t->elem = elem;
    return t;
}

Type *type_weak(Type *elem) {
    Type *t = type_new(TY_WEAK, "weak");
    t->elem = elem;
    return t;
}

Type *type_task(Type *elem) {
    Type *t = type_new(TY_TASK, "Task");
    t->elem = elem;
    return t;
}

Type *type_clone(const Type *t) {
    if (!t) return NULL;
    Type *n = type_new(t->kind, t->name);
    n->elem = type_clone(t->elem);
    n->elem2 = type_clone(t->elem2);
    n->array_len = t->array_len;
    n->is_const = t->is_const;
    return n;
}

const char *type_name(const Type *t) {
    if (!t) return "<auto>";
    switch (t->kind) {
        case TY_VOID: return "void";
        case TY_INT: return "int";
        case TY_I8: return "i8";
        case TY_I16: return "i16";
        case TY_I32: return "i32";
        case TY_I64: return "i64";
        case TY_U8: return "u8";
        case TY_U16: return "u16";
        case TY_U32: return "u32";
        case TY_U64: return "u64";
        case TY_F32: return "f32";
        case TY_F64: return "f64";
        case TY_BOOL: return "bool";
        case TY_STRING: return "string";
        case TY_CHAR: return "char";
        case TY_CCHAR: return "cchar";
        case TY_NAMED: return t->name ? t->name : "<named>";
        case TY_ARRAY: return "array";
        case TY_SLICE: return "slice";
        case TY_PTR: return "pointer";
        case TY_REF: return "reference";
        case TY_VECTOR: return "Vector";
        case TY_OPTION: return "Option";
        case TY_RESULT: return "Result";
        case TY_MAP: return "Map";
        case TY_SET: return "Set";
        case TY_SHARED: return "shared";
        case TY_WEAK: return "weak";
        case TY_TASK: return "Task";
        default: return "<unknown>";
    }
}

int type_is_int(const Type *t) {
    if (!t) return 0;
    return t->kind >= TY_INT && t->kind <= TY_U64;
}

int type_is_float(const Type *t) {
    return t && (t->kind == TY_F32 || t->kind == TY_F64);
}

int type_is_number(const Type *t) {
    return type_is_int(t) || type_is_float(t);
}

int type_is_string(const Type *t) {
    return t && t->kind == TY_STRING;
}

int type_is_cstring(const Type *t) {
    if (!t) return 0;
    if (t->kind == TY_NAMED && t->name && strcmp(t->name, "cstring") == 0) return 1;
    if (t->kind == TY_PTR && t->elem && t->elem->kind == TY_CCHAR) return 1;
    return 0;
}

int type_is_bool(const Type *t) {
    return t && t->kind == TY_BOOL;
}

int type_is_void(const Type *t) {
    return !t || t->kind == TY_VOID;
}

int type_equal(const Type *a, const Type *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    if (a->kind == TY_NAMED) {
        return a->name && b->name && strcmp(a->name, b->name) == 0;
    }
    if (!type_equal(a->elem, b->elem)) return 0;
    if (!type_equal(a->elem2, b->elem2)) return 0;
    if (a->kind == TY_ARRAY && a->array_len != b->array_len) return 0;
    return 1;
}
