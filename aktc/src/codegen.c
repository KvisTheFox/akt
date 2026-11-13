#include "aktc.h"

#include <ctype.h>
#include <stdarg.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

static void bput(Buf *b, const char *s) {
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->data = (char *)xrealloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}
static void bprintf(Buf *b, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    if (b->len + (size_t)n + 1 > b->cap) {
        b->cap = (b->len + (size_t)n + 1) * 2;
        b->data = (char *)xrealloc(b->data, b->cap);
    }
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
}
static void bfree(Buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

typedef struct DeferFrame {
    Stmt **items;
    int count;
    int cap;
    struct DeferFrame *prev;
} DeferFrame;

typedef struct {
    char *name;
    Type *type;
} GVar;

typedef struct {
    Expr *closure;
    char name[32];
    GVar caps[128];
    int ncap;
} ThrRec;

typedef struct {
    FILE *out;
    Program *prog;
    Func *current;
    int indent;
    DeferFrame *defers;
    int tmp;
    GVar vars[256];
    int nvars;
    char *moved[128];
    int nmoved;
    int varmarks[64];
    int nmarks;
    ThrRec thr[64];
    int nthr;
} Gen;

static void record_var(Gen *g, const char *name, Type *type) {
    if (!name || !type || g->nvars >= 256) return;
    g->vars[g->nvars].name = xstrdup(name);
    g->vars[g->nvars].type = type;
    g->nvars++;
}

static Type *decl_type(Gen *g, const char *name) {
    if (!name) return NULL;
    for (int i = g->nvars - 1; i >= 0; i--)
        if (strcmp(g->vars[i].name, name) == 0) return g->vars[i].type;
    return NULL;
}

static void emit(Gen *g, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g->out, fmt, ap);
    va_end(ap);
}
static void line(Gen *g, const char *fmt, ...) {
    for (int i = 0; i < g->indent; i++) fputs("    ", g->out);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g->out, fmt, ap);
    va_end(ap);
    fputc('\n', g->out);
}

static const char *g_managed[128];
static int g_nmanaged = 0;

static int is_managed_name(const char *n) {
    if (!n) return 0;
    for (int i = 0; i < g_nmanaged; i++)
        if (strcmp(g_managed[i], n) == 0) return 1;
    return 0;
}

static StructDecl *find_struct_cg(Program *p, const char *name);

static const char *c_type(Type *t) {
    if (!t) return "void";
    switch (t->kind) {
        case TY_VOID: return "void";
        case TY_INT: return "int";
        case TY_I8: return "int8_t";
        case TY_I16: return "int16_t";
        case TY_I32: return "int32_t";
        case TY_I64: return "int64_t";
        case TY_U8: return "uint8_t";
        case TY_U16: return "uint16_t";
        case TY_U32: return "uint32_t";
        case TY_U64: return "uint64_t";
        case TY_F32: return "float";
        case TY_F64: return "double";
        case TY_BOOL: return "bool";
        case TY_STRING: return "AktString";
        case TY_CHAR: return "uint32_t";
        case TY_CCHAR: return "char";
        case TY_NAMED:
            if (t->name && is_managed_name(t->name)) return "AktShared*";
            if (t->name && strcmp(t->name, "arena") == 0) return "AktArena";
            if (t->name && strcmp(t->name, "cstring") == 0) return "const char*";
            if (t->name && strcmp(t->name, "size") == 0) return "size_t";
            if (t->name && strcmp(t->name, "isize") == 0) return "ptrdiff_t";
            if (t->name && strcmp(t->name, "Mutex") == 0) return "AktMutex*";
            if (t->name && strcmp(t->name, "Atomic") == 0) return "AktAtomic*";
            if (t->name && strcmp(t->name, "Json") == 0) return "AktJson";
            if (t->name && strcmp(t->name, "Handle") == 0) return "AktThread*";
            if (t->name && strcmp(t->name, "Sqlite") == 0) return "AktSqlite*";
            if (t->name) {
                const char *dot = strrchr(t->name, '.');
                return dot ? dot + 1 : t->name;
            }
            return "void";
        case TY_PTR:
        case TY_REF: {
            static char buf[8][256];
            static int slot = 0;
            char *s = buf[slot++ & 7];
            snprintf(s, 256, "%s%s*", t->is_const ? "const " : "", c_type(t->elem));
            return s;
        }
        case TY_VECTOR: return "AktVec";
        case TY_OPTION: return "AktOption";
        case TY_RESULT: return "AktResult";
        case TY_MAP: return "AktMap";
        case TY_SET: return "AktSet";
        case TY_SHARED: return "AktShared*";
        case TY_WEAK: return "AktShared*";
        case TY_TASK: return "AktTask*";
        case TY_CFUNC: return t->name ? t->name : "void (*)(void)";
        default: return "void";
    }
}

static void c_escape(Buf *b, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '\n': bput(b, "\\n"); break;
            case '\t': bput(b, "\\t"); break;
            case '\r': bput(b, "\\r"); break;
            case '"': bput(b, "\\\""); break;
            case '\\': bput(b, "\\\\"); break;
            default:
                if (*p >= 32 && *p < 127) {
                    char t[2] = {(char)*p, 0};
                    bput(b, t);
                } else {
                    bprintf(b, "\\x%02x", *p);
                }
                break;
        }
    }
}

static char *gen_expr(Gen *g, Expr *e);
static void gen_stmt(Gen *g, Stmt *s);
static void defer_push(Gen *g, Stmt *s);

static Expr *mk_ident(const char *name) {
    Expr *e = (Expr *)xcalloc(1, sizeof(Expr));
    e->kind = EX_IDENT;
    e->as.sval = xstrdup(name);
    return e;
}
static Expr *mk_amp(Expr *x) {
    Expr *e = (Expr *)xcalloc(1, sizeof(Expr));
    e->kind = EX_UNARY;
    e->as.unary.op = TK_AMP;
    e->as.unary.expr = x;
    return e;
}
static Stmt *mk_call_stmt1(const char *fn, Expr *arg) {
    Expr *callee = mk_ident(fn);
    Expr *call = (Expr *)xcalloc(1, sizeof(Expr));
    call->kind = EX_CALL;
    call->as.call.callee = callee;
    call->as.call.args = (Expr **)xcalloc(1, sizeof(Expr *));
    call->as.call.args[0] = arg;
    call->as.call.argc = 1;
    Stmt *s = (Stmt *)xcalloc(1, sizeof(Stmt));
    s->kind = ST_EXPR;
    s->as.expr = call;
    return s;
}

static char *str_literal(const char *s) {
    Buf b = {0};
    bprintf(&b, "akt_str(\"");
    c_escape(&b, s);
    bprintf(&b, "\", %zu)", strlen(s));
    return b.data;
}

static char *to_string_expr(Gen *g, Expr *e, int prec) {
    Buf b = {0};
    Type *t = e->type;
    if (!t) { free(gen_expr(g, e)); bput(&b, "akt_str_cstr(\"\")"); return b.data; }
    if (t->kind == TY_STRING) {
        return gen_expr(g, e);
    }
    if (type_is_int(t) || (t->kind == TY_NAMED && t->name &&
                           (strcmp(t->name, "size") == 0 || strcmp(t->name, "isize") == 0))) {
        bprintf(&b, "akt_str_from_int((int64_t)(%s))", gen_expr(g, e));
    } else if (type_is_float(t)) {
        if (prec >= 0)
            bprintf(&b, "akt_str_from_float_prec((double)(%s), %d)", gen_expr(g, e), prec);
        else
            bprintf(&b, "akt_str_from_float((double)(%s))", gen_expr(g, e));
    } else if (t->kind == TY_BOOL) {
        bprintf(&b, "akt_str_from_bool((bool)(%s))", gen_expr(g, e));
    } else if (t->kind == TY_CHAR) {
        bprintf(&b, "akt_str_from_char((uint32_t)(%s))", gen_expr(g, e));
    } else if (t->kind == TY_NAMED && t->name && strcmp(t->name, "Json") == 0) {
        bprintf(&b, "akt_json_to_string(%s)", gen_expr(g, e));
    } else {
        bprintf(&b, "akt_str_cstr(\"<value>\")");
    }
    return b.data;
}

static char *gen_fstring(Gen *g, Expr *e) {
    char *acc = str_literal(e->as.fstring.parts[0]);
    for (int i = 0; i < e->as.fstring.nparts - 1; i++) {
        char *val = to_string_expr(g, e->as.fstring.exprs[i],
                                   e->as.fstring.prec ? e->as.fstring.prec[i] : -1);
        char *next = xasprintf("akt_str_concat(%s, %s)", acc, val);
        free(acc);
        free(val);
        acc = next;
        char *part = str_literal(e->as.fstring.parts[i + 1]);
        next = xasprintf("akt_str_concat(%s, %s)", acc, part);
        free(part);
        free(acc);
        acc = next;
    }
    return acc;
}

static char *gen_binary(Gen *g, Expr *e) {
    Buf b = {0};
    Expr *l = e->as.binary.left, *r = e->as.binary.right;
    int op = e->as.binary.op;
    char *ls = gen_expr(g, l);
    char *rs = gen_expr(g, r);
    Type *lt = l->type, *rt = r->type;
    int string = (lt && lt->kind == TY_STRING) || (rt && rt->kind == TY_STRING);
    if (op == TK_PLUS && string) {
        bprintf(&b, "akt_str_concat(%s, %s)", ls, rs);
    } else if (op == TK_EQEQ && string) {
        bprintf(&b, "akt_str_eq(%s, %s)", ls, rs);
    } else if (op == TK_NEQ && string) {
        bprintf(&b, "!akt_str_eq(%s, %s)", ls, rs);
    } else if (op == TK_LT && string) {
        bprintf(&b, "(akt_str_cmp(%s, %s) < 0)", ls, rs);
    } else if (op == TK_GT && string) {
        bprintf(&b, "(akt_str_cmp(%s, %s) > 0)", ls, rs);
    } else if (op == TK_LE && string) {
        bprintf(&b, "(akt_str_cmp(%s, %s) <= 0)", ls, rs);
    } else if (op == TK_GE && string) {
        bprintf(&b, "(akt_str_cmp(%s, %s) >= 0)", ls, rs);
    } else if (op == TK_POW) {
        bprintf(&b, "pow((double)(%s), (double)(%s))", ls, rs);
    } else {
        const char *s = "?";
        switch (op) {
            case TK_PLUS: s = "+"; break;
            case TK_MINUS: s = "-"; break;
            case TK_STAR: s = "*"; break;
            case TK_SLASH: s = "/"; break;
            case TK_PERCENT: s = "%"; break;
            case TK_AMP: s = "&"; break;
            case TK_PIPE: s = "|"; break;
            case TK_CARET: s = "^"; break;
            case TK_SHL: s = "<<"; break;
            case TK_SHR: s = ">>"; break;
            case TK_ANDAND: s = "&&"; break;
            case TK_OROR: s = "||"; break;
            case TK_EQEQ: s = "=="; break;
            case TK_NEQ: s = "!="; break;
            case TK_LT: s = "<"; break;
            case TK_GT: s = ">"; break;
            case TK_LE: s = "<="; break;
            case TK_GE: s = ">="; break;
        }
        bprintf(&b, "(%s %s %s)", ls, s, rs);
    }
    free(ls);
    free(rs);
    return b.data;
}

static Func *find_func_cg(Program *p, const char *name) {
    for (int i = 0; i < p->nfuncs; i++)
        if (strcmp(p->funcs[i]->name, name) == 0) return p->funcs[i];
    return NULL;
}

static char *gen_call(Gen *g, Expr *e) {
    Expr *callee = e->as.call.callee;
    Buf b = {0};
    if (callee->kind == EX_IDENT) {
        const char *name = callee->as.sval;
        if (strcmp(name, "println") == 0 || strcmp(name, "print") == 0) {
            int nl = strcmp(name, "println") == 0;
            if (e->as.call.argc == 0) return xstrdup(nl ? "akt_print_newline()" : "((void)0)");
            Type *t = e->as.call.args[0]->type;
            const char *fn = "akt_print_str";
            if (t && type_is_int(t)) fn = nl ? "akt_println_int" : "akt_print_int";
            else if (t && type_is_float(t)) fn = nl ? "akt_println_float" : "akt_print_float";
            else if (t && t->kind == TY_BOOL) fn = nl ? "akt_println_bool" : "akt_print_bool";
            else if (t && t->kind == TY_CHAR) fn = nl ? "akt_println_char" : "akt_print_char";
            else if (t && t->kind == TY_NAMED && t->name && strcmp(t->name, "Json") == 0)
                return xasprintf("%s(akt_json_to_string(%s))", nl ? "akt_println_str" : "akt_print_str",
                                 gen_expr(g, e->as.call.args[0]));
            else fn = nl ? "akt_println_str" : "akt_print_str";
            bprintf(&b, "%s(%s)", fn, gen_expr(g, e->as.call.args[0]));
            return b.data;
        }
        if (strcmp(name, "len") == 0) {
            Expr *a = e->as.call.args[0];
            Type *t = a->type;
            if (t && t->kind == TY_STRING) {
                bprintf(&b, "akt_str_len(%s)", gen_expr(g, a));
            } else if (t && t->kind == TY_ARRAY) {
                if (a->kind == EX_ARRAY) bprintf(&b, "%d", a->as.array.count);
                else bprintf(&b, "(sizeof(%s) / sizeof((%s)[0]))", gen_expr(g, a), gen_expr(g, a));
            } else if (t && t->kind == TY_VECTOR) {
                bprintf(&b, "akt_vec_len(&(%s))", gen_expr(g, a));
            } else {
                bprintf(&b, "0");
            }
            return b.data;
        }
        if (strcmp(name, "str") == 0 || strcmp(name, "to_string") == 0) {
            return to_string_expr(g, e->as.call.args[0], -1);
        }
        if (strcmp(name, "int") == 0) {
            bprintf(&b, "((int)(%s))", gen_expr(g, e->as.call.args[0]));
            return b.data;
        }
        if (strcmp(name, "float") == 0) {
            bprintf(&b, "((double)(%s))", gen_expr(g, e->as.call.args[0]));
            return b.data;
        }
        if (strcmp(name, "assert") == 0) {
            char *c = gen_expr(g, e->as.call.args[0]);
            bprintf(&b, "((%s) ? (void)0 : (akt_assert_fail(\"assert\", __FILE__, __LINE__), (void)0))", c);
            free(c);
            return b.data;
        }
        if (strcmp(name, "assert_eq") == 0 || strcmp(name, "assert_ne") == 0) {
            Expr *a = e->as.call.args[0], *bb = e->as.call.args[1];
            char *x = gen_expr(g, a), *y = gen_expr(g, bb);
            int eq = strcmp(name, "assert_eq") == 0;
            if (a->type && a->type->kind == TY_STRING)
                bprintf(&b, "((akt_str_eq(%s, %s) == %d) ? (void)0 : (akt_assert_fail(\"%s\", __FILE__, __LINE__), (void)0))",
                        x, y, eq, name);
            else
                bprintf(&b, "(((%s) %s (%s)) ? (void)0 : (akt_assert_fail(\"%s\", __FILE__, __LINE__), (void)0))",
                        x, eq ? "==" : "!=", y, name);
            free(x); free(y);
            return b.data;
        }
        if (strcmp(name, "panic") == 0) {
            bprintf(&b, "akt_panic((%s).data)", gen_expr(g, e->as.call.args[0]));
            return b.data;
        }
        if (strcmp(name, "Some") == 0) {
            bprintf(&b, "AKT_OPTION_SOME(%s)", gen_expr(g, e->as.call.args[0]));
            return b.data;
        }
        if (strcmp(name, "Ok") == 0) {
            Type *at = e->as.call.args[0]->type;
            if (!at || at->kind == TY_VOID) return xstrdup("AKT_RESULT_OK_UNIT()");
            bprintf(&b, "AKT_RESULT_OK(%s)", gen_expr(g, e->as.call.args[0]));
            return b.data;
        }
        if (strcmp(name, "Err") == 0) {
            bprintf(&b, "AKT_RESULT_ERR(%s)", gen_expr(g, e->as.call.args[0]));
            return b.data;
        }
        if (strcmp(name, "sqrt") == 0 || strcmp(name, "sin") == 0 || strcmp(name, "cos") == 0 ||
            strcmp(name, "tan") == 0 || strcmp(name, "floor") == 0 || strcmp(name, "ceil") == 0 ||
            strcmp(name, "round") == 0) {
            bprintf(&b, "%s((double)(%s))", name, gen_expr(g, e->as.call.args[0]));
            return b.data;
        }
        if (strcmp(name, "abs") == 0) {
            Expr *a = e->as.call.args[0];
            if (a->type && type_is_float(a->type))
                bprintf(&b, "fabs((double)(%s))", gen_expr(g, a));
            else
                bprintf(&b, "((double)llabs((long long)(%s)))", gen_expr(g, a));
            return b.data;
        }
        if (strcmp(name, "pow") == 0 || strcmp(name, "min") == 0 || strcmp(name, "max") == 0) {
            const char *fn = strcmp(name, "pow") == 0 ? "pow" : (strcmp(name, "min") == 0 ? "fmin" : "fmax");
            bprintf(&b, "%s((double)(%s), (double)(%s))", fn,
                    gen_expr(g, e->as.call.args[0]), gen_expr(g, e->as.call.args[1]));
            return b.data;
        }
        if (strcmp(name, "sleep_ms") == 0) {
            bprintf(&b, "akt_sleep_ms((int64_t)(%s))", gen_expr(g, e->as.call.args[0]));
            return b.data;
        }
        if (strcmp(name, "now_ms") == 0) return xstrdup("akt_now_ms()");
        Func *uf = find_func_cg(g->prog, name);
        bprintf(&b, "%s%s(", name, uf && uf->is_async ? "_task" : "");
        for (int i = 0; i < e->as.call.argc; i++) {
            if (i) bput(&b, ", ");
            Expr *a = e->as.call.args[i];
            char *v = gen_expr(g, a);
            if (uf && uf->is_cfunc && i < uf->nparams && a->type && a->type->kind == TY_STRING &&
                type_is_cstring(uf->params[i].type)) {
                bprintf(&b, "(%s).data", v);
            } else {
                bput(&b, v);
            }
            free(v);
        }
        bput(&b, ")");
        return b.data;
    }
    if (callee->kind == EX_MEMBER) {
        Expr *obj = callee->as.member.obj;
        const char *mn = callee->as.member.name;
        if (obj->kind == EX_IDENT && !decl_type(g, obj->as.sval)) {
            const char *mod = obj->as.sval;
            if (strcmp(mod, "io") == 0) {
                if (strcmp(mn, "read_to_string") == 0 || strcmp(mn, "read_file") == 0) {
                    bprintf(&b, "akt_read_file((%s).data, NULL)", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
                if (strcmp(mn, "write_to_string") == 0 || strcmp(mn, "write_file") == 0) {
                    bprintf(&b, "akt_write_file((%s).data, %s)",
                            gen_expr(g, e->as.call.args[0]), gen_expr(g, e->as.call.args[1]));
                    return b.data;
                }
                if (strcmp(mn, "exists") == 0) {
                    bprintf(&b, "akt_file_exists((%s).data)", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
            }
            if (strcmp(mod, "fs") == 0 && strcmp(mn, "exists") == 0) {
                bprintf(&b, "akt_file_exists((%s).data)", gen_expr(g, e->as.call.args[0]));
                return b.data;
            }
            if (strcmp(mod, "fs") == 0 && (strcmp(mn, "list_dir") == 0 || strcmp(mn, "walk") == 0)) {
                bprintf(&b, "%s(%s)", strcmp(mn, "walk") == 0 ? "akt_fs_walk" : "akt_fs_list_dir",
                        gen_expr(g, e->as.call.args[0]));
                return b.data;
            }
            if (strcmp(mod, "path") == 0) {
                if (strcmp(mn, "join") == 0) {
                    bprintf(&b, "akt_path_join(%s, %s)", gen_expr(g, e->as.call.args[0]),
                            gen_expr(g, e->as.call.args[1]));
                    return b.data;
                }
                const char *fn = strcmp(mn, "basename") == 0 ? "akt_path_basename" :
                                 strcmp(mn, "dirname") == 0 ? "akt_path_dirname" :
                                 strcmp(mn, "extension") == 0 ? "akt_path_extension" :
                                 strcmp(mn, "absolute") == 0 ? "akt_path_absolute" : NULL;
                if (fn) {
                    bprintf(&b, "%s(%s)", fn, gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
            }
            if (strcmp(mod, "env") == 0) {
                if (strcmp(mn, "get") == 0) {
                    bprintf(&b, "akt_env_get((%s).data, NULL)", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
                if (strcmp(mn, "cwd") == 0) return xstrdup("akt_get_cwd()");
            }
            if (strcmp(mod, "time") == 0) {
                if (strcmp(mn, "now_ms") == 0) return xstrdup("akt_now_ms()");
                if (strcmp(mn, "sleep_ms") == 0) {
                    bprintf(&b, "akt_sleep_ms((int64_t)(%s))", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
            }
            if (strcmp(mod, "process") == 0) {
                if (strcmp(mn, "pid") == 0) return xstrdup("akt_process_pid()");
                if (strcmp(mn, "exit") == 0) {
                    bprintf(&b, "akt_process_exit((int)(%s))", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
            }
            if (strcmp(mod, "random") == 0) {
                if (strcmp(mn, "int") == 0) {
                    bprintf(&b, "akt_random_int((int64_t)(%s))", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
                if (strcmp(mn, "float") == 0) return xstrdup("akt_random_float()");
            }
            if (strcmp(mod, "hash") == 0 && strcmp(mn, "fnv1a") == 0) {
                bprintf(&b, "akt_hash_fnv1a(%s)", gen_expr(g, e->as.call.args[0]));
                return b.data;
            }
            if (strcmp(mod, "base64") == 0) {
                if (strcmp(mn, "encode") == 0) {
                    bprintf(&b, "akt_base64_encode(%s)", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
                if (strcmp(mn, "decode") == 0) {
                    bprintf(&b, "akt_base64_decode(%s)", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
            }
            if (strcmp(mod, "fmt") == 0) {
                if (strcmp(mn, "parse_int") == 0) {
                    bprintf(&b, "((int)akt_str_parse_int(%s))", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
                if (strcmp(mn, "parse_float") == 0) {
                    bprintf(&b, "akt_str_parse_float(%s)", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
            }
            if (strcmp(mod, "json") == 0) {
                if (strcmp(mn, "parse") == 0) {
                    bprintf(&b, "akt_json_parse(%s)", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
                if (strcmp(mn, "stringify") == 0) {
                    bprintf(&b, "akt_json_stringify(%s)", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
            }
            if (strcmp(mod, "regex") == 0) {
                if (strcmp(mn, "match") == 0) {
                    bprintf(&b, "akt_regex_match(%s, %s)", gen_expr(g, e->as.call.args[0]),
                            gen_expr(g, e->as.call.args[1]));
                    return b.data;
                }
                if (strcmp(mn, "find") == 0) {
                    bprintf(&b, "akt_regex_find(%s, %s)", gen_expr(g, e->as.call.args[0]),
                            gen_expr(g, e->as.call.args[1]));
                    return b.data;
                }
                if (strcmp(mn, "replace") == 0) {
                    bprintf(&b, "akt_regex_replace(%s, %s, %s)", gen_expr(g, e->as.call.args[0]),
                            gen_expr(g, e->as.call.args[1]), gen_expr(g, e->as.call.args[2]));
                    return b.data;
                }
            }
            if (strcmp(mod, "thread") == 0) {
                if (strcmp(mn, "spawn") == 0) {
                    Expr *fn = e->as.call.args[0];
                    if (fn->kind == EX_CLOSURE) {
                        ThrRec *rec = NULL;
                        for (int i = 0; i < g->nthr; i++)
                            if (g->thr[i].closure == fn) rec = &g->thr[i];
                        if (!rec) { bput(&b, "NULL"); return b.data; }
                        rec->ncap = g->nvars;
                        for (int i = 0; i < g->nvars && i < 128; i++) rec->caps[i] = g->vars[i];
                        int id = g->tmp++;
                        if (g->nvars == 0) {
                            bprintf(&b, "akt_thread_spawn(%s, NULL)", rec->name);
                        } else {
                            bprintf(&b, "({ void** _ctx%d = (void**)akt_alloc(sizeof(void*) * %d);", id, g->nvars);
                            for (int i = 0; i < g->nvars; i++)
                                bprintf(&b, " _ctx%d[%d] = (void*)&%s;", id, i, g->vars[i].name);
                            bprintf(&b, " akt_thread_spawn(%s, _ctx%d); })", rec->name, id);
                        }
                    } else {
                        bprintf(&b, "akt_thread_spawn((AktThreadFn)%s, NULL)", fn->as.sval);
                    }
                    return b.data;
                }
                if (strcmp(mn, "join") == 0) {
                    bprintf(&b, "akt_thread_join(%s)", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
                if (strcmp(mn, "sleep_ms") == 0) {
                    bprintf(&b, "akt_thread_sleep_ms((int64_t)(%s))", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
            }
            if (strcmp(mod, "async") == 0 && (strcmp(mn, "all") == 0 || strcmp(mn, "select") == 0)) {
                Expr *arr = e->as.call.args[0];
                int n = arr->as.array.count;
                int id = g->tmp++;
                const char *et = "int";
                if (n) {
                    Type *it = arr->as.array.items[0]->type;
                    if (it && it->kind == TY_TASK) it = it->elem;
                    et = c_type(it);
                }
                bprintf(&b, "({ AktTask* _t%d[%d] = {", id, n > 0 ? n : 1);
                for (int i = 0; i < n; i++) {
                    if (i) bput(&b, ", ");
                    bput(&b, gen_expr(g, arr->as.array.items[i]));
                }
                if (n == 0) bput(&b, "0");
                bput(&b, "};");
                if (strcmp(mn, "all") == 0) {
                    bprintf(&b, " AktVec _o%d = akt_vec_new(sizeof(%s));", id, et);
                    bprintf(&b, " for (int _j%d = 0; _j%d < %d; _j%d++) { %s* _p%d = (%s*)akt_task_wait(_t%d[_j%d]);",
                            id, id, n, id, et, id, et, id, id);
                    bprintf(&b, " %s _v%d = *_p%d; akt_free(_p%d); akt_vec_push(&_o%d, &_v%d); akt_task_free(_t%d[_j%d]); }",
                            et, id, id, id, id, id, id, id);
                    bprintf(&b, " _o%d; })", id);
                } else {
                    bprintf(&b, " int _i%d = akt_task_wait_any(_t%d, %d); %s _v%d = {0}; %s* _sel%d = NULL;",
                            id, id, n, et, id, et, id);
                    bprintf(&b, " for (int _j%d = 0; _j%d < %d; _j%d++) { void* _r%d = akt_task_wait(_t%d[_j%d]);",
                            id, id, n, id, id, id, id);
                    bprintf(&b, " if (_j%d == _i%d) _sel%d = (%s*)_r%d; else if (_r%d) akt_free(_r%d); akt_task_free(_t%d[_j%d]); }",
                            id, id, id, et, id, id, id, id, id);
                    bprintf(&b, " if (_sel%d) { _v%d = *_sel%d; akt_free(_sel%d); } _v%d; })", id, id, id, id, id);
                }
                return b.data;
            }
            if (strcmp(mod, "async") == 0 && strcmp(mn, "timeout") == 0) {
                Expr *call = e->as.call.args[1];
                Type *rt = call->type;
                if (rt && rt->kind == TY_TASK) rt = rt->elem;
                const char *et = c_type(rt);
                int id = g->tmp++;
                char *ms = gen_expr(g, e->as.call.args[0]);
                char *task = gen_expr(g, call);
                bprintf(&b, "({ AktTask* _t%d = %s; void* _p%d = akt_task_wait_timeout(_t%d, (int64_t)(%s)); "
                           "AktResult _r%d;", id, task, id, id, ms, id);
                bprintf(&b, " if (_p%d) { %s _v%d = *(%s*)_p%d; _r%d = AKT_RESULT_OK(_v%d); akt_free(_p%d); akt_task_free(_t%d); }",
                        id, et, id, et, id, id, id, id, id);
                bprintf(&b, " else { _r%d = AKT_RESULT_ERR(akt_str(\"timeout\", 7)); } _r%d; })", id, id);
                free(ms); free(task);
                return b.data;
            }
            if (strcmp(mod, "http") == 0) {
                if (strcmp(mn, "get") == 0) {
                    bprintf(&b, "akt_http_get(%s)", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
                if (strcmp(mn, "post") == 0) {
                    bprintf(&b, "akt_http_post(%s, %s)", gen_expr(g, e->as.call.args[0]),
                            gen_expr(g, e->as.call.args[1]));
                    return b.data;
                }
            }
            if (strcmp(mod, "sqlite") == 0 && strcmp(mn, "open") == 0) {
                bprintf(&b, "akt_sqlite_open(%s)", gen_expr(g, e->as.call.args[0]));
                return b.data;
            }
            if (strcmp(mod, "c") == 0) {
                if (strcmp(mn, "to_string") == 0 || strcmp(mn, "cstring") == 0) {
                    bprintf(&b, "akt_str_cstr((const char*)(%s))", gen_expr(g, e->as.call.args[0]));
                    return b.data;
                }
                bprintf(&b, "%s(", mn);
                for (int i = 0; i < e->as.call.argc; i++) {
                    if (i) bput(&b, ", ");
                    Expr *a = e->as.call.args[i];
                    if (a->type && a->type->kind == TY_STRING) {
                        bprintf(&b, "(%s).data", gen_expr(g, a));
                    } else {
                        bput(&b, gen_expr(g, a));
                    }
                }
                bput(&b, ")");
                return b.data;
            }
        }
        Type *ot = obj->type;
        Type *decl = obj->kind == EX_IDENT ? decl_type(g, obj->as.sval) : NULL;
        if (decl && decl->kind == TY_WEAK && strcmp(mn, "upgrade") == 0) {
            Type *base = decl->elem ? decl->elem : type_named("int");
            const char *ct = c_type(base);
            int id = g->tmp++;
            bprintf(&b, "({ %s _ov%d; int _ok%d = akt_shared_try_get(%s, &_ov%d, sizeof(%s)); "
                        "_ok%d ? AKT_OPTION_SOME(_ov%d) : AKT_OPTION_NONE(); })",
                    ct, id, id, obj->as.sval, id, ct, id, id);
            return b.data;
        }
        if (obj->kind == EX_IDENT && is_managed_name(obj->as.sval) && strcmp(mn, "new") == 0) {
            StructDecl *sd = find_struct_cg(g->prog, obj->as.sval);
            const char *tn = sd ? sd->name : obj->as.sval;
            int id = g->tmp++;
            if (e->as.call.argc > 0) {
                bprintf(&b, "({ %s_s _tmp%d = {", tn, id);
                for (int i = 0; i < e->as.call.argc; i++) {
                    if (i) bput(&b, ", ");
                    bput(&b, gen_expr(g, e->as.call.args[i]));
                }
                bprintf(&b, "}; akt_shared_new(sizeof(%s_s), &_tmp%d); })", tn, id);
            } else {
                bprintf(&b, "akt_shared_new(sizeof(%s_s), NULL)", tn);
            }
            return b.data;
        }
        if (!ot && obj->kind == EX_IDENT) {
            bprintf(&b, "%s(", mn);
            for (int i = 0; i < e->as.call.argc; i++) {
                if (i) bput(&b, ", ");
                bput(&b, gen_expr(g, e->as.call.args[i]));
            }
            bput(&b, ")");
            return b.data;
        }
        if (ot && ot->kind == TY_NAMED && ot->name && strcmp(ot->name, "Sqlite") == 0) {
            char *o = gen_expr(g, obj);
            if (strcmp(mn, "exec") == 0) bprintf(&b, "akt_sqlite_exec(%s, %s)", o, gen_expr(g, e->as.call.args[0]));
            else if (strcmp(mn, "query") == 0) bprintf(&b, "akt_sqlite_query(%s, %s)", o, gen_expr(g, e->as.call.args[0]));
            else if (strcmp(mn, "close") == 0) bprintf(&b, "akt_sqlite_close(%s)", o);
            else bput(&b, "0");
            free(o);
            return b.data;
        }
        if (ot && ot->kind == TY_NAMED && ot->name && strcmp(ot->name, "Mutex") == 0) {
            char *o = gen_expr(g, obj);
            if (strcmp(mn, "lock") == 0) bprintf(&b, "akt_mutex_lock(%s)", o);
            else if (strcmp(mn, "unlock") == 0) bprintf(&b, "akt_mutex_unlock(%s)", o);
            else bput(&b, "0");
            free(o);
            return b.data;
        }
        if (ot && ot->kind == TY_NAMED && ot->name && strcmp(ot->name, "Atomic") == 0) {
            char *o = gen_expr(g, obj);
            if (strcmp(mn, "load") == 0) bprintf(&b, "akt_atomic_load(%s)", o);
            else if (strcmp(mn, "store") == 0) bprintf(&b, "akt_atomic_store(%s, (int64_t)(%s))", o, gen_expr(g, e->as.call.args[0]));
            else if (strcmp(mn, "fetch_add") == 0) bprintf(&b, "akt_atomic_add(%s, (int64_t)(%s))", o, gen_expr(g, e->as.call.args[0]));
            else bput(&b, "0");
            free(o);
            return b.data;
        }
        if (ot && ot->kind == TY_NAMED && is_managed_name(ot->name)) {
            const char *tn = ot->name;
            const char *dot = strrchr(tn, '.');
            if (dot) tn = dot + 1;
            bprintf(&b, "%s_%s((( %s_s*)akt_shared_get(%s))", tn, mn, tn, gen_expr(g, obj));
            for (int i = 0; i < e->as.call.argc; i++) {
                bput(&b, ", ");
                bput(&b, gen_expr(g, e->as.call.args[i]));
            }
            bput(&b, ")");
            return b.data;
        }
        if (ot && (ot->kind == TY_REF || ot->kind == TY_PTR)) ot = ot->elem;
        if (ot && ot->kind == TY_NAMED) {
            const char *tn = ot->name;
            const char *dot = strrchr(tn, '.');
            if (dot) tn = dot + 1;
            bprintf(&b, "%s_%s(", tn, mn);
            if (obj->kind == EX_IDENT && strcmp(obj->as.sval, "self") == 0)
                bput(&b, "self");
            else if (obj->type && (obj->type->kind == TY_REF || obj->type->kind == TY_PTR))
                bput(&b, gen_expr(g, obj));
            else {
                bput(&b, "&(");
                bput(&b, gen_expr(g, obj));
                bput(&b, ")");
            }
            for (int i = 0; i < e->as.call.argc; i++) {
                bput(&b, ", ");
                bput(&b, gen_expr(g, e->as.call.args[i]));
            }
            bput(&b, ")");
            return b.data;
        }
        if (ot && ot->kind == TY_VECTOR) {
            if (strcmp(mn, "len") == 0) {
                bprintf(&b, "akt_vec_len(&(%s))", gen_expr(g, obj));
                return b.data;
            }
            if (strcmp(mn, "push") == 0) {
                bprintf(&b, "AKT_VEC_PUSH(%s, %s)", gen_expr(g, obj), gen_expr(g, e->as.call.args[0]));
                return b.data;
            }
            if (strcmp(mn, "clear") == 0) {
                bprintf(&b, "akt_vec_clear(&(%s))", gen_expr(g, obj));
                return b.data;
            }
            if (strcmp(mn, "filter") == 0 || strcmp(mn, "map") == 0) {
                Expr *cl = e->as.call.args[0];
                char *v = gen_expr(g, obj);
                int id = g->tmp++;
                const char *et = c_type(ot->elem);
                const char *outt = strcmp(mn, "map") == 0
                    ? c_type(cl->as.closure.body->type) : et;
                bprintf(&b, "({ AktVec _out%d = akt_vec_new(sizeof(%s));", id, outt);
                bprintf(&b, " for (size_t _i%d = 0; _i%d < akt_vec_len(&(%s)); _i%d++) {", id, id, v, id);
                bprintf(&b, " %s %s = *(%s*)akt_vec_get(&(%s), _i%d);", et, cl->as.closure.params[0], et, v, id);
                if (strcmp(mn, "filter") == 0) {
                    char *body = gen_expr(g, cl->as.closure.body);
                    bprintf(&b, " if (%s) akt_vec_push(&_out%d, &%s);", body, id, cl->as.closure.params[0]);
                    free(body);
                } else {
                    char *body = gen_expr(g, cl->as.closure.body);
                    bprintf(&b, " %s _v%d = %s; akt_vec_push(&_out%d, &_v%d);", outt, id, body, id, id);
                    free(body);
                }
                bprintf(&b, " } _out%d; })", id);
                free(v);
                return b.data;
            }
            if (strcmp(mn, "reduce") == 0) {
                Expr *init = e->as.call.args[0];
                Expr *cl = e->as.call.args[1];
                char *v = gen_expr(g, obj);
                char *iv = gen_expr(g, init);
                int id = g->tmp++;
                const char *et = c_type(ot->elem);
                const char *at = c_type(init->type);
                bprintf(&b, "({ %s _acc%d = %s;", at, id, iv);
                bprintf(&b, " for (size_t _i%d = 0; _i%d < akt_vec_len(&(%s)); _i%d++) {", id, id, v, id);
                bprintf(&b, " %s %s = *(%s*)akt_vec_get(&(%s), _i%d);", et, cl->as.closure.params[1], et, v, id);
                bprintf(&b, " %s %s = _acc%d;", at, cl->as.closure.params[0], id);
                char *body = gen_expr(g, cl->as.closure.body);
                bprintf(&b, " _acc%d = (%s); } _acc%d; })", id, body, id, id);
                free(body); free(v); free(iv);
                return b.data;
            }
            if (strcmp(mn, "pop") == 0) {
                bprintf(&b, "AKT_VEC_POP(%s, %s)", gen_expr(g, obj), c_type(ot->elem));
                return b.data;
            }
            if (strcmp(mn, "push_back") == 0) {
                bprintf(&b, "AKT_VEC_PUSH(%s, %s)", gen_expr(g, obj), gen_expr(g, e->as.call.args[0]));
                return b.data;
            }
            if (strcmp(mn, "push_front") == 0) {
                bprintf(&b, "({ %s _akt_v = %s; akt_vec_push_front(&(%s), &_akt_v); })",
                        c_type(ot->elem), gen_expr(g, e->as.call.args[0]), gen_expr(g, obj));
                return b.data;
            }
            if (strcmp(mn, "front") == 0) {
                bprintf(&b, "(*(%s*)akt_vec_front(&(%s)))", c_type(ot->elem), gen_expr(g, obj));
                return b.data;
            }
            if (strcmp(mn, "back") == 0) {
                bprintf(&b, "(*(%s*)akt_vec_back(&(%s)))", c_type(ot->elem), gen_expr(g, obj));
                return b.data;
            }
        }
        if (ot && ot->kind == TY_SET) {
            if (strcmp(mn, "len") == 0) {
                bprintf(&b, "akt_set_len(&(%s))", gen_expr(g, obj));
                return b.data;
            }
            if (strcmp(mn, "add") == 0) {
                bprintf(&b, "({ %s _akt_v = %s; akt_set_add(&(%s), &_akt_v); })",
                        c_type(ot->elem), gen_expr(g, e->as.call.args[0]), gen_expr(g, obj));
                return b.data;
            }
            if (strcmp(mn, "contains") == 0) {
                bprintf(&b, "({ %s _akt_v = %s; akt_set_contains(&(%s), &_akt_v); })",
                        c_type(ot->elem), gen_expr(g, e->as.call.args[0]), gen_expr(g, obj));
                return b.data;
            }
            if (strcmp(mn, "remove") == 0) {
                bprintf(&b, "({ %s _akt_v = %s; akt_set_remove(&(%s), &_akt_v); })",
                        c_type(ot->elem), gen_expr(g, e->as.call.args[0]), gen_expr(g, obj));
                return b.data;
            }
        }
        if (ot && ot->kind == TY_MAP) {
            if (strcmp(mn, "len") == 0) {
                bprintf(&b, "akt_map_len(&(%s))", gen_expr(g, obj));
                return b.data;
            }
            if (strcmp(mn, "set") == 0) {
                bprintf(&b, "AKT_MAP_SET(%s, %s, %s)", gen_expr(g, obj),
                        gen_expr(g, e->as.call.args[0]), gen_expr(g, e->as.call.args[1]));
                return b.data;
            }
            if (strcmp(mn, "get") == 0) {
                bprintf(&b, "akt_map_get(&(%s), %s)", gen_expr(g, obj), gen_expr(g, e->as.call.args[0]));
                return b.data;
            }
            if (strcmp(mn, "contains") == 0) {
                bprintf(&b, "akt_map_contains(&(%s), %s)", gen_expr(g, obj), gen_expr(g, e->as.call.args[0]));
                return b.data;
            }
            if (strcmp(mn, "remove") == 0) {
                bprintf(&b, "akt_map_remove(&(%s), %s)", gen_expr(g, obj), gen_expr(g, e->as.call.args[0]));
                return b.data;
            }
        }
        if (ot && ot->kind == TY_STRING) {
            if (strcmp(mn, "len") == 0 || strcmp(mn, "byte_len") == 0 || strcmp(mn, "char_len") == 0)
                bprintf(&b, "akt_str_len(%s)", gen_expr(g, obj));
            else if (strcmp(mn, "to_upper") == 0)
                bprintf(&b, "akt_str_upper(%s)", gen_expr(g, obj));
            else if (strcmp(mn, "to_lower") == 0)
                bprintf(&b, "akt_str_lower(%s)", gen_expr(g, obj));
            else if (strcmp(mn, "trim") == 0)
                bprintf(&b, "akt_str_trim(%s)", gen_expr(g, obj));
            else if (strcmp(mn, "reverse") == 0)
                bprintf(&b, "akt_str_reverse(%s)", gen_expr(g, obj));
            else if (strcmp(mn, "capitalize") == 0)
                bprintf(&b, "akt_str_capitalize(%s)", gen_expr(g, obj));
            else if (strcmp(mn, "contains") == 0)
                bprintf(&b, "akt_str_contains(%s, %s)", gen_expr(g, obj), gen_expr(g, e->as.call.args[0]));
            else if (strcmp(mn, "starts_with") == 0)
                bprintf(&b, "akt_str_starts_with(%s, %s)", gen_expr(g, obj), gen_expr(g, e->as.call.args[0]));
            else if (strcmp(mn, "ends_with") == 0)
                bprintf(&b, "akt_str_ends_with(%s, %s)", gen_expr(g, obj), gen_expr(g, e->as.call.args[0]));
            else if (strcmp(mn, "find") == 0)
                bprintf(&b, "akt_str_find(%s, %s)", gen_expr(g, obj), gen_expr(g, e->as.call.args[0]));
            else if (strcmp(mn, "replace") == 0)
                bprintf(&b, "akt_str_replace(%s, %s, %s)", gen_expr(g, obj),
                        gen_expr(g, e->as.call.args[0]), gen_expr(g, e->as.call.args[1]));
            else if (strcmp(mn, "repeat") == 0)
                bprintf(&b, "akt_str_repeat(%s, (int64_t)(%s))", gen_expr(g, obj),
                        gen_expr(g, e->as.call.args[0]));
            else if (strcmp(mn, "pad_left") == 0)
                bprintf(&b, "akt_str_pad_left(%s, (int64_t)(%s), (uint32_t)(%s))", gen_expr(g, obj),
                        gen_expr(g, e->as.call.args[0]), gen_expr(g, e->as.call.args[1]));
            else if (strcmp(mn, "pad_right") == 0)
                bprintf(&b, "akt_str_pad_right(%s, (int64_t)(%s), (uint32_t)(%s))", gen_expr(g, obj),
                        gen_expr(g, e->as.call.args[0]), gen_expr(g, e->as.call.args[1]));
            else if (strcmp(mn, "split") == 0)
                bprintf(&b, "akt_str_split(%s, %s)", gen_expr(g, obj), gen_expr(g, e->as.call.args[0]));
            else if (strcmp(mn, "parse_int") == 0)
                bprintf(&b, "((int)akt_str_parse_int(%s))", gen_expr(g, obj));
            else if (strcmp(mn, "parse_float") == 0)
                bprintf(&b, "akt_str_parse_float(%s)", gen_expr(g, obj));
            else
                bprintf(&b, "akt_str_cstr(\"\")");
            return b.data;
        }
    }
    if (callee->kind == EX_STATIC) {
        if (is_managed_name(callee->as.path.type_name) &&
            strcmp(callee->as.path.name, "new") == 0) {
            const char *tn = callee->as.path.type_name;
            int id = g->tmp++;
            if (e->as.call.argc > 0) {
                bprintf(&b, "({ %s_s _tmp%d = {", tn, id);
                for (int i = 0; i < e->as.call.argc; i++) {
                    if (i) bput(&b, ", ");
                    bput(&b, gen_expr(g, e->as.call.args[i]));
                }
                bprintf(&b, "}; akt_shared_new(sizeof(%s_s), &_tmp%d); })", tn, id);
            } else {
                bprintf(&b, "akt_shared_new(sizeof(%s_s), NULL)", tn);
            }
            return b.data;
        }
        if (strcmp(callee->as.path.type_name, "Mutex") == 0 &&
            strcmp(callee->as.path.name, "new") == 0)
            return xstrdup("akt_mutex_new()");
        if (strcmp(callee->as.path.type_name, "Atomic") == 0 &&
            strcmp(callee->as.path.name, "new") == 0) {
            bprintf(&b, "akt_atomic_new((int64_t)(%s))", gen_expr(g, e->as.call.args[0]));
            return b.data;
        }
        for (int i = 0; i < g->prog->nenums; i++) {
            EnumDecl *ed = g->prog->enums[i];
            if (strcmp(ed->name, callee->as.path.type_name) != 0) continue;
            for (int v = 0; v < ed->nvariants; v++) {
                if (strcmp(ed->variants[v], callee->as.path.name) != 0) continue;
                if (ed->variant_nfields[v] == 0) break;
                bprintf(&b, "(%s){ .tag = %s_%s, .data.%s = {", ed->name, ed->name, ed->variants[v], ed->variants[v]);
                for (int a = 0; a < e->as.call.argc; a++) {
                    if (a) bput(&b, ", ");
                    bprintf(&b, ".%s = %s", ed->variant_field_names[v][a], gen_expr(g, e->as.call.args[a]));
                }
                bput(&b, "} }");
                return b.data;
            }
        }
        bprintf(&b, "%s_%s(", callee->as.path.type_name, callee->as.path.name);
        for (int i = 0; i < e->as.call.argc; i++) {
            if (i) bput(&b, ", ");
            bput(&b, gen_expr(g, e->as.call.args[i]));
        }
        bput(&b, ")");
        return b.data;
    }
    return xstrdup("0");
}

static char *gen_expr(Gen *g, Expr *e) {
    if (!e) return xstrdup("0");
    Buf b = {0};
    switch (e->kind) {
        case EX_INT:
            if (e->as.ival >= -2147483647LL - 1LL && e->as.ival <= 2147483647LL)
                bprintf(&b, "%lld", e->as.ival);
            else
                bprintf(&b, "%lldLL", e->as.ival);
            return b.data;
        case EX_FLOAT: {
            double v = e->as.fval;
            bprintf(&b, "%.17g", v);
            if (!strchr(b.data, '.') && !strchr(b.data, 'e') && !strchr(b.data, 'E'))
                bput(&b, ".0");
            return b.data;
        }
        case EX_STRING: return str_literal(e->as.sval);
        case EX_FSTRING: return gen_fstring(g, e);
        case EX_CHAR: bprintf(&b, "((uint32_t)%lld)", e->as.ival); return b.data;
        case EX_BOOL: return xstrdup(e->as.ival ? "true" : "false");
        case EX_NULL: return xstrdup("NULL");
        case EX_UNIT: return xstrdup("0");
        case EX_IDENT:
            if (e->shared_ref)
                return xasprintf("(*(%s*)akt_shared_get(%s))", c_type(e->type), e->as.sval);
            if (strcmp(e->as.sval, "PI") == 0) return xstrdup("3.14159265358979323846");
            if (strcmp(e->as.sval, "E") == 0) return xstrdup("2.71828182845904523536");
            if (strcmp(e->as.sval, "TAU") == 0) return xstrdup("6.28318530717958647692");
            if (strcmp(e->as.sval, "PHI") == 0) return xstrdup("1.61803398874989484820");
            if (strcmp(e->as.sval, "INF") == 0) return xstrdup("(1.0/0.0)");
            if (strcmp(e->as.sval, "NAN") == 0) return xstrdup("(0.0/0.0)");
            if (strcmp(e->as.sval, "None") == 0) return xstrdup("AKT_OPTION_NONE()");
            return xstrdup(e->as.sval);
        case EX_BINARY: return gen_binary(g, e);
        case EX_UNARY: {
            char *x = gen_expr(g, e->as.unary.expr);
            switch (e->as.unary.op) {
                case TK_MINUS: bprintf(&b, "(-(%s))", x); break;
                case TK_BANG:
                case TK_NOT: bprintf(&b, "(!(%s))", x); break;
                case TK_TILDE: bprintf(&b, "(~(%s))", x); break;
                case TK_STAR: bprintf(&b, "(*(%s))", x); break;
                case TK_AMP: bprintf(&b, "(&(%s))", x); break;
                case TK_CARET: bprintf(&b, "(&(%s))", x); break;
                case TK_QUESTION: {
                    Type *ot = e->as.unary.expr->type;
                    if (ot && ot->kind == TY_RESULT) {
                        const char *et = ot->elem ? c_type(ot->elem) : "void";
                        if (ot->elem && ot->elem->kind != TY_VOID)
                            bprintf(&b, "({ AktResult _akt_r = (%s); if (!_akt_r.is_ok) return _akt_r; *(%s*)_akt_r.value; })", x, et);
                        else
                            bprintf(&b, "({ AktResult _akt_r = (%s); if (!_akt_r.is_ok) return _akt_r; })", x);
                    } else if (ot && ot->kind == TY_OPTION) {
                        const char *et = ot->elem ? c_type(ot->elem) : "void";
                        if (ot->elem && ot->elem->kind != TY_VOID)
                            bprintf(&b, "({ AktOption _akt_o = (%s); if (!_akt_o.is_some) return AKT_OPTION_NONE(); *(%s*)_akt_o.value; })", x, et);
                        else
                            bprintf(&b, "({ AktOption _akt_o = (%s); if (!_akt_o.is_some) return AKT_OPTION_NONE(); })", x);
                    } else {
                        bput(&b, x);
                    }
                    break;
                }
                case TK_AWAIT: {
                    Expr *inner = e->as.unary.expr;
                    int is_async_call = inner->kind == EX_CALL && inner->as.call.callee->kind == EX_IDENT &&
                        find_func_cg(g->prog, inner->as.call.callee->as.sval) &&
                        find_func_cg(g->prog, inner->as.call.callee->as.sval)->is_async;
                    int is_task = (inner->type && inner->type->kind == TY_TASK) || is_async_call;
                    if (!is_task) { bput(&b, x); break; }
                    Type *rt = e->type;
                    int id = g->tmp++;
                    if (!rt || rt->kind == TY_VOID) {
                        bprintf(&b, "({ AktTask* _t%d = %s; akt_task_wait(_t%d); akt_task_free(_t%d); })",
                                id, x, id, id);
                    } else {
                        bprintf(&b, "({ AktTask* _t%d = %s; %s* _p%d = (%s*)akt_task_wait(_t%d); "
                                   "%s _v%d = *_p%d; akt_free(_p%d); akt_task_free(_t%d); _v%d; })",
                                id, x, c_type(rt), id, c_type(rt), id,
                                c_type(rt), id, id, id, id, id);
                    }
                    break;
                }
                default: bput(&b, x); break;
            }
            free(x);
            return b.data;
        }
        case EX_CALL: return gen_call(g, e);
        case EX_MEMBER: {
            char *o = gen_expr(g, e->as.member.obj);
            Type *ot = e->as.member.obj->type;
            if (ot && ot->kind == TY_NAMED && is_managed_name(ot->name))
                bprintf(&b, "((%s_s*)akt_shared_get(%s))->%s", ot->name, o, e->as.member.name);
            else if (ot && (ot->kind == TY_REF || ot->kind == TY_PTR))
                bprintf(&b, "(%s)->%s", o, e->as.member.name);
            else
                bprintf(&b, "(%s).%s", o, e->as.member.name);
            free(o);
            return b.data;
        }
        case EX_INDEX: {
            Expr *obj = e->as.index.obj;
            Type *ot = obj->type;
            char *o = gen_expr(g, obj);
            char *i = gen_expr(g, e->as.index.index);
            if (ot && ot->kind == TY_VECTOR) {
                bprintf(&b, "(*(%s*)akt_vec_get(&(%s), (size_t)(%s)))", c_type(ot->elem), o, i);
            } else if (ot && ot->kind == TY_STRING) {
                bprintf(&b, "akt_str_char_at(%s, (size_t)(%s))", o, i);
            } else if (ot && ot->kind == TY_NAMED && ot->name && strcmp(ot->name, "Json") == 0) {
                if (e->as.index.index->type && e->as.index.index->type->kind == TY_STRING)
                    bprintf(&b, "akt_json_get(%s, %s)", o, i);
                else
                    bprintf(&b, "akt_json_at(%s, (size_t)(%s))", o, i);
            } else {
                bprintf(&b, "(%s)[%s]", o, i);
            }
            free(o); free(i);
            return b.data;
        }
        case EX_CAST: {
            Expr *src = e->as.cast.expr;
            Type *st = src->type, *tt = e->as.cast.target;
            if (tt && tt->kind == TY_STRING) {
                return to_string_expr(g, src, -1);
            }
            if (tt && type_is_int(tt) && st && st->kind == TY_STRING) {
                bprintf(&b, "((int64_t)akt_str_parse_int(%s))", gen_expr(g, src));
                return b.data;
            }
            if (tt && type_is_float(tt) && st && st->kind == TY_STRING) {
                bprintf(&b, "akt_str_parse_float(%s)", gen_expr(g, src));
                return b.data;
            }
            bprintf(&b, "((%s)(%s))", c_type(tt), gen_expr(g, src));
            return b.data;
        }
        case EX_TERNARY: {
            char *c = gen_expr(g, e->as.ternary.cond);
            char *a = gen_expr(g, e->as.ternary.then_expr);
            char *d = gen_expr(g, e->as.ternary.else_expr);
            bprintf(&b, "((%s) ? (%s) : (%s))", c, a, d);
            free(c); free(a); free(d);
            return b.data;
        }
        case EX_COALESCE: {
            char *a = gen_expr(g, e->as.ternary.then_expr);
            char *d = gen_expr(g, e->as.ternary.else_expr);
            Type *t = e->as.ternary.then_expr->type;
            if (t && t->kind == TY_STRING)
                bprintf(&b, "((%s).len ? (%s) : (%s))", a, a, d);
            else if (t && (t->kind == TY_PTR || t->kind == TY_REF))
                bprintf(&b, "((%s) ? (%s) : (%s))", a, a, d);
            else if (t && t->kind == TY_OPTION)
                bprintf(&b, "({ AktOption _akt_o = (%s); _akt_o.is_some ? *(%s*)_akt_o.value : (%s); })",
                        a, t->elem ? c_type(t->elem) : "int", d);
            else if (t && t->kind == TY_RESULT)
                bprintf(&b, "({ AktResult _akt_r = (%s); _akt_r.is_ok ? *(%s*)_akt_r.value : (%s); })",
                        a, t->elem ? c_type(t->elem) : "int", d);
            else
                bprintf(&b, "((%s) ? (%s) : (%s))", a, a, d);
            free(a); free(d);
            return b.data;
        }
        case EX_STRUCT: {
            const char *tn = e->as.struct_init.type_name;
            const char *dot = strrchr(tn, '.');
            if (dot) tn = dot + 1;
            bprintf(&b, "(%s){", tn);
            for (int i = 0; i < e->as.struct_init.nfields; i++) {
                if (i) bput(&b, ", ");
                bprintf(&b, ".%s = %s", e->as.struct_init.field_names[i],
                        gen_expr(g, e->as.struct_init.field_vals[i]));
            }
            bput(&b, "}");
            return b.data;
        }
        case EX_ARRAY: {
            Type *t = e->type;
            const char *et = (t && t->elem) ? c_type(t->elem) : "int64_t";
            bprintf(&b, "(%s[]){", et);
            for (int i = 0; i < e->as.array.count; i++) {
                if (i) bput(&b, ", ");
                bput(&b, gen_expr(g, e->as.array.items[i]));
            }
            bput(&b, "}");
            return b.data;
        }
        case EX_MAP:
            bput(&b, "akt_map_new(sizeof(int))");
            return b.data;
        case EX_RANGE:
            fatal_at(g->prog->file, e->line, e->col, "range is only valid in for-in");
            return xstrdup("0");
        case EX_SWITCH: {
            int id = g->tmp++;
            Type *t = e->type;
            Type *vt = e->as.switch_expr.value->type;
            char *v = gen_expr(g, e->as.switch_expr.value);
            bprintf(&b, "({ %s _sw%d = %s; %s _out = {0};", c_type(vt), id, v, c_type(t));
            free(v);
            for (int i = 0; i < e->as.switch_expr.ncases; i++) {
                SwitchCase *sc = &e->as.switch_expr.cases[i];
                if (sc->is_default) {
                    char *x = gen_expr(g, e->as.switch_expr.values[i]);
                    bprintf(&b, " _out = %s;", x);
                    free(x);
                    break;
                }
                char cond[512];
                char *m = sc->match->kind == EX_RANGE ? NULL : gen_expr(g, sc->match);
                if (sc->match->kind == EX_RANGE) {
                    char *a = gen_expr(g, sc->match->as.range.start);
                    char *bb = gen_expr(g, sc->match->as.range.end);
                    snprintf(cond, sizeof(cond), "_sw%d %s %s && _sw%d %s %s", id,
                             sc->match->as.range.inclusive ? ">=" : ">", a,
                             id, sc->match->as.range.inclusive ? "<=" : "<", bb);
                    free(a); free(bb);
                } else if (e->as.switch_expr.value->type && e->as.switch_expr.value->type->kind == TY_STRING)
                    snprintf(cond, sizeof(cond), "akt_str_eq(_sw%d, %s)", id, m);
                else
                    snprintf(cond, sizeof(cond), "_sw%d == %s", id, m);
                if (m) free(m);
                char *x = gen_expr(g, e->as.switch_expr.values[i]);
                bprintf(&b, " if (%s) _out = %s; else", cond, x);
                free(x);
            }
            bput(&b, " (void)0; _out; })");
            return b.data;
        }
        case EX_CLOSURE:
            fatal_at(g->prog->file, e->line, e->col, "closure is only supported in Vector.filter/map/reduce");
            return xstrdup("0");
        case EX_STATIC: {
            EnumDecl *ed = NULL;
            for (int i = 0; i < g->prog->nenums; i++)
                if (strcmp(g->prog->enums[i]->name, e->as.path.type_name) == 0)
                    ed = g->prog->enums[i];
            if (ed) bprintf(&b, "%s_%s", e->as.path.type_name, e->as.path.name);
            else bput(&b, "0");
            return b.data;
        }
    }
    return xstrdup("0");
}

static void defer_push(Gen *g, Stmt *s) {
    DeferFrame *f = g->defers;
    if (!f) return;
    if (f->count + 1 > f->cap) {
        f->cap = f->cap ? f->cap * 2 : 4;
        f->items = (Stmt **)xrealloc(f->items, (size_t)f->cap * sizeof(Stmt *));
    }
    f->items[f->count++] = s;
}

static int is_moved(Gen *g, const char *name) {
    for (int i = 0; i < g->nmoved; i++)
        if (strcmp(g->moved[i], name) == 0) return 1;
    return 0;
}

static void mark_moved(Gen *g, const char *name) {
    if (is_moved(g, name) || g->nmoved >= 128) return;
    g->moved[g->nmoved++] = xstrdup(name);
}

static int is_unref_of_moved(Gen *g, Stmt *s) {
    if (!s || s->kind != ST_EXPR) return 0;
    Expr *e = s->as.expr;
    if (!e || e->kind != EX_CALL || !e->as.call.callee || e->as.call.callee->kind != EX_IDENT) return 0;
    if (strcmp(e->as.call.callee->as.sval, "akt_shared_unref") != 0) return 0;
    if (e->as.call.argc != 1 || e->as.call.args[0]->kind != EX_IDENT) return 0;
    return is_moved(g, e->as.call.args[0]->as.sval);
}

static void emit_defers_until(Gen *g, DeferFrame *stop) {
    for (DeferFrame *f = g->defers; f && f != stop; f = f->prev) {
        for (int i = f->count - 1; i >= 0; i--) {
            if (is_unref_of_moved(g, f->items[i])) continue;
            gen_stmt(g, f->items[i]);
        }
    }
}
static void emit_all_defers(Gen *g) { emit_defers_until(g, NULL); }

static void gen_block(Gen *g, Stmt *s) {
    DeferFrame frame;
    frame.items = NULL;
    frame.count = 0;
    frame.cap = 0;
    frame.prev = g->defers;
    g->defers = &frame;
    int mark = g->nvars;
    line(g, "{");
    g->indent++;
    for (int i = 0; i < s->as.block.count; i++) gen_stmt(g, s->as.block.items[i]);
    for (int i = frame.count - 1; i >= 0; i--) {
        if (is_unref_of_moved(g, frame.items[i])) continue;
        gen_stmt(g, frame.items[i]);
    }
    g->indent--;
    line(g, "}");
    g->defers = frame.prev;
    g->nvars = mark;
    free(frame.items);
}

static void gen_var(Gen *g, Stmt *s) {
    Type *t = s->as.var.type;
    if (!t) t = type_named("int");
    record_var(g, s->as.var.name, t);
    if (t->kind == TY_NAMED && is_managed_name(t->name)) {
        if (s->as.var.init) {
            char *v = gen_expr(g, s->as.var.init);
            line(g, "AktShared* %s = %s;", s->as.var.name, v);
            free(v);
        } else {
            line(g, "AktShared* %s = NULL;", s->as.var.name);
        }
        Expr *mid = mk_ident(s->as.var.name);
        defer_push(g, mk_call_stmt1("akt_shared_unref", mid));
        return;
    }
    if (t->kind == TY_SHARED || t->kind == TY_WEAK) {
        Type *base = t->elem ? t->elem : type_named("int");
        int id = g->tmp++;
        if (t->kind == TY_WEAK && s->as.var.init && s->as.var.init->kind == EX_IDENT &&
            s->as.var.init->shared_ref) {
            line(g, "AktShared* %s = %s;", s->as.var.name, s->as.var.init->as.sval);
            line(g, "akt_shared_weak_ref(%s);", s->as.var.name);
            Expr *id = mk_ident(s->as.var.name);
            defer_push(g, mk_call_stmt1("akt_shared_weak_unref", id));
            return;
        }
        if (t->kind == TY_WEAK && !s->as.var.init) {
            line(g, "AktShared* %s = NULL;", s->as.var.name);
            Expr *wid = mk_ident(s->as.var.name);
            defer_push(g, mk_call_stmt1("akt_shared_weak_unref", wid));
            return;
        }
        if (s->as.var.init) {
            char *v = gen_expr(g, s->as.var.init);
            line(g, "%s _tmp%d = %s;", c_type(base), id, v);
            free(v);
            line(g, "AktShared* %s = akt_shared_new(sizeof(%s), &_tmp%d);", s->as.var.name, c_type(base), id);
        } else {
            line(g, "AktShared* %s = akt_shared_new(sizeof(%s), NULL);", s->as.var.name, c_type(base));
        }
        if (t->kind == TY_SHARED) {
            Expr *id = mk_ident(s->as.var.name);
            Stmt *unref = mk_call_stmt1("akt_shared_unref", id);
            defer_push(g, unref);
        }
        return;
    }
    if (t->kind == TY_NAMED && strcmp(t->name, "arena") == 0 && !s->as.var.init) {
        line(g, "AktArena %s = akt_arena_new(64 * 1024);", s->as.var.name);
        Expr *id = mk_ident(s->as.var.name);
        Stmt *free_stmt = mk_call_stmt1("akt_arena_free", mk_amp(id));
        defer_push(g, free_stmt);
        return;
    }
    if (t->kind == TY_VECTOR && s->as.var.init && s->as.var.init->kind == EX_ARRAY) {
        Expr *a = s->as.var.init;
        line(g, "AktVec %s = akt_vec_new(sizeof(%s));", s->as.var.name, c_type(t->elem));
        for (int i = 0; i < a->as.array.count; i++) {
            char *v = gen_expr(g, a->as.array.items[i]);
            line(g, "{ %s _tmp = %s; akt_vec_push(&%s, &_tmp); }", c_type(t->elem), v, s->as.var.name);
            free(v);
        }
        return;
    }
    if (t->kind == TY_MAP && s->as.var.init && s->as.var.init->kind == EX_MAP) {
        Expr *m = s->as.var.init;
        line(g, "AktMap %s = akt_map_new(sizeof(%s));", s->as.var.name, c_type(t->elem2));
        for (int i = 0; i < m->as.map.count; i++) {
            char *k = gen_expr(g, m->as.map.keys[i]);
            char *v = gen_expr(g, m->as.map.vals[i]);
            line(g, "{ %s _tmp = %s; akt_map_set(&%s, %s, &_tmp); }", c_type(t->elem2), v, s->as.var.name, k);
            free(k); free(v);
        }
        return;
    }
    if (t->kind == TY_SET && s->as.var.init && s->as.var.init->kind == EX_ARRAY) {
        Expr *a = s->as.var.init;
        line(g, "AktSet %s = akt_set_new(sizeof(%s));", s->as.var.name, c_type(t->elem));
        for (int i = 0; i < a->as.array.count; i++) {
            char *v = gen_expr(g, a->as.array.items[i]);
            line(g, "{ %s _tmp = %s; akt_set_add(&%s, &_tmp); }", c_type(t->elem), v, s->as.var.name);
            free(v);
        }
        return;
    }
    if (t->kind == TY_ARRAY) {
        if (!s->as.var.init) {
            line(g, "%s %s[%lld];", c_type(t->elem), s->as.var.name, t->array_len);
            return;
        }
        if (s->as.var.init->kind == EX_ARRAY) {
            Buf b = {0};
            bprintf(&b, "%s %s[%lld] = {", c_type(t->elem), s->as.var.name, t->array_len);
            for (int i = 0; i < s->as.var.init->as.array.count; i++) {
                if (i) bput(&b, ", ");
                bput(&b, gen_expr(g, s->as.var.init->as.array.items[i]));
            }
            bput(&b, "};");
            line(g, "%s", b.data);
            bfree(&b);
            return;
        }
    }
    if (s->as.var.init) {
        char *v = gen_expr(g, s->as.var.init);
        line(g, "%s %s = %s;", c_type(t), s->as.var.name, v);
        free(v);
    } else {
        line(g, "%s %s;", c_type(t), s->as.var.name);
    }
}

static void gen_forin(Gen *g, Stmt *s) {
    Expr *it = s->as.forin.iter;
    Type *t = it->type;
    int id = g->tmp++;
    if (it->kind == EX_RANGE) {
        char *a = gen_expr(g, it->as.range.start);
        char *b = gen_expr(g, it->as.range.end);
        char *st = it->as.range.step ? gen_expr(g, it->as.range.step) : xstrdup("1");
        line(g, "for (int64_t %s = %s; %s %s %s; %s += %s) {",
             s->as.forin.name, a, s->as.forin.name,
             it->as.range.inclusive ? "<=" : "<", b, s->as.forin.name, st);
        free(a); free(b); free(st);
        g->indent++;
        gen_stmt(g, s->as.forin.body);
        g->indent--;
        line(g, "}");
        return;
    }
    if (t && t->kind == TY_ARRAY) {
        char *arr = gen_expr(g, it);
        line(g, "for (size_t _i%d = 0; _i%d < (size_t)%lld; _i%d++) {", id, id, t->array_len, id);
        g->indent++;
        if (s->as.forin.index_name) line(g, "int64_t %s = (int64_t)_i%d;", s->as.forin.index_name, id);
        line(g, "%s %s = (%s)[_i%d];", c_type(t->elem), s->as.forin.name, arr, id);
        gen_stmt(g, s->as.forin.body);
        g->indent--;
        line(g, "}");
        free(arr);
        return;
    }
    if (t && t->kind == TY_VECTOR) {
        char *v = gen_expr(g, it);
        line(g, "for (size_t _i%d = 0; _i%d < akt_vec_len(&(%s)); _i%d++) {", id, id, v, id);
        g->indent++;
        if (s->as.forin.index_name) line(g, "int64_t %s = (int64_t)_i%d;", s->as.forin.index_name, id);
        line(g, "%s %s = *(%s*)akt_vec_get(&(%s), _i%d);", c_type(t->elem), s->as.forin.name,
             c_type(t->elem), v, id);
        gen_stmt(g, s->as.forin.body);
        g->indent--;
        line(g, "}");
        free(v);
        return;
    }
    if (t && t->kind == TY_SET) {
        char *v = gen_expr(g, it);
        line(g, "for (size_t _i%d = 0; _i%d < (%s).items.len; _i%d++) {", id, id, v, id);
        g->indent++;
        if (s->as.forin.index_name) line(g, "int64_t %s = (int64_t)_i%d;", s->as.forin.index_name, id);
        line(g, "%s %s = *(%s*)akt_vec_get(&(%s).items, _i%d);", c_type(t->elem), s->as.forin.name,
             c_type(t->elem), v, id);
        gen_stmt(g, s->as.forin.body);
        g->indent--;
        line(g, "}");
        free(v);
        return;
    }
    if (t && t->kind == TY_STRING) {
        char *v = gen_expr(g, it);
        line(g, "for (size_t _i%d = 0; _i%d < (%s).len; _i%d++) {", id, id, v, id);
        g->indent++;
        if (s->as.forin.index_name) line(g, "int64_t %s = (int64_t)_i%d;", s->as.forin.index_name, id);
        line(g, "uint32_t %s = akt_str_char_at(%s, _i%d);", s->as.forin.name, v, id);
        gen_stmt(g, s->as.forin.body);
        g->indent--;
        line(g, "}");
        free(v);
        return;
    }
    fatal_at(g->prog->file, s->line, s->col, "for-in over unsupported type");
}

static void gen_switch(Gen *g, Stmt *s) {
    int id = g->tmp++;
    Expr *v = s->as.switch_stmt.value;
    Type *t = v->type;
    char *val = gen_expr(g, v);
    line(g, "{");
    g->indent++;
    line(g, "%s _sw%d = %s;", c_type(t), id, val);
    free(val);
    int opened = 0;
    for (int i = 0; i < s->as.switch_stmt.ncases; i++) {
        SwitchCase *sc = &s->as.switch_stmt.cases[i];
        if (sc->is_default) continue;
        char cond[512];
        if (sc->enum_type) {
            snprintf(cond, sizeof(cond), "_sw%d.tag == %s_%s", id, sc->enum_type, sc->enum_variant);
        } else if (sc->pattern) {
            if (strcmp(sc->pattern, "Some") == 0 || strcmp(sc->pattern, "Ok") == 0)
                snprintf(cond, sizeof(cond), "_sw%d.%s", id,
                         strcmp(sc->pattern, "Some") == 0 ? "is_some" : "is_ok");
            else
                snprintf(cond, sizeof(cond), "!_sw%d.%s", id,
                         strcmp(sc->pattern, "None") == 0 ? "is_some" : "is_ok");
        } else {
            char *m = sc->match->kind == EX_RANGE ? NULL : gen_expr(g, sc->match);
            if (sc->match->kind == EX_RANGE) {
                char *a = gen_expr(g, sc->match->as.range.start);
                char *b = gen_expr(g, sc->match->as.range.end);
                snprintf(cond, sizeof(cond), "_sw%d %s %s && _sw%d %s %s", id,
                         sc->match->as.range.inclusive ? ">=" : ">", a,
                         id, sc->match->as.range.inclusive ? "<=" : "<", b);
                free(a); free(b);
            } else if (t && t->kind == TY_STRING)
                snprintf(cond, sizeof(cond), "akt_str_eq(_sw%d, %s)", id, m);
            else
                snprintf(cond, sizeof(cond), "_sw%d == %s", id, m);
            if (m) free(m);
        }
        if (opened) line(g, "} else if (%s) {", cond);
        else line(g, "if (%s) {", cond);
        opened = 1;
        g->indent++;
        if (sc->enum_type) {
            EnumDecl *ed = NULL;
            for (int ei = 0; ei < g->prog->nenums; ei++)
                if (strcmp(g->prog->enums[ei]->name, sc->enum_type) == 0) ed = g->prog->enums[ei];
            if (ed) {
                int vi = 0;
                for (int j = 0; j < ed->nvariants; j++)
                    if (strcmp(ed->variants[j], sc->enum_variant) == 0) vi = j;
                for (int b = 0; b < sc->nbindings; b++)
                    line(g, "%s %s = _sw%d.data.%s.%s;", c_type(ed->variant_types[vi][b]),
                         sc->bindings[b], id, sc->enum_variant, ed->variant_field_names[vi][b]);
            }
        } else if (sc->binding) {
            Type *bt = NULL;
            if (t && t->kind == TY_OPTION) bt = t->elem;
            else if (t && t->kind == TY_RESULT)
                bt = (strcmp(sc->pattern, "Ok") == 0) ? t->elem : t->elem2;
            if (!bt) bt = type_named("int");
            line(g, "%s %s = *(%s*)_sw%d.value;", c_type(bt), sc->binding, c_type(bt), id);
        }
        gen_stmt(g, sc->body);
        g->indent--;
    }
    for (int i = 0; i < s->as.switch_stmt.ncases; i++) {
        SwitchCase *sc = &s->as.switch_stmt.cases[i];
        if (!sc->is_default) continue;
        if (opened) line(g, "} else {");
        else line(g, "{");
        opened = 1;
        g->indent++;
        gen_stmt(g, sc->body);
        g->indent--;
    }
    if (opened) line(g, "}");
    g->indent--;
    line(g, "}");
}

static void gen_stmt(Gen *g, Stmt *s) {
    if (!s) return;
    switch (s->kind) {
        case ST_VAR: gen_var(g, s); break;
        case ST_ASSIGN: {
            Type *dt = s->as.assign.target->kind == EX_IDENT
                           ? decl_type(g, s->as.assign.target->as.sval)
                           : NULL;
            if (dt && dt->kind == TY_NAMED && is_managed_name(dt->name) &&
                s->as.assign.op == TK_ASSIGN) {
                char *v = gen_expr(g, s->as.assign.value);
                line(g, "akt_shared_unref(%s); %s = %s;",
                     s->as.assign.target->as.sval, s->as.assign.target->as.sval, v);
                free(v);
                break;
            }
            if (dt && dt->kind == TY_WEAK && s->as.assign.op == TK_ASSIGN &&
                s->as.assign.value->kind == EX_IDENT && s->as.assign.value->shared_ref) {
                line(g, "akt_shared_weak_unref(%s); %s = %s; akt_shared_weak_ref(%s);",
                     s->as.assign.target->as.sval, s->as.assign.target->as.sval,
                     s->as.assign.value->as.sval, s->as.assign.target->as.sval);
                break;
            }
            char *t = gen_expr(g, s->as.assign.target);
            char *v = gen_expr(g, s->as.assign.value);
            Type *tt = s->as.assign.target->type;
            if (s->as.assign.op == TK_ASSIGN) {
                line(g, "%s = %s;", t, v);
            } else if (tt && tt->kind == TY_STRING && s->as.assign.op == TK_PLUSEQ) {
                line(g, "%s = akt_str_concat(%s, %s);", t, t, v);
            } else {
                const char *op = "=";
                switch (s->as.assign.op) {
                    case TK_PLUSEQ: op = "+="; break;
                    case TK_MINUSEQ: op = "-="; break;
                    case TK_STAREQ: op = "*="; break;
                    case TK_SLASHEQ: op = "/="; break;
                    case TK_PERCENTEQ: op = "%="; break;
                }
                line(g, "%s %s %s;", t, op, v);
            }
            free(t); free(v);
            break;
        }
        case ST_EXPR: {
            char *v = gen_expr(g, s->as.expr);
            line(g, "%s;", v);
            free(v);
            break;
        }
        case ST_RETURN: {
            if (s->as.ret) {
                if (s->as.ret->kind == EX_IDENT) {
                    Type *rt = decl_type(g, s->as.ret->as.sval);
                    if (rt && rt->kind == TY_NAMED && is_managed_name(rt->name))
                        mark_moved(g, s->as.ret->as.sval);
                }
                char *v = gen_expr(g, s->as.ret);
                emit_all_defers(g);
                line(g, "return %s;", v);
                free(v);
            } else {
                emit_all_defers(g);
                line(g, "return;");
            }
            break;
        }
        case ST_IF: {
            char *c = gen_expr(g, s->as.if_stmt.cond);
            line(g, "if (%s) {", c);
            free(c);
            g->indent++;
            gen_stmt(g, s->as.if_stmt.then_stmt);
            g->indent--;
            if (s->as.if_stmt.else_stmt) {
                line(g, "} else {");
                g->indent++;
                gen_stmt(g, s->as.if_stmt.else_stmt);
                g->indent--;
            }
            line(g, "}");
            break;
        }
        case ST_WHILE: {
            char *c = gen_expr(g, s->as.while_stmt.cond);
            line(g, "while (%s) {", c);
            free(c);
            g->indent++;
            gen_stmt(g, s->as.while_stmt.body);
            g->indent--;
            line(g, "}");
            break;
        }
        case ST_FOR: {
            line(g, "{");
            g->indent++;
            if (s->as.for_stmt.init) gen_stmt(g, s->as.for_stmt.init);
            char *c = s->as.for_stmt.cond ? gen_expr(g, s->as.for_stmt.cond) : xstrdup("1");
            char *st = s->as.for_stmt.step ? gen_expr(g, s->as.for_stmt.step) : xstrdup("");
            line(g, "for (; %s; %s) {", c, st);
            free(c); free(st);
            g->indent++;
            gen_stmt(g, s->as.for_stmt.body);
            g->indent--;
            line(g, "}");
            g->indent--;
            line(g, "}");
            break;
        }
        case ST_FORIN: gen_forin(g, s); break;
        case ST_BLOCK: gen_block(g, s); break;
        case ST_SWITCH: gen_switch(g, s); break;
        case ST_BREAK:
            emit_all_defers(g);
            line(g, "break;");
            break;
        case ST_CONTINUE:
            emit_all_defers(g);
            line(g, "continue;");
            break;
        case ST_DEFER: defer_push(g, s->as.defer_stmt); break;
    }
}

static void gen_struct(Gen *g, StructDecl *s) {
    if (s->from_header) return;
    if (s->is_managed) {
        line(g, "typedef struct %s_s {", s->name);
        g->indent++;
        for (int i = 0; i < s->nfields; i++) {
            Field *f = &s->fields[i];
            if (f->type->kind == TY_ARRAY)
                line(g, "%s %s[%lld];", c_type(f->type->elem), f->name, f->type->array_len);
            else
                line(g, "%s %s;", c_type(f->type), f->name);
        }
        if (s->nfields == 0) line(g, "char _empty;");
        g->indent--;
        line(g, "} %s_s;", s->name);
        line(g, "");
        return;
    }
    line(g, "typedef %s %s {", s->is_cunion ? "union" : "struct", s->name);
    g->indent++;
    for (int i = 0; i < s->nfields; i++) {
        Field *f = &s->fields[i];
        if (f->type->kind == TY_ARRAY) {
            line(g, "%s %s[%lld];", c_type(f->type->elem), f->name, f->type->array_len);
        } else {
            char *decl = type_c_decl(f->type, f->name);
            line(g, "%s;", decl);
            free(decl);
        }
    }
    if (s->nfields == 0) line(g, "char _empty;");
    g->indent--;
    line(g, "} %s;", s->name);
    line(g, "");
}

static const char *func_c_name(Func *f) {
    static char buf[512];
    if (f->owner) {
        snprintf(buf, sizeof(buf), "%s_%s", f->owner->name, f->name);
        return buf;
    }
    if (strcmp(f->name, "main") == 0) return "akt_user_main";
    return f->name;
}

static void emit_param(Gen *g, Type *t, const char *name) {
    if (t && t->kind == TY_CFUNC && t->name) {
        const char *p = strstr(t->name, "(*)");
        if (p) {
            emit(g, "%.*s(*%s)%s", (int)(p - t->name), t->name, name, p + 3);
            return;
        }
    }
    emit(g, "%s %s", c_type(t), name);
}

static void gen_proto(Gen *g, Func *f) {
    if (f->is_cfunc && f->is_declared) return;
    emit(g, "%s %s(", c_type(f->ret), func_c_name(f));
    int first = 1;
    if (f->is_method) {
        if (f->owner->is_managed) emit(g, "%s_s* self", f->owner->name);
        else emit(g, "%s* self", f->owner->name);
        first = 0;
    }
    for (int i = 0; i < f->nparams; i++) {
        if (!first) emit(g, ", ");
        emit_param(g, f->params[i].type, f->params[i].name);
        first = 0;
    }
    if (f->is_variadic) {
        if (!first) emit(g, ", ");
        emit(g, "...");
        first = 0;
    }
    if (first) emit(g, "void");
    emit(g, ");\n");
    if (f->is_async) {
        emit(g, "AktTask* %s_task(", func_c_name(f));
        first = 1;
        for (int i = 0; i < f->nparams; i++) {
            if (!first) emit(g, ", ");
            emit(g, "%s %s", c_type(f->params[i].type), f->params[i].name);
            first = 0;
        }
        if (first) emit(g, "void");
        emit(g, ");\n");
    }
}

static void gen_func(Gen *g, Func *f) {
    if (!f->body) return;
    g->current = f;
    g->indent = 0;
    g->nvars = 0;
    g->nmoved = 0;
    if (f->is_method) record_var(g, "self", type_ref(type_named(f->owner->name)));
    for (int i = 0; i < f->nparams; i++) record_var(g, f->params[i].name, f->params[i].type);
    emit(g, "%s %s(", c_type(f->ret), func_c_name(f));
    int first = 1;
    if (f->is_method) {
        if (f->owner->is_managed) emit(g, "%s_s* self", f->owner->name);
        else emit(g, "%s* self", f->owner->name);
        first = 0;
    }
    for (int i = 0; i < f->nparams; i++) {
        if (!first) emit(g, ", ");
        emit_param(g, f->params[i].type, f->params[i].name);
        first = 0;
    }
    if (first) emit(g, "void");
    emit(g, ") {\n");
    g->indent++;
    gen_stmt(g, f->body);
    if (f->ret && !type_is_void(f->ret) && !f->owner && strcmp(f->name, "main") == 0 &&
        f->ret->kind == TY_INT)
        line(g, "return 0;");
    g->indent--;
    emit(g, "}\n\n");
}

static void gen_async_decls(Gen *g) {
    for (int i = 0; i < g->prog->nfuncs; i++) {
        Func *f = g->prog->funcs[i];
        if (!f->is_async || f->owner) continue;
        const char *cn = func_c_name(f);
        line(g, "typedef struct %s_ctx {", cn);
        g->indent++;
        for (int p = 0; p < f->nparams; p++)
            line(g, "%s %s;", c_type(f->params[p].type), f->params[p].name);
        if (f->nparams == 0) line(g, "char _unused;");
        g->indent--;
        line(g, "} %s_ctx;", cn);
        line(g, "static void* %s_tramp(void* _p) {", cn);
        g->indent++;
        line(g, "%s_ctx* c = (%s_ctx*)_p;", cn, cn);
        emit(g, "    ");
        if (f->ret && f->ret->kind != TY_VOID)
            emit(g, "%s _r = %s(", c_type(f->ret), cn);
        else
            emit(g, "%s(", cn);
        for (int p = 0; p < f->nparams; p++) {
            if (p) emit(g, ", ");
            emit(g, "c->%s", f->params[p].name);
        }
        emit(g, ");\n");
        if (f->ret && f->ret->kind != TY_VOID) {
            line(g, "%s* _out = (%s*)akt_alloc(sizeof(%s));", c_type(f->ret), c_type(f->ret), c_type(f->ret));
            line(g, "*_out = _r;");
            line(g, "akt_free(c);");
            line(g, "return _out;");
        } else {
            line(g, "akt_free(c);");
            line(g, "return NULL;");
        }
        g->indent--;
        line(g, "}");
        emit(g, "AktTask* %s_task(", cn);
        for (int p = 0; p < f->nparams; p++) {
            if (p) emit(g, ", ");
            emit(g, "%s %s", c_type(f->params[p].type), f->params[p].name);
        }
        if (f->nparams == 0) emit(g, "void");
        emit(g, ") {\n");
        g->indent++;
        line(g, "%s_ctx* c = (%s_ctx*)akt_alloc(sizeof(%s_ctx));", cn, cn, cn);
        for (int p = 0; p < f->nparams; p++)
            line(g, "c->%s = %s;", f->params[p].name, f->params[p].name);
        line(g, "return akt_task_start(%s_tramp, c);", cn);
        g->indent--;
        emit(g, "}\n\n");
    }
}

static void gen_globals(Gen *g) {
    for (int i = 0; i < g->prog->nglobals; i++) {
        Stmt *s = g->prog->globals[i];
        if (s->as.var.is_c_macro) continue;
        Type *t = s->as.var.type;
        if (t && t->kind == TY_ARRAY) {
            if (s->as.var.init && s->as.var.init->kind == EX_ARRAY) {
                Buf b = {0};
                bprintf(&b, "%s %s[%lld] = {", c_type(t->elem), s->as.var.name, t->array_len);
                for (int j = 0; j < s->as.var.init->as.array.count; j++) {
                    if (j) bput(&b, ", ");
                    bput(&b, gen_expr(g, s->as.var.init->as.array.items[j]));
                }
                bput(&b, "};");
                line(g, "%s", b.data);
                bfree(&b);
            } else {
                line(g, "%s %s[%lld];", c_type(t->elem), s->as.var.name, t->array_len);
            }
            continue;
        }
        if (s->as.var.init) {
            char *v = gen_expr(g, s->as.var.init);
            line(g, "%s %s = %s;", c_type(t), s->as.var.name, v);
            free(v);
        } else {
            line(g, "%s %s;", c_type(t), s->as.var.name);
        }
    }
    if (g->prog->nglobals) line(g, "");
}

static void scan_expr_thr(Gen *g, Expr *e);
static void scan_stmt_thr(Gen *g, Stmt *s);

static void reg_thr(Gen *g, Expr *cl) {
    for (int i = 0; i < g->nthr; i++)
        if (g->thr[i].closure == cl) return;
    if (g->nthr >= 64) return;
    ThrRec *r = &g->thr[g->nthr];
    memset(r, 0, sizeof(*r));
    r->closure = cl;
    snprintf(r->name, sizeof(r->name), "_akt_thr_%d", g->nthr);
    g->nthr++;
}

static void scan_expr_thr(Gen *g, Expr *e) {
    if (!e) return;
    switch (e->kind) {
        case EX_CALL:
            if (e->as.call.callee && e->as.call.callee->kind == EX_MEMBER) {
                Expr *o = e->as.call.callee->as.member.obj;
                if (o && o->kind == EX_IDENT && !strcmp(o->as.sval, "thread") &&
                    !strcmp(e->as.call.callee->as.member.name, "spawn") &&
                    e->as.call.argc == 1 && e->as.call.args[0]->kind == EX_CLOSURE)
                    reg_thr(g, e->as.call.args[0]);
            }
            scan_expr_thr(g, e->as.call.callee);
            for (int i = 0; i < e->as.call.argc; i++) scan_expr_thr(g, e->as.call.args[i]);
            break;
        case EX_BINARY: scan_expr_thr(g, e->as.binary.left); scan_expr_thr(g, e->as.binary.right); break;
        case EX_UNARY: scan_expr_thr(g, e->as.unary.expr); break;
        case EX_MEMBER: scan_expr_thr(g, e->as.member.obj); break;
        case EX_INDEX: scan_expr_thr(g, e->as.index.obj); scan_expr_thr(g, e->as.index.index); break;
        case EX_CAST: scan_expr_thr(g, e->as.cast.expr); break;
        case EX_TERNARY: scan_expr_thr(g, e->as.ternary.cond); scan_expr_thr(g, e->as.ternary.then_expr); scan_expr_thr(g, e->as.ternary.else_expr); break;
        case EX_COALESCE: scan_expr_thr(g, e->as.ternary.then_expr); scan_expr_thr(g, e->as.ternary.else_expr); break;
        case EX_STRUCT: for (int i = 0; i < e->as.struct_init.nfields; i++) scan_expr_thr(g, e->as.struct_init.field_vals[i]); break;
        case EX_ARRAY: for (int i = 0; i < e->as.array.count; i++) scan_expr_thr(g, e->as.array.items[i]); break;
        case EX_MAP: for (int i = 0; i < e->as.map.count; i++) { scan_expr_thr(g, e->as.map.keys[i]); scan_expr_thr(g, e->as.map.vals[i]); } break;
        case EX_RANGE: scan_expr_thr(g, e->as.range.start); scan_expr_thr(g, e->as.range.end); scan_expr_thr(g, e->as.range.step); break;
        case EX_SWITCH:
            scan_expr_thr(g, e->as.switch_expr.value);
            for (int i = 0; i < e->as.switch_expr.ncases; i++) {
                scan_expr_thr(g, e->as.switch_expr.cases[i].match);
                scan_expr_thr(g, e->as.switch_expr.values[i]);
                scan_stmt_thr(g, e->as.switch_expr.cases[i].body);
            }
            break;
        case EX_FSTRING: for (int i = 0; i < e->as.fstring.nparts - 1; i++) scan_expr_thr(g, e->as.fstring.exprs[i]); break;
        case EX_CLOSURE: scan_stmt_thr(g, e->as.closure.block); break;
        default: break;
    }
}

static void scan_stmt_thr(Gen *g, Stmt *s) {
    if (!s) return;
    switch (s->kind) {
        case ST_VAR: scan_expr_thr(g, s->as.var.init); break;
        case ST_ASSIGN: scan_expr_thr(g, s->as.assign.target); scan_expr_thr(g, s->as.assign.value); break;
        case ST_EXPR: scan_expr_thr(g, s->as.expr); break;
        case ST_RETURN: scan_expr_thr(g, s->as.ret); break;
        case ST_IF: scan_expr_thr(g, s->as.if_stmt.cond); scan_stmt_thr(g, s->as.if_stmt.then_stmt); scan_stmt_thr(g, s->as.if_stmt.else_stmt); break;
        case ST_WHILE: scan_expr_thr(g, s->as.while_stmt.cond); scan_stmt_thr(g, s->as.while_stmt.body); break;
        case ST_FOR: scan_stmt_thr(g, s->as.for_stmt.init); scan_expr_thr(g, s->as.for_stmt.cond); scan_expr_thr(g, s->as.for_stmt.step); scan_stmt_thr(g, s->as.for_stmt.body); break;
        case ST_FORIN: scan_expr_thr(g, s->as.forin.iter); scan_stmt_thr(g, s->as.forin.body); break;
        case ST_BLOCK: for (int i = 0; i < s->as.block.count; i++) scan_stmt_thr(g, s->as.block.items[i]); break;
        case ST_DEFER: scan_stmt_thr(g, s->as.defer_stmt); break;
        case ST_SWITCH: scan_expr_thr(g, s->as.switch_stmt.value); for (int i = 0; i < s->as.switch_stmt.ncases; i++) { scan_expr_thr(g, s->as.switch_stmt.cases[i].match); scan_stmt_thr(g, s->as.switch_stmt.cases[i].body); } break;
        default: break;
    }
}


static StructDecl *find_struct_cg(Program *p, const char *name) {
    for (int i = 0; i < p->nstructs; i++)
        if (strcmp(p->structs[i]->name, name) == 0) return p->structs[i];
    return NULL;
}

static long long abi_type_size(Program *p, Type *t, int *align, int depth);

static long long abi_struct_size(Program *p, StructDecl *s, int *align, int depth) {
    *align = 1;
    if (depth > 8) return 0;
    long long off = 0, maxsz = 0;
    int maxa = 1;
    for (int i = 0; i < s->nfields; i++) {
        int a = 0;
        long long sz = abi_type_size(p, s->fields[i].type, &a, depth + 1);
        if (sz <= 0 || a <= 0) return 0;
        if (a > maxa) maxa = a;
        if (s->is_cunion) {
            if (sz > maxsz) maxsz = sz;
        } else {
            off = (off + a - 1) / a * a;
            off += sz;
        }
    }
    long long total = s->is_cunion ? maxsz : off;
    total = (total + maxa - 1) / maxa * maxa;
    *align = maxa;
    return total;
}

static long long abi_type_size(Program *p, Type *t, int *align, int depth) {
    *align = 1;
    if (!t) return 0;
    switch (t->kind) {
        case TY_BOOL: case TY_CCHAR: case TY_I8: case TY_U8: *align = 1; return 1;
        case TY_I16: case TY_U16: *align = 2; return 2;
        case TY_I32: case TY_U32: case TY_F32: case TY_CHAR: *align = 4; return 4;
        case TY_I64: case TY_U64: case TY_F64: *align = 8; return 8;
        case TY_PTR: case TY_REF: *align = 8; return 8;
        case TY_ARRAY: {
            int a = 1;
            long long sz = abi_type_size(p, t->elem, &a, depth);
            *align = a;
            return sz > 0 ? sz * t->array_len : 0;
        }
        case TY_NAMED: {
            StructDecl *sd = find_struct_cg(p, t->name ? t->name : "");
            if (!sd || (!sd->is_cstruct && !sd->is_cunion)) return 0;
            return abi_struct_size(p, sd, align, depth);
        }
        default: return 0;
    }
}

static void gen_abi_asserts(Gen *g) {
    for (int i = 0; i < g->prog->nstructs; i++) {
        StructDecl *s = g->prog->structs[i];
        if (!s->from_header || s->imported) continue;
        int a = 1;
        long long sz = abi_struct_size(g->prog, s, &a, 0);
        if (sz > 0)
            line(g, "_Static_assert(sizeof(%s) == %lld, \"akt ABI: %s size mismatch\");",
                 s->name, sz, s->name);
    }
}

int generate_c(Program *prog, const char *output_path, const char *source_name) {
    FILE *out = fopen(output_path, "wb");
    if (!out) fatal("cannot open output file '%s'", output_path);
    Gen g;
    g.out = out;
    g.prog = prog;
    g.current = NULL;
    g.indent = 0;
    g.defers = NULL;
    g.tmp = 0;
    g.nvars = 0;
    g.nmarks = 0;
    g.nthr = 0;
    g.nmoved = 0;
    g_nmanaged = 0;
    for (int i = 0; i < prog->nstructs; i++)
        if (prog->structs[i]->is_managed && g_nmanaged < 128)
            g_managed[g_nmanaged++] = prog->structs[i]->name;

    fprintf(out, "/* generated by aktc %s from %s */\n", AKT_VERSION, source_name);
    fprintf(out, "#include \"akt_runtime.h\"\n");
    for (int i = 0; i < prog->nc_includes; i++)
        fprintf(out, "#include %s\n", prog->c_includes[i]);
    fprintf(out, "#include <math.h>\n\n");
    fprintf(out, "#ifndef AKT_VEC_PUSH\n");
    fprintf(out, "#define AKT_VEC_PUSH(v, x) do { __typeof__(x) _akt_tmp = (x); akt_vec_push(&(v), &_akt_tmp); } while (0)\n");
    fprintf(out, "#define AKT_VEC_POP(v, T) ({ T _akt_out; akt_vec_pop(&(v), &_akt_out); _akt_out; })\n");
    fprintf(out, "#define AKT_MAP_SET(m, k, v) do { __typeof__(v) _akt_v = (v); akt_map_set(&(m), (k), &_akt_v); } while (0)\n");
    fprintf(out, "#endif\n\n");

    for (int i = 0; i < prog->nenums; i++) {
        EnumDecl *e = prog->enums[i];
        if (!e->has_payload) {
            line(&g, "typedef enum %s {", e->name);
            g.indent++;
            for (int j = 0; j < e->nvariants; j++)
                line(&g, "%s_%s%s", e->name, e->variants[j], j + 1 < e->nvariants ? "," : "");
            g.indent--;
            line(&g, "} %s;", e->name);
            line(&g, "");
        } else {
            line(&g, "typedef enum %s_Tag {", e->name);
            g.indent++;
            for (int j = 0; j < e->nvariants; j++)
                line(&g, "%s_%s%s", e->name, e->variants[j], j + 1 < e->nvariants ? "," : "");
            g.indent--;
            line(&g, "} %s_Tag;", e->name);
            line(&g, "typedef struct %s {", e->name);
            g.indent++;
            line(&g, "%s_Tag tag;", e->name);
            line(&g, "union {");
            g.indent++;
            for (int j = 0; j < e->nvariants; j++) {
                if (e->variant_nfields[j] == 0) continue;
                line(&g, "struct {");
                g.indent++;
                for (int k = 0; k < e->variant_nfields[j]; k++)
                    line(&g, "%s %s;", c_type(e->variant_types[j][k]), e->variant_field_names[j][k]);
                g.indent--;
                line(&g, "} %s;", e->variants[j]);
            }
            g.indent--;
            line(&g, "} data;");
            g.indent--;
            line(&g, "} %s;", e->name);
            line(&g, "");
        }
    }
    for (int i = 0; i < prog->nstructs; i++) gen_struct(&g, prog->structs[i]);
    gen_abi_asserts(&g);

    gen_globals(&g);

    for (int i = 0; i < prog->nfuncs; i++) gen_proto(&g, prog->funcs[i]);
    for (int i = 0; i < prog->nstructs; i++)
        for (int j = 0; j < prog->structs[i]->nmethods; j++)
            gen_proto(&g, prog->structs[i]->methods[j]);
    gen_async_decls(&g);
    for (int i = 0; i < prog->nfuncs; i++) scan_stmt_thr(&g, prog->funcs[i]->body);
    for (int i = 0; i < prog->nstructs; i++)
        for (int j = 0; j < prog->structs[i]->nmethods; j++)
            scan_stmt_thr(&g, prog->structs[i]->methods[j]->body);
    for (int i = 0; i < g.nthr; i++) line(&g, "static void %s(void*);", g.thr[i].name);
    fprintf(out, "\n");

    for (int i = 0; i < prog->nfuncs; i++) gen_func(&g, prog->funcs[i]);
    for (int i = 0; i < prog->nstructs; i++)
        for (int j = 0; j < prog->structs[i]->nmethods; j++)
            gen_func(&g, prog->structs[i]->methods[j]);

    for (int i = 0; i < g.nthr; i++) {
        ThrRec *r = &g.thr[i];
        line(&g, "static void %s(void* _p) {", r->name);
        g.indent++;
        line(&g, "void** _c = (void**)_p;");
        for (int c = 0; c < r->ncap; c++) {
            Type *ct = r->caps[c].type;
            if (ct && (ct->kind == TY_SHARED || ct->kind == TY_WEAK))
                line(&g, "AktShared* %s = *(AktShared**)_c[%d];", r->caps[c].name, c);
            else
                line(&g, "%s %s = *(%s*)_c[%d];", c_type(ct), r->caps[c].name, c_type(ct), c);
        }
        DeferFrame *old = g.defers;
        g.defers = NULL;
        gen_stmt(&g, r->closure->as.closure.block);
        g.defers = old;
        line(&g, "akt_free(_c);");
        g.indent--;
        line(&g, "}");
        line(&g, "");
    }

    Func *mainf = NULL;
    for (int i = 0; i < prog->nfuncs; i++)
        if (!prog->funcs[i]->owner && strcmp(prog->funcs[i]->name, "main") == 0) mainf = prog->funcs[i];
    if (mainf) {
        if (mainf->ret && mainf->ret->kind == TY_RESULT) {
            fprintf(out, "int main(void) { AktResult _r = akt_user_main(); int _c = _r.is_ok ? 0 : 1; akt_runtime_shutdown(); return _c; }\n");
        } else if (mainf->nparams == 2) {
            fprintf(out, "int main(int argc, char** argv) { int _c = akt_user_main(argc, argv); akt_runtime_shutdown(); return _c; }\n");
        } else if (mainf->ret && mainf->ret->kind == TY_VOID) {
            fprintf(out, "int main(void) { akt_user_main(); akt_runtime_shutdown(); return 0; }\n");
        } else {
            fprintf(out, "int main(void) { int _c = (int)akt_user_main(); akt_runtime_shutdown(); return _c; }\n");
        }
    }
    fclose(out);
    return 0;
}
