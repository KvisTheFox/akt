#include "aktc.h"

typedef struct Var {
    char *name;
    Type *type;
    struct Var *next;
} Var;

typedef struct Scope {
    Var *vars;
    struct Scope *parent;
} Scope;

typedef struct {
    Program *prog;
    Scope *scope;
    Scope *globals;
    Func *current;
    int loop_depth;
} Checker;

static Type *check_expr(Checker *c, Expr *e);
static void check_stmt(Checker *c, Stmt *s);

static Scope *scope_new(Scope *parent) {
    Scope *s = (Scope *)xcalloc(1, sizeof(Scope));
    s->parent = parent;
    return s;
}

static Var *scope_find(Scope *s, const char *name) {
    for (; s; s = s->parent)
        for (Var *v = s->vars; v; v = v->next)
            if (strcmp(v->name, name) == 0) return v;
    return NULL;
}

static Type *scope_type(Scope *s, const char *name) {
    Var *v = scope_find(s, name);
    return v ? v->type : NULL;
}

static void scope_define(Scope *s, const char *name, Type *type) {
    Var *v = (Var *)xcalloc(1, sizeof(Var));
    v->name = xstrdup(name);
    v->type = type;
    v->next = s->vars;
    s->vars = v;
}

static Func *find_func(Program *prog, const char *name) {
    for (int i = 0; i < prog->nfuncs; i++)
        if (strcmp(prog->funcs[i]->name, name) == 0) return prog->funcs[i];
    return NULL;
}

static StructDecl *find_struct(Program *prog, const char *name) {
    const char *dot = strrchr(name, '.');
    if (dot) name = dot + 1;
    for (int i = 0; i < prog->nstructs; i++)
        if (strcmp(prog->structs[i]->name, name) == 0) return prog->structs[i];
    return NULL;
}

static EnumDecl *find_enum(Program *prog, const char *name) {
    const char *dot = strrchr(name, '.');
    if (dot) name = dot + 1;
    for (int i = 0; i < prog->nenums; i++)
        if (strcmp(prog->enums[i]->name, name) == 0) return prog->enums[i];
    return NULL;
}

static Field *find_field(StructDecl *s, const char *name) {
    if (!s) return NULL;
    for (int i = 0; i < s->nfields; i++)
        if (strcmp(s->fields[i].name, name) == 0) return &s->fields[i];
    return NULL;
}

static Func *find_method(StructDecl *s, const char *name) {
    if (!s) return NULL;
    for (int i = 0; i < s->nmethods; i++)
        if (strcmp(s->methods[i]->name, name) == 0) return s->methods[i];
    return NULL;
}

static int is_builtin_module(const char *name) {
    static const char *mods[] = {"io", "fs", "path", "env", "math", "string",
                                 "time", "process", "mem", "console", "fmt", "c",
                                 "random", "hash", "base64", "json", "regex", "thread", "http", "sqlite", "async", NULL};
    for (int i = 0; mods[i]; i++)
        if (strcmp(mods[i], name) == 0) return 1;
    return 0;
}

static Type *t_int(void) { return type_named("int"); }
static Type *t_f64(void) { return type_named("f64"); }
static Type *t_bool(void) { return type_named("bool"); }
static Type *t_string(void) { return type_named("string"); }
static Type *t_void(void) { return type_named("void"); }
static int is_managed_type(Program *p, Type *t) {
    if (!t || t->kind != TY_NAMED || !t->name) return 0;
    StructDecl *s = find_struct(p, t->name);
    return s && s->is_managed;
}

static void require_assignable(Checker *c, Type *target, Type *value, Expr *e, const char *what) {
    (void)c;
    if (!target || !value) return;
    if (type_equal(target, value)) return;
    if (type_is_number(target) && type_is_number(value)) return;
    if (target->kind == TY_STRING && value->kind == TY_STRING) return;
    if (value->kind == TY_STRING && type_is_cstring(target)) return;
    if (target->kind == TY_CFUNC && value->kind == TY_CFUNC) return;
    if ((target->kind == TY_PTR || target->kind == TY_REF) &&
        (value->kind == TY_PTR || value->kind == TY_REF)) return;
    if (target->kind == TY_REF && value->kind == TY_PTR) return;
    if (target->kind == TY_VECTOR && value->kind == TY_ARRAY) return;
    if (target->kind == TY_ARRAY && value->kind == TY_ARRAY) {
        if (target->array_len == value->array_len &&
            (type_equal(target->elem, value->elem) ||
             (type_is_number(target->elem) && type_is_number(value->elem))))
            return;
    }
    if (target->kind == TY_OPTION && value->kind == TY_OPTION) {
        if (!value->elem || type_equal(target->elem, value->elem)) return;
    }
    if (target->kind == TY_RESULT && value->kind == TY_RESULT) {
        if ((!value->elem || type_equal(target->elem, value->elem)) &&
            (!value->elem2 || type_equal(target->elem2, value->elem2))) return;
    }
    if (target->kind == TY_MAP && value->kind == TY_MAP) {
        if ((!value->elem || type_equal(target->elem, value->elem)) &&
            (!value->elem2 || type_equal(target->elem2, value->elem2) ||
             (type_is_number(target->elem2) && type_is_number(value->elem2)))) return;
    }
    if (target->kind == TY_SET && value->kind == TY_ARRAY) {
        if (type_equal(target->elem, value->elem) ||
            (type_is_number(target->elem) && type_is_number(value->elem))) return;
    }
    if (target->kind == TY_NAMED && value->kind == TY_NAMED) {
        if (strcmp(target->name, value->name) == 0) return;
    }
    if (target->kind == TY_UNKNOWN || value->kind == TY_UNKNOWN) return;
    fatal_at(c->current ? c->current->body ? c->prog->file : c->prog->file : c->prog->file,
             e->line, e->col, "cannot use %s as %s in %s", type_name(value), type_name(target), what);
}

static Type *check_builtin_call(Checker *c, const char *name, Expr **args, int argc, Expr *call) {
    (void)c;
    if (strcmp(name, "print") == 0 || strcmp(name, "println") == 0) {
        if (argc > 1) fatal_at(c->prog->file, call->line, call->col, "%s takes 0 or 1 argument", name);
        if (argc == 1) check_expr(c, args[0]);
        return t_void();
    }
    if (strcmp(name, "len") == 0) {
        if (argc != 1) fatal_at(c->prog->file, call->line, call->col, "len takes 1 argument");
        Type *t = check_expr(c, args[0]);
        if (!t || !(t->kind == TY_STRING || t->kind == TY_ARRAY || t->kind == TY_VECTOR || t->kind == TY_SLICE))
            fatal_at(c->prog->file, call->line, call->col, "len() expects string, array or Vector, got %s", type_name(t));
        return t_int();
    }
    if (strcmp(name, "str") == 0 || strcmp(name, "to_string") == 0) {
        if (argc != 1) fatal_at(c->prog->file, call->line, call->col, "str takes 1 argument");
        check_expr(c, args[0]);
        return t_string();
    }
    if (strcmp(name, "int") == 0) {
        if (argc != 1) fatal_at(c->prog->file, call->line, call->col, "int takes 1 argument");
        check_expr(c, args[0]);
        return t_int();
    }
    if (strcmp(name, "float") == 0) {
        if (argc != 1) fatal_at(c->prog->file, call->line, call->col, "float takes 1 argument");
        check_expr(c, args[0]);
        return t_f64();
    }
    if (strcmp(name, "assert") == 0) {
        if (argc < 1) fatal_at(c->prog->file, call->line, call->col, "assert takes a condition");
        for (int i = 0; i < argc; i++) check_expr(c, args[i]);
        return t_void();
    }
    if (strcmp(name, "assert_eq") == 0 || strcmp(name, "assert_ne") == 0) {
        if (argc < 2) fatal_at(c->prog->file, call->line, call->col, "%s takes two values", name);
        for (int i = 0; i < argc; i++) check_expr(c, args[i]);
        return t_void();
    }
    if (strcmp(name, "panic") == 0) {
        if (argc != 1) fatal_at(c->prog->file, call->line, call->col, "panic takes 1 string");
        check_expr(c, args[0]);
        return t_void();
    }
    if (strcmp(name, "Some") == 0) {
        if (argc != 1) fatal_at(c->prog->file, call->line, call->col, "Some takes 1 value");
        Type *v = check_expr(c, args[0]);
        Type *o = type_new(TY_OPTION, "Option");
        o->elem = v;
        return o;
    }
    if (strcmp(name, "None") == 0) {
        Type *o = type_new(TY_OPTION, "Option");
        return o;
    }
    if (strcmp(name, "Ok") == 0) {
        if (argc != 1) fatal_at(c->prog->file, call->line, call->col, "Ok takes 1 value");
        Type *v = check_expr(c, args[0]);
        Type *r = type_new(TY_RESULT, "Result");
        r->elem = v;
        return r;
    }
    if (strcmp(name, "Err") == 0) {
        if (argc != 1) fatal_at(c->prog->file, call->line, call->col, "Err takes 1 value");
        Type *v = check_expr(c, args[0]);
        Type *r = type_new(TY_RESULT, "Result");
        r->elem2 = v;
        return r;
    }
    if (strcmp(name, "sqrt") == 0 || strcmp(name, "sin") == 0 || strcmp(name, "cos") == 0 ||
        strcmp(name, "tan") == 0 || strcmp(name, "floor") == 0 || strcmp(name, "ceil") == 0 ||
        strcmp(name, "round") == 0 || strcmp(name, "abs") == 0) {
        if (argc != 1) fatal_at(c->prog->file, call->line, call->col, "%s takes 1 argument", name);
        check_expr(c, args[0]);
        return t_f64();
    }
    if (strcmp(name, "pow") == 0 || strcmp(name, "min") == 0 || strcmp(name, "max") == 0) {
        if (argc != 2) fatal_at(c->prog->file, call->line, call->col, "%s takes 2 arguments", name);
        check_expr(c, args[0]);
        check_expr(c, args[1]);
        return t_f64();
    }
    if (strcmp(name, "sleep_ms") == 0) {
        if (argc != 1) fatal_at(c->prog->file, call->line, call->col, "sleep_ms takes 1 argument");
        check_expr(c, args[0]);
        return t_void();
    }
    if (strcmp(name, "now_ms") == 0) return t_int();
    return NULL;
}

static Type *check_module_call(Checker *c, const char *mod, const char *fn, Expr **args, int argc, Expr *call) {
    if (strcmp(mod, "math") == 0) {
        Type *t = check_builtin_call(c, fn, args, argc, call);
        if (t) return t;
    }
    if (strcmp(mod, "io") == 0) {
        if (strcmp(fn, "read_to_string") == 0 || strcmp(fn, "read_file") == 0) {
            if (argc != 1) fatal_at(c->prog->file, call->line, call->col, "io.%s takes a path", fn);
            check_expr(c, args[0]);
            return t_string();
        }
        if (strcmp(fn, "write_to_string") == 0 || strcmp(fn, "write_file") == 0) {
            if (argc != 2) fatal_at(c->prog->file, call->line, call->col, "io.%s takes path and data", fn);
            check_expr(c, args[0]);
            check_expr(c, args[1]);
            return t_bool();
        }
        if (strcmp(fn, "exists") == 0) {
            check_expr(c, args[0]);
            return t_bool();
        }
    }
    if (strcmp(mod, "fs") == 0) {
        if (strcmp(fn, "exists") == 0) {
            check_expr(c, args[0]);
            return t_bool();
        }
        if (strcmp(fn, "read_to_string") == 0) {
            check_expr(c, args[0]);
            return t_string();
        }
        if (strcmp(fn, "list_dir") == 0 || strcmp(fn, "walk") == 0) {
            check_expr(c, args[0]);
            return type_vector(t_string());
        }
    }
    if (strcmp(mod, "path") == 0) {
        if (strcmp(fn, "join") == 0) {
            check_expr(c, args[0]);
            check_expr(c, args[1]);
            return t_string();
        }
        if (strcmp(fn, "basename") == 0 || strcmp(fn, "dirname") == 0 ||
            strcmp(fn, "extension") == 0 || strcmp(fn, "absolute") == 0) {
            check_expr(c, args[0]);
            return t_string();
        }
    }
    if (strcmp(mod, "env") == 0) {
        if (strcmp(fn, "get") == 0) {
            check_expr(c, args[0]);
            return t_string();
        }
        if (strcmp(fn, "cwd") == 0) return t_string();
        if (strcmp(fn, "args") == 0) return type_array(t_string(), 0);
    }
    if (strcmp(mod, "time") == 0) {
        if (strcmp(fn, "now_ms") == 0 || strcmp(fn, "sleep_ms") == 0) return t_int();
    }
    if (strcmp(mod, "process") == 0) {
        if (strcmp(fn, "pid") == 0) return t_int();
        if (strcmp(fn, "exit") == 0) return t_void();
    }
    if (strcmp(mod, "random") == 0) {
        if (strcmp(fn, "int") == 0) { check_expr(c, args[0]); return t_int(); }
        if (strcmp(fn, "float") == 0) return t_f64();
    }
    if (strcmp(mod, "hash") == 0) {
        if (strcmp(fn, "fnv1a") == 0) { check_expr(c, args[0]); return type_named("u64"); }
    }
    if (strcmp(mod, "base64") == 0) {
        if (strcmp(fn, "encode") == 0 || strcmp(fn, "decode") == 0) {
            check_expr(c, args[0]);
            return t_string();
        }
    }
    if (strcmp(mod, "json") == 0) {
        if (strcmp(fn, "parse") == 0) {
            check_expr(c, args[0]);
            return type_named("Json");
        }
        if (strcmp(fn, "stringify") == 0) {
            check_expr(c, args[0]);
            return t_string();
        }
    }
    if (strcmp(mod, "regex") == 0) {
        if (strcmp(fn, "match") == 0) {
            check_expr(c, args[0]);
            check_expr(c, args[1]);
            return t_bool();
        }
        if (strcmp(fn, "find") == 0 || strcmp(fn, "replace") == 0) {
            for (int i = 0; i < argc; i++) check_expr(c, args[i]);
            return t_string();
        }
    }
    if (strcmp(mod, "thread") == 0) {
        if (strcmp(fn, "spawn") == 0) {
            if (argc != 1) fatal_at(c->prog->file, call->line, call->col, "thread.spawn expects a function or closure");
            if (args[0]->kind == EX_IDENT) {
                if (!find_func(c->prog, args[0]->as.sval))
                    fatal_at(c->prog->file, call->line, call->col, "unknown thread function '%s'", args[0]->as.sval);
            } else if (args[0]->kind == EX_CLOSURE) {
                if (args[0]->as.closure.nparams != 0 || !args[0]->as.closure.block)
                    fatal_at(c->prog->file, call->line, call->col, "thread closure must be || { ... }");
                check_expr(c, args[0]);
            } else {
                fatal_at(c->prog->file, call->line, call->col, "thread.spawn expects a function or closure");
            }
            return type_named("Handle");
        }
        if (strcmp(fn, "join") == 0) { check_expr(c, args[0]); return t_void(); }
        if (strcmp(fn, "sleep_ms") == 0) { check_expr(c, args[0]); return t_void(); }
    }
    if (strcmp(mod, "http") == 0) {
        if (strcmp(fn, "get") == 0 || strcmp(fn, "post") == 0) {
            for (int i = 0; i < argc; i++) check_expr(c, args[i]);
            return t_string();
        }
    }
    if (strcmp(mod, "sqlite") == 0 && strcmp(fn, "open") == 0) {
        check_expr(c, args[0]);
        return type_named("Sqlite");
    }
    if (strcmp(mod, "async") == 0 && (strcmp(fn, "all") == 0 || strcmp(fn, "select") == 0)) {
        if (argc != 1 || args[0]->kind != EX_ARRAY)
            fatal_at(c->prog->file, call->line, call->col, "async.%s expects an array of async calls", fn);
        Type *elem = NULL;
        for (int i = 0; i < args[0]->as.array.count; i++) {
            Expr *it = args[0]->as.array.items[i];
            if (it->kind != EX_CALL || it->as.call.callee->kind != EX_IDENT)
                fatal_at(c->prog->file, call->line, call->col, "async.%s expects async calls", fn);
            Func *f = find_func(c->prog, it->as.call.callee->as.sval);
            if (!f || !f->is_async)
                fatal_at(c->prog->file, call->line, call->col, "'%s' is not async", it->as.call.callee->as.sval);
            Type *rt = f->ret;
            if (!elem) elem = rt;
            else if (!type_equal(elem, rt))
                fatal_at(c->prog->file, call->line, call->col, "async.%s results have different types", fn);
            check_expr(c, it);
        }
        return strcmp(fn, "all") == 0 ? type_vector(elem ? elem : t_int()) : (elem ? elem : t_int());
    }
    if (strcmp(mod, "async") == 0 && strcmp(fn, "timeout") == 0) {
        if (argc != 2 || args[1]->kind != EX_CALL || args[1]->as.call.callee->kind != EX_IDENT)
            fatal_at(c->prog->file, call->line, call->col, "async.timeout expects (ms, async_call)");
        check_expr(c, args[0]);
        Func *f = find_func(c->prog, args[1]->as.call.callee->as.sval);
        if (!f || !f->is_async) fatal_at(c->prog->file, call->line, call->col, "timeout target is not async");
        check_expr(c, args[1]);
        Type *r = type_new(TY_RESULT, "Result");
        r->elem = f->ret;
        r->elem2 = t_string();
        return r;
    }
    if (strcmp(mod, "fmt") == 0) {
        if (strcmp(fn, "parse_int") == 0) {
            check_expr(c, args[0]);
            return t_int();
        }
        if (strcmp(fn, "parse_float") == 0) {
            check_expr(c, args[0]);
            return t_f64();
        }
    }
    if (strcmp(mod, "c") == 0) {
        if (strcmp(fn, "to_string") == 0 || strcmp(fn, "cstring") == 0) {
            check_expr(c, args[0]);
            return t_string();
        }
        for (int i = 0; i < argc; i++) check_expr(c, args[i]);
        if (strcmp(fn, "atof") == 0 || strcmp(fn, "sin") == 0 || strcmp(fn, "cos") == 0 ||
            strcmp(fn, "tan") == 0 || strcmp(fn, "sqrt") == 0 || strcmp(fn, "pow") == 0 ||
            strcmp(fn, "fabs") == 0)
            return t_f64();
        return t_int();
    }
    fatal_at(c->prog->file, call->line, call->col, "unknown function %s.%s", mod, fn);
    return NULL;
}

static Type *check_expr(Checker *c, Expr *e) {
    if (!e) return t_void();
    switch (e->kind) {
        case EX_INT: e->type = t_int(); return e->type;
        case EX_FLOAT: e->type = t_f64(); return e->type;
        case EX_STRING: e->type = t_string(); return e->type;
        case EX_CHAR: e->type = type_named("char"); return e->type;
        case EX_BOOL: e->type = t_bool(); return e->type;
        case EX_NULL: e->type = type_ptr(t_void()); return e->type;
        case EX_UNIT: e->type = t_void(); return e->type;
        case EX_FSTRING: {
            for (int i = 0; i < e->as.fstring.nparts - 1; i++) check_expr(c, e->as.fstring.exprs[i]);
            e->type = t_string();
            return e->type;
        }
        case EX_IDENT: {
            Type *t = scope_type(c->scope, e->as.sval);
            if (!t) {
                Func *f = find_func(c->prog, e->as.sval);
                if (f) {
                    e->type = type_new(TY_UNKNOWN, "fn");
                    return e->type;
                }
                if (is_builtin_module(e->as.sval)) {
                    e->type = type_new(TY_UNKNOWN, e->as.sval);
                    return e->type;
                }
                if (strcmp(e->as.sval, "PI") == 0 || strcmp(e->as.sval, "E") == 0 ||
                    strcmp(e->as.sval, "TAU") == 0 || strcmp(e->as.sval, "PHI") == 0 ||
                    strcmp(e->as.sval, "INF") == 0 || strcmp(e->as.sval, "NAN") == 0) {
                    e->type = t_f64();
                    return e->type;
                }
                if (strcmp(e->as.sval, "None") == 0) {
                    e->type = type_new(TY_OPTION, "Option");
                    return e->type;
                }
                fatal_at(c->prog->file, e->line, e->col, "unknown name '%s'", e->as.sval);
            }
            if (t->kind == TY_SHARED || t->kind == TY_WEAK) {
                e->shared_ref = 1;
                e->type = t->elem ? t->elem : t_int();
                return e->type;
            }
            e->type = t;
            return t;
        }
        case EX_BINARY: {
            Type *l = check_expr(c, e->as.binary.left);
            Type *r = check_expr(c, e->as.binary.right);
            int op = e->as.binary.op;
            if (op == TK_ANDAND || op == TK_OROR || op == TK_EQEQ || op == TK_NEQ ||
                op == TK_LT || op == TK_GT || op == TK_LE || op == TK_GE) {
                e->type = t_bool();
            } else if (op == TK_PLUS && (type_is_string(l) || type_is_string(r))) {
                e->type = t_string();
            } else if (type_is_float(l) || type_is_float(r)) {
                e->type = t_f64();
            } else {
                e->type = t_int();
            }
            return e->type;
        }
        case EX_UNARY: {
            if (e->as.unary.op == TK_AMP && e->as.unary.expr->kind == EX_IDENT) {
                Func *fn = find_func(c->prog, e->as.unary.expr->as.sval);
                if (fn) {
                    Type *ps[16];
                    int np = 0;
                    for (int i = 0; i < fn->nparams && np < 16; i++) ps[np++] = fn->params[i].type;
                    Type *ft = type_cfunc(fn->ret, ps, np);
                    e->as.unary.expr->type = ft;
                    e->type = ft;
                    return ft;
                }
            }
            Type *t = check_expr(c, e->as.unary.expr);
            int op = e->as.unary.op;
            if (op == TK_BANG || op == TK_NOT) e->type = t_bool();
            else if (op == TK_AMP) e->type = type_ptr(t);
            else if (op == TK_CARET) e->type = type_ref(t);
            else if (op == TK_STAR) e->type = (t && t->elem) ? t->elem : t_int();
            else if (op == TK_QUESTION) e->type = (t && (t->kind == TY_RESULT || t->kind == TY_OPTION)) ? t->elem : t;
            else if (op == TK_AWAIT) {
                if (t && t->kind == TY_TASK && t->elem) e->type = t->elem;
                else if (t && t->kind == TY_TASK) e->type = t_void();
                else e->type = t;
            }
            else e->type = t;
            return e->type;
        }
        case EX_CALL: {
            Expr *callee = e->as.call.callee;
            if (callee->kind == EX_IDENT) {
                Type *bt = check_builtin_call(c, callee->as.sval, e->as.call.args, e->as.call.argc, e);
                if (bt) { e->type = bt; return bt; }
                Func *f = find_func(c->prog, callee->as.sval);
                if (!f) fatal_at(c->prog->file, e->line, e->col, "unknown function '%s'", callee->as.sval);
                if (f->nparams != e->as.call.argc)
                    fatal_at(c->prog->file, e->line, e->col, "function '%s' expects %d arguments, got %d",
                             f->name, f->nparams, e->as.call.argc);
                for (int i = 0; i < e->as.call.argc; i++) {
                    Type *at = check_expr(c, e->as.call.args[i]);
                    if (is_managed_type(c->prog, at))
                        fatal_at(c->prog->file, e->line, e->col, "managed values cannot be passed by value");
                    require_assignable(c, f->params[i].type, at, e->as.call.args[i], "argument");
                }
                e->type = f->is_async ? type_task(f->ret) : f->ret;
                return e->type;
            }
            if (callee->kind == EX_MEMBER) {
                Expr *obj = callee->as.member.obj;
                if (obj->kind == EX_IDENT) {
                    StructDecl *ms = find_struct(c->prog, obj->as.sval);
                    if (ms && ms->is_managed && strcmp(callee->as.member.name, "new") == 0) {
                        if (e->as.call.argc != ms->nfields)
                            fatal_at(c->prog->file, e->line, e->col,
                                     "%s::new expects %d arguments, got %d",
                                     ms->name, ms->nfields, e->as.call.argc);
                        for (int i = 0; i < e->as.call.argc; i++) {
                            Type *at = check_expr(c, e->as.call.args[i]);
                            if (is_managed_type(c->prog, at))
                                fatal_at(c->prog->file, e->line, e->col,
                                         "managed values cannot be passed by value");
                            require_assignable(c, ms->fields[i].type, at, e->as.call.args[i], "argument");
                        }
                        e->type = type_named(ms->name);
                        return e->type;
                    }
                }
                if (obj->kind == EX_IDENT && is_builtin_module(obj->as.sval) &&
                    !scope_find(c->scope, obj->as.sval)) {
                    e->type = check_module_call(c, obj->as.sval, callee->as.member.name,
                                                e->as.call.args, e->as.call.argc, e);
                    return e->type;
                }
                if (obj->kind == EX_IDENT && !scope_find(c->scope, obj->as.sval)) {
                    Func *gf = find_func(c->prog, callee->as.member.name);
                    if (gf) {
                        if (gf->nparams != e->as.call.argc)
                            fatal_at(c->prog->file, e->line, e->col,
                                     "function '%s' expects %d arguments, got %d",
                                     gf->name, gf->nparams, e->as.call.argc);
                        for (int i = 0; i < e->as.call.argc; i++) {
                            Type *at = check_expr(c, e->as.call.args[i]);
                            if (is_managed_type(c->prog, at))
                                fatal_at(c->prog->file, e->line, e->col, "managed values cannot be passed by value");
                            require_assignable(c, gf->params[i].type, at, e->as.call.args[i], "argument");
                        }
                        e->type = gf->ret;
                        return e->type;
                    }
                }
                Type *ot0 = NULL;
                if (!(obj->kind == EX_IDENT && !scope_find(c->scope, obj->as.sval)))
                    ot0 = check_expr(c, obj);
                if (ot0 && ot0->kind == TY_NAMED && ot0->name &&
                    strcmp(ot0->name, "Mutex") == 0) {
                    const char *mn = callee->as.member.name;
                    if (strcmp(mn, "lock") == 0 || strcmp(mn, "unlock") == 0) { e->type = t_void(); return e->type; }
                }
                if (ot0 && ot0->kind == TY_NAMED && ot0->name &&
                    strcmp(ot0->name, "Sqlite") == 0) {
                    const char *mn = callee->as.member.name;
                    if (strcmp(mn, "exec") == 0) {
                        check_expr(c, e->as.call.args[0]);
                        e->type = t_bool();
                        return e->type;
                    }
                    if (strcmp(mn, "query") == 0) {
                        check_expr(c, e->as.call.args[0]);
                        e->type = type_vector(t_string());
                        return e->type;
                    }
                    if (strcmp(mn, "close") == 0) { e->type = t_void(); return e->type; }
                }
                if (ot0 && ot0->kind == TY_NAMED && ot0->name &&
                    strcmp(ot0->name, "Atomic") == 0) {
                    const char *mn = callee->as.member.name;
                    if (strcmp(mn, "load") == 0) { e->type = t_int(); return e->type; }
                    if (strcmp(mn, "store") == 0) {
                        check_expr(c, e->as.call.args[0]);
                        e->type = t_void();
                        return e->type;
                    }
                    if (strcmp(mn, "fetch_add") == 0) {
                        check_expr(c, e->as.call.args[0]);
                        e->type = t_int();
                        return e->type;
                    }
                }
                if (obj->kind == EX_IDENT && !scope_find(c->scope, obj->as.sval)) {
                    Func *gf = find_func(c->prog, callee->as.member.name);
                    if (gf) {
                        if (gf->nparams != e->as.call.argc)
                            fatal_at(c->prog->file, e->line, e->col,
                                     "function '%s' expects %d arguments, got %d",
                                     gf->name, gf->nparams, e->as.call.argc);
                        for (int i = 0; i < e->as.call.argc; i++) {
                            Type *at = check_expr(c, e->as.call.args[i]);
                            if (is_managed_type(c->prog, at))
                                fatal_at(c->prog->file, e->line, e->col, "managed values cannot be passed by value");
                            require_assignable(c, gf->params[i].type, at, e->as.call.args[i], "argument");
                        }
                        e->type = gf->ret;
                        return e->type;
                    }
                }
                Var *dv = obj->kind == EX_IDENT ? scope_find(c->scope, obj->as.sval) : NULL;
                Type *dt = dv ? dv->type : NULL;
                if (dt && dt->kind == TY_WEAK && strcmp(callee->as.member.name, "upgrade") == 0) {
                    if (e->as.call.argc != 0)
                        fatal_at(c->prog->file, e->line, e->col, "weak.upgrade takes no arguments");
                    Type *o = type_new(TY_OPTION, "Option");
                    o->elem = dt->elem ? dt->elem : t_int();
                    e->type = o;
                    return e->type;
                }
                Type *ot = check_expr(c, obj);
                if (ot && ot->kind == TY_REF) ot = ot->elem;
                if (ot && ot->kind == TY_PTR) ot = ot->elem;
                if (ot && ot->kind == TY_NAMED) {
                    StructDecl *sd = find_struct(c->prog, ot->name);
                    Func *m = find_method(sd, callee->as.member.name);
                    if (!m) fatal_at(c->prog->file, e->line, e->col, "unknown method '%s' on %s",
                                     callee->as.member.name, ot->name);
                    if (m->nparams != e->as.call.argc)
                        fatal_at(c->prog->file, e->line, e->col, "method '%s' expects %d arguments, got %d",
                                 m->name, m->nparams, e->as.call.argc);
                    for (int i = 0; i < e->as.call.argc; i++) {
                        Type *at = check_expr(c, e->as.call.args[i]);
                        if (is_managed_type(c->prog, at))
                            fatal_at(c->prog->file, e->line, e->col, "managed values cannot be passed by value");
                        require_assignable(c, m->params[i].type, at, e->as.call.args[i], "argument");
                    }
                    e->type = m->ret;
                    return e->type;
                }
                if (ot && ot->kind == TY_VECTOR) {
                    const char *mn = callee->as.member.name;
                    if (strcmp(mn, "len") == 0) { e->type = t_int(); return e->type; }
                    if (strcmp(mn, "push") == 0) {
                        if (e->as.call.argc != 1) fatal_at(c->prog->file, e->line, e->col, "push takes 1 argument");
                        check_expr(c, e->as.call.args[0]);
                        e->type = t_void();
                        return e->type;
                    }
                    if (strcmp(mn, "pop") == 0) { e->type = ot->elem; return e->type; }
                    if (strcmp(mn, "clear") == 0) { e->type = t_void(); return e->type; }
                    if (strcmp(mn, "filter") == 0) {
                        if (e->as.call.argc != 1 || e->as.call.args[0]->kind != EX_CLOSURE)
                            fatal_at(c->prog->file, e->line, e->col, "filter expects a closure");
                        Expr *cl = e->as.call.args[0];
                        if (cl->as.closure.nparams != 1)
                            fatal_at(c->prog->file, e->line, e->col, "filter closure needs 1 parameter");
                        Scope *old = c->scope;
                        c->scope = scope_new(old);
                        scope_define(c->scope, cl->as.closure.params[0], ot->elem);
                        Type *bt = check_expr(c, cl->as.closure.body);
                        if (!type_is_bool(bt))
                            fatal_at(c->prog->file, e->line, e->col, "filter closure must return bool");
                        c->scope = old;
                        e->type = type_vector(ot->elem);
                        return e->type;
                    }
                    if (strcmp(mn, "map") == 0) {
                        if (e->as.call.argc != 1 || e->as.call.args[0]->kind != EX_CLOSURE)
                            fatal_at(c->prog->file, e->line, e->col, "map expects a closure");
                        Expr *cl = e->as.call.args[0];
                        if (cl->as.closure.nparams != 1)
                            fatal_at(c->prog->file, e->line, e->col, "map closure needs 1 parameter");
                        Scope *old = c->scope;
                        c->scope = scope_new(old);
                        scope_define(c->scope, cl->as.closure.params[0], ot->elem);
                        Type *bt = check_expr(c, cl->as.closure.body);
                        c->scope = old;
                        e->type = type_vector(bt);
                        return e->type;
                    }
                    if (strcmp(mn, "reduce") == 0) {
                        if (e->as.call.argc != 2 || e->as.call.args[1]->kind != EX_CLOSURE)
                            fatal_at(c->prog->file, e->line, e->col, "reduce expects init and closure");
                        Expr *init = e->as.call.args[0];
                        Expr *cl = e->as.call.args[1];
                        if (cl->as.closure.nparams != 2)
                            fatal_at(c->prog->file, e->line, e->col, "reduce closure needs 2 parameters");
                        Type *it = check_expr(c, init);
                        Scope *old = c->scope;
                        c->scope = scope_new(old);
                        scope_define(c->scope, cl->as.closure.params[0], it);
                        scope_define(c->scope, cl->as.closure.params[1], ot->elem);
                        Type *bt = check_expr(c, cl->as.closure.body);
                        c->scope = old;
                        require_assignable(c, it, bt, cl->as.closure.body, "reduce");
                        e->type = it;
                        return e->type;
                    }
                    if (strcmp(mn, "push_back") == 0) {
                        check_expr(c, e->as.call.args[0]);
                        e->type = t_void();
                        return e->type;
                    }
                    if (strcmp(mn, "push_front") == 0) {
                        check_expr(c, e->as.call.args[0]);
                        e->type = t_void();
                        return e->type;
                    }
                    if (strcmp(mn, "front") == 0 || strcmp(mn, "back") == 0) {
                        e->type = ot->elem;
                        return e->type;
                    }
                }
                if (ot && ot->kind == TY_SET) {
                    const char *mn = callee->as.member.name;
                    if (strcmp(mn, "len") == 0) { e->type = t_int(); return e->type; }
                    if (strcmp(mn, "add") == 0) {
                        check_expr(c, e->as.call.args[0]);
                        e->type = t_void();
                        return e->type;
                    }
                    if (strcmp(mn, "contains") == 0) {
                        check_expr(c, e->as.call.args[0]);
                        e->type = t_bool();
                        return e->type;
                    }
                    if (strcmp(mn, "remove") == 0) {
                        check_expr(c, e->as.call.args[0]);
                        e->type = t_void();
                        return e->type;
                    }
                }
                if (ot && ot->kind == TY_MAP) {
                    const char *mn = callee->as.member.name;
                    if (strcmp(mn, "len") == 0) { e->type = t_int(); return e->type; }
                    if (strcmp(mn, "set") == 0) {
                        if (e->as.call.argc != 2) fatal_at(c->prog->file, e->line, e->col, "set takes 2 arguments");
                        check_expr(c, e->as.call.args[0]);
                        Type *vt = check_expr(c, e->as.call.args[1]);
                        require_assignable(c, ot->elem2, vt, e->as.call.args[1], "map value");
                        e->type = t_void();
                        return e->type;
                    }
                    if (strcmp(mn, "get") == 0) {
                        if (e->as.call.argc != 1) fatal_at(c->prog->file, e->line, e->col, "get takes 1 argument");
                        check_expr(c, e->as.call.args[0]);
                        Type *o = type_new(TY_OPTION, "Option");
                        o->elem = ot->elem2;
                        e->type = o;
                        return e->type;
                    }
                    if (strcmp(mn, "contains") == 0) {
                        check_expr(c, e->as.call.args[0]);
                        e->type = t_bool();
                        return e->type;
                    }
                    if (strcmp(mn, "remove") == 0) {
                        check_expr(c, e->as.call.args[0]);
                        e->type = t_void();
                        return e->type;
                    }
                }
                if (ot && ot->kind == TY_STRING) {
                    const char *mn = callee->as.member.name;
                    if (strcmp(mn, "len") == 0 || strcmp(mn, "byte_len") == 0 ||
                        strcmp(mn, "char_len") == 0) { e->type = t_int(); return e->type; }
                    if (strcmp(mn, "to_upper") == 0 || strcmp(mn, "to_lower") == 0 ||
                        strcmp(mn, "trim") == 0 || strcmp(mn, "reverse") == 0 ||
                        strcmp(mn, "capitalize") == 0) { e->type = t_string(); return e->type; }
                    if (strcmp(mn, "contains") == 0 || strcmp(mn, "starts_with") == 0 ||
                        strcmp(mn, "ends_with") == 0) {
                        for (int i = 0; i < e->as.call.argc; i++) check_expr(c, e->as.call.args[i]);
                        e->type = t_bool();
                        return e->type;
                    }
                    if (strcmp(mn, "find") == 0) {
                        for (int i = 0; i < e->as.call.argc; i++) check_expr(c, e->as.call.args[i]);
                        e->type = t_int();
                        return e->type;
                    }
                    if (strcmp(mn, "replace") == 0 || strcmp(mn, "repeat") == 0 ||
                        strcmp(mn, "pad_left") == 0 || strcmp(mn, "pad_right") == 0) {
                        for (int i = 0; i < e->as.call.argc; i++) check_expr(c, e->as.call.args[i]);
                        e->type = t_string();
                        return e->type;
                    }
                    if (strcmp(mn, "split") == 0) {
                        for (int i = 0; i < e->as.call.argc; i++) check_expr(c, e->as.call.args[i]);
                        e->type = type_vector(t_string());
                        return e->type;
                    }
                    if (strcmp(mn, "parse_int") == 0) { e->type = t_int(); return e->type; }
                    if (strcmp(mn, "parse_float") == 0) { e->type = t_f64(); return e->type; }
                }
                fatal_at(c->prog->file, e->line, e->col, "cannot call '%s'", callee->as.member.name);
            }
            if (callee->kind == EX_STATIC) {
                if (strcmp(callee->as.path.type_name, "Mutex") == 0 &&
                    strcmp(callee->as.path.name, "new") == 0) {
                    e->type = type_named("Mutex");
                    return e->type;
                }
                if (strcmp(callee->as.path.type_name, "Atomic") == 0 &&
                    strcmp(callee->as.path.name, "new") == 0) {
                    if (e->as.call.argc != 1) fatal_at(c->prog->file, e->line, e->col, "Atomic::new takes 1 value");
                    check_expr(c, e->as.call.args[0]);
                    e->type = type_named("Atomic");
                    return e->type;
                }
                EnumDecl *ed = find_enum(c->prog, callee->as.path.type_name);
                if (ed) {
                    for (int i = 0; i < ed->nvariants; i++) {
                        if (strcmp(ed->variants[i], callee->as.path.name) == 0) {
                            if (ed->variant_nfields[i] != e->as.call.argc)
                                fatal_at(c->prog->file, e->line, e->col,
                                         "%s::%s expects %d arguments, got %d",
                                         ed->name, ed->variants[i], ed->variant_nfields[i], e->as.call.argc);
                            for (int j = 0; j < e->as.call.argc; j++) {
                                Type *at = check_expr(c, e->as.call.args[j]);
                                require_assignable(c, ed->variant_types[i][j], at, e->as.call.args[j], "enum payload");
                            }
                            e->type = type_named(ed->name);
                            return e->type;
                        }
                    }
                }
                StructDecl *sd = find_struct(c->prog, callee->as.path.type_name);
                if (sd) {
                    if (sd->is_managed && strcmp(callee->as.path.name, "new") == 0) {
                        if (e->as.call.argc != sd->nfields)
                            fatal_at(c->prog->file, e->line, e->col,
                                     "%s::new expects %d arguments, got %d",
                                     sd->name, sd->nfields, e->as.call.argc);
                        for (int i = 0; i < e->as.call.argc; i++) {
                            Type *at = check_expr(c, e->as.call.args[i]);
                            if (is_managed_type(c->prog, at))
                                fatal_at(c->prog->file, e->line, e->col,
                                         "managed values cannot be passed by value");
                            require_assignable(c, sd->fields[i].type, at, e->as.call.args[i], "argument");
                        }
                        e->type = type_named(sd->name);
                        return e->type;
                    }
                    Func *m = find_method(sd, callee->as.path.name);
                    if (!m || !m->is_static)
                        fatal_at(c->prog->file, e->line, e->col, "unknown associated function %s::%s",
                                 callee->as.path.type_name, callee->as.path.name);
                    for (int i = 0; i < e->as.call.argc; i++) {
                        Type *at = check_expr(c, e->as.call.args[i]);
                        if (is_managed_type(c->prog, at))
                            fatal_at(c->prog->file, e->line, e->col, "managed values cannot be passed by value");
                        require_assignable(c, m->params[i].type, at, e->as.call.args[i], "argument");
                    }
                    e->type = m->ret;
                    return e->type;
                }
                fatal_at(c->prog->file, e->line, e->col, "unknown type '%s'", callee->as.path.type_name);
            }
            fatal_at(c->prog->file, e->line, e->col, "expression is not callable");
        }
        case EX_MEMBER: {
            Type *ot = check_expr(c, e->as.member.obj);
            if (ot && (ot->kind == TY_REF || ot->kind == TY_PTR)) ot = ot->elem;
            if (ot && ot->kind == TY_NAMED) {
                StructDecl *sd = find_struct(c->prog, ot->name);
                Field *f = find_field(sd, e->as.member.name);
                if (!f) fatal_at(c->prog->file, e->line, e->col, "unknown field '%s' on %s",
                                 e->as.member.name, ot->name);
                e->type = f->type;
                return e->type;
            }
            fatal_at(c->prog->file, e->line, e->col, "field access on non-struct %s", type_name(ot));
        }
        case EX_INDEX: {
            Type *ot = check_expr(c, e->as.index.obj);
            check_expr(c, e->as.index.index);
            if (ot && ot->kind == TY_ARRAY) e->type = ot->elem;
            else if (ot && ot->kind == TY_SLICE) e->type = ot->elem;
            else if (ot && ot->kind == TY_VECTOR) e->type = ot->elem;
            else if (ot && ot->kind == TY_STRING) e->type = type_named("char");
            else if (ot && ot->kind == TY_NAMED && ot->name && strcmp(ot->name, "Json") == 0)
                e->type = type_named("Json");
            else fatal_at(c->prog->file, e->line, e->col, "cannot index %s", type_name(ot));
            return e->type;
        }
        case EX_CAST: {
            check_expr(c, e->as.cast.expr);
            e->type = e->as.cast.target;
            return e->type;
        }
        case EX_TERNARY: {
            check_expr(c, e->as.ternary.cond);
            Type *a = check_expr(c, e->as.ternary.then_expr);
            Type *b = check_expr(c, e->as.ternary.else_expr);
            e->type = type_equal(a, b) ? a : (type_is_float(a) || type_is_float(b) ? t_f64() : a);
            return e->type;
        }
        case EX_COALESCE: {
            Type *a = check_expr(c, e->as.ternary.then_expr);
            check_expr(c, e->as.ternary.else_expr);
            e->type = (a && (a->kind == TY_OPTION || a->kind == TY_RESULT)) ? a->elem : a;
            return e->type;
        }
        case EX_STRUCT: {
            StructDecl *sd = find_struct(c->prog, e->as.struct_init.type_name);
            if (!sd) fatal_at(c->prog->file, e->line, e->col, "unknown struct '%s'", e->as.struct_init.type_name);
            for (int i = 0; i < e->as.struct_init.nfields; i++) {
                Field *f = find_field(sd, e->as.struct_init.field_names[i]);
                if (!f) fatal_at(c->prog->file, e->line, e->col, "unknown field '%s' in %s",
                                 e->as.struct_init.field_names[i], sd->name);
                Type *vt = check_expr(c, e->as.struct_init.field_vals[i]);
                require_assignable(c, f->type, vt, e->as.struct_init.field_vals[i], "field");
            }
            e->type = type_named(sd->name);
            return e->type;
        }
        case EX_ARRAY: {
            Type *elem = NULL;
            for (int i = 0; i < e->as.array.count; i++) {
                Type *it = check_expr(c, e->as.array.items[i]);
                if (!elem) elem = it;
            }
            if (!elem) elem = t_int();
            e->type = type_array(elem, e->as.array.count);
            return e->type;
        }
        case EX_MAP: {
            Type *kt = NULL, *vt = NULL;
            for (int i = 0; i < e->as.map.count; i++) {
                Type *k = check_expr(c, e->as.map.keys[i]);
                Type *v = check_expr(c, e->as.map.vals[i]);
                if (!kt) kt = k;
                if (!vt) vt = v;
            }
            if (!kt) kt = t_string();
            if (!vt) vt = t_int();
            e->type = type_map(kt, vt);
            return e->type;
        }
        case EX_RANGE: {
            check_expr(c, e->as.range.start);
            check_expr(c, e->as.range.end);
            if (e->as.range.step) check_expr(c, e->as.range.step);
            e->type = t_int();
            return e->type;
        }
        case EX_STATIC: {
            EnumDecl *ed = find_enum(c->prog, e->as.path.type_name);
            if (ed) {
                for (int i = 0; i < ed->nvariants; i++) {
                    if (strcmp(ed->variants[i], e->as.path.name) == 0) {
                        if (ed->variant_nfields[i] > 0)
                            fatal_at(c->prog->file, e->line, e->col,
                                     "payload variant %s::%s requires arguments",
                                     ed->name, ed->variants[i]);
                        e->type = type_named(ed->name);
                        return e->type;
                    }
                }
            }
            fatal_at(c->prog->file, e->line, e->col, "unknown enum variant %s::%s",
                     e->as.path.type_name, e->as.path.name);
        }
        case EX_CLOSURE:
            if (e->as.closure.block) {
                Scope *old = c->scope;
                c->scope = scope_new(old);
                check_stmt(c, e->as.closure.block);
                c->scope = old;
                e->type = t_void();
                return e->type;
            }
            e->type = type_new(TY_UNKNOWN, "closure");
            return e->type;
        case EX_SWITCH: {
            Type *vt = check_expr(c, e->as.switch_expr.value);
            Type *rt = NULL;
            for (int i = 0; i < e->as.switch_expr.ncases; i++) {
                SwitchCase *sc = &e->as.switch_expr.cases[i];
                if (!sc->is_default) {
                    Type *mt = check_expr(c, sc->match);
                    if (vt && mt && !type_equal(vt, mt) &&
                        !(type_is_number(vt) && type_is_number(mt)) &&
                        !(vt->kind == TY_STRING && mt->kind == TY_STRING))
                        fatal_at(c->prog->file, sc->match->line, sc->match->col,
                                 "case type does not match switch type");
                }
                Type *ct = check_expr(c, e->as.switch_expr.values[i]);
                if (!rt) rt = ct;
                else if (type_is_float(rt) || type_is_float(ct)) rt = t_f64();
                else if (type_is_number(rt) && type_is_number(ct)) rt = t_int();
                else if (!type_equal(rt, ct)) rt = ct;
            }
            e->type = rt ? rt : t_void();
            return e->type;
        }
    }
    return t_void();
}

static void check_stmt(Checker *c, Stmt *s) {
    if (!s) return;
    switch (s->kind) {
        case ST_VAR: {
            Type *t = s->as.var.type;
            Type *declared = t;
            Type *base = (t && (t->kind == TY_SHARED || t->kind == TY_WEAK)) ? t->elem : t;
            if (s->as.var.is_auto) {
                if (!s->as.var.init) fatal_at(c->prog->file, s->line, s->col, "auto variable needs initializer");
                t = check_expr(c, s->as.var.init);
                declared = t;
            } else if (s->as.var.init) {
                Type *it = check_expr(c, s->as.var.init);
                if (is_managed_type(c->prog, base) && is_managed_type(c->prog, it) &&
                    s->as.var.init->kind == EX_IDENT)
                    fatal_at(c->prog->file, s->line, s->col,
                             "managed values cannot be copied; move them or return them instead");
                require_assignable(c, base, it, s->as.var.init, "initializer");
            }
            s->as.var.type = declared;
            scope_define(c->scope, s->as.var.name, declared);
            break;
        }
        case ST_ASSIGN: {
            Type *tt = check_expr(c, s->as.assign.target);
            Type *vt = check_expr(c, s->as.assign.value);
            if (is_managed_type(c->prog, tt) && is_managed_type(c->prog, vt) &&
                s->as.assign.value->kind == EX_IDENT)
                fatal_at(c->prog->file, s->line, s->col,
                         "managed values cannot be copied; move them or return them instead");
            require_assignable(c, tt, vt, s->as.assign.value, "assignment");
            break;
        }
        case ST_EXPR: check_expr(c, s->as.expr); break;
        case ST_RETURN: {
            if (s->as.ret) {
                if (s->as.ret->kind == EX_UNARY && s->as.ret->as.unary.op == TK_CARET) {
                    Expr *root = s->as.ret->as.unary.expr;
                    while (root) {
                        if (root->kind == EX_MEMBER) root = root->as.member.obj;
                        else if (root->kind == EX_INDEX) root = root->as.index.obj;
                        else if (root->kind == EX_UNARY && (root->as.unary.op == TK_STAR ||
                                                           root->as.unary.op == TK_CARET)) root = root->as.unary.expr;
                        else if (root->kind == EX_CAST) root = root->as.cast.expr;
                        else break;
                    }
                    if (root && root->kind == EX_IDENT) {
                        const char *nm = root->as.sval;
                        if (strcmp(nm, "self") != 0 && scope_find(c->scope, nm))
                            fatal_at(c->prog->file, s->line, s->col,
                                     "returning a reference to local '%s' is forbidden", nm);
                    }
                }
                Type *t = check_expr(c, s->as.ret);
                require_assignable(c, c->current->ret, t, s->as.ret, "return");
            }
            break;
        }
        case ST_IF:
            check_expr(c, s->as.if_stmt.cond);
            check_stmt(c, s->as.if_stmt.then_stmt);
            check_stmt(c, s->as.if_stmt.else_stmt);
            break;
        case ST_WHILE:
            check_expr(c, s->as.while_stmt.cond);
            c->loop_depth++;
            check_stmt(c, s->as.while_stmt.body);
            c->loop_depth--;
            break;
        case ST_FOR: {
            Scope *old = c->scope;
            c->scope = scope_new(old);
            check_stmt(c, s->as.for_stmt.init);
            if (s->as.for_stmt.cond) check_expr(c, s->as.for_stmt.cond);
            if (s->as.for_stmt.step) check_expr(c, s->as.for_stmt.step);
            c->loop_depth++;
            check_stmt(c, s->as.for_stmt.body);
            c->loop_depth--;
            c->scope = old;
            break;
        }
        case ST_FORIN: {
            Type *it = check_expr(c, s->as.forin.iter);
            Type *vt = t_int();
            if (it && it->kind == TY_ARRAY) vt = it->elem;
            else if (it && it->kind == TY_SLICE) vt = it->elem;
            else if (it && it->kind == TY_VECTOR) vt = it->elem;
            else if (it && it->kind == TY_SET) vt = it->elem;
            else if (it && it->kind == TY_STRING) vt = type_named("char");
            Scope *old = c->scope;
            c->scope = scope_new(old);
            if (s->as.forin.index_name) scope_define(c->scope, s->as.forin.index_name, t_int());
            scope_define(c->scope, s->as.forin.name, vt);
            c->loop_depth++;
            check_stmt(c, s->as.forin.body);
            c->loop_depth--;
            c->scope = old;
            break;
        }
        case ST_BLOCK: {
            Scope *old = c->scope;
            c->scope = scope_new(old);
            for (int i = 0; i < s->as.block.count; i++) check_stmt(c, s->as.block.items[i]);
            c->scope = old;
            break;
        }
        case ST_SWITCH: {
            Type *vt = check_expr(c, s->as.switch_stmt.value);
            for (int i = 0; i < s->as.switch_stmt.ncases; i++) {
                SwitchCase *sc = &s->as.switch_stmt.cases[i];
                Scope *old = c->scope;
                if (sc->enum_type) {
                    EnumDecl *ed = find_enum(c->prog, sc->enum_type);
                    if (!ed) fatal_at(c->prog->file, s->line, s->col, "unknown enum '%s'", sc->enum_type);
                    int vi = -1;
                    for (int j = 0; j < ed->nvariants; j++)
                        if (strcmp(ed->variants[j], sc->enum_variant) == 0) vi = j;
                    if (vi < 0) fatal_at(c->prog->file, s->line, s->col, "unknown variant '%s'", sc->enum_variant);
                    if (!vt || vt->kind != TY_NAMED || strcmp(vt->name, ed->name) != 0)
                        fatal_at(c->prog->file, s->line, s->col, "enum pattern type mismatch");
                    c->scope = scope_new(old);
                    for (int b = 0; b < sc->nbindings; b++)
                        scope_define(c->scope, sc->bindings[b], ed->variant_types[vi][b]);
                    check_stmt(c, sc->body);
                    c->scope = old;
                    continue;
                }
                if (sc->pattern) {
                    int is_opt = strcmp(sc->pattern, "Some") == 0 || strcmp(sc->pattern, "None") == 0;
                    if (is_opt && (!vt || vt->kind != TY_OPTION))
                        fatal_at(c->prog->file, s->line, s->col, "option pattern requires Option value");
                    if (!is_opt && (!vt || vt->kind != TY_RESULT))
                        fatal_at(c->prog->file, s->line, s->col, "result pattern requires Result value");
                    c->scope = scope_new(old);
                    if (sc->binding) {
                        Type *bt = is_opt ? vt->elem : (strcmp(sc->pattern, "Ok") == 0 ? vt->elem : vt->elem2);
                        if (!bt) bt = t_int();
                        scope_define(c->scope, sc->binding, bt);
                    }
                    check_stmt(c, sc->body);
                    c->scope = old;
                    continue;
                }
                c->scope = old;
                if (!sc->is_default) {
                    Type *mt = check_expr(c, sc->match);
                    if (vt && mt && !type_equal(vt, mt) &&
                        !(type_is_number(vt) && type_is_number(mt)) &&
                        !(vt->kind == TY_STRING && mt->kind == TY_STRING))
                        fatal_at(c->prog->file, sc->match->line, sc->match->col,
                                 "case type %s does not match switch type %s",
                                 type_name(mt), type_name(vt));
                }
                check_stmt(c, sc->body);
            }
            break;
        }
        case ST_BREAK:
        case ST_CONTINUE:
            if (c->loop_depth <= 0)
                fatal_at(c->prog->file, s->line, s->col, "break/continue outside loop");
            break;
        case ST_DEFER: check_stmt(c, s->as.defer_stmt); break;
    }
}

static void check_func(Checker *c, Func *f) {
    c->current = f;
    c->scope = scope_new(c->globals);
    c->loop_depth = 0;
    if (f->is_method) {
        Type *self = type_ref(type_named(f->owner->name));
        scope_define(c->scope, "self", self);
    }
    for (int i = 0; i < f->nparams; i++) {
        scope_define(c->scope, f->params[i].name, f->params[i].type);
    }
    if (f->body) check_stmt(c, f->body);
    c->scope = NULL;
    c->current = NULL;
}

int check_program(Program *prog) {
    Checker c;
    c.prog = prog;
    c.scope = scope_new(NULL);
    c.globals = c.scope;
    c.current = NULL;
    c.loop_depth = 0;
    for (int i = 0; i < prog->nglobals; i++) {
        Stmt *g = prog->globals[i];
        if (g->as.var.init) {
            Type *it = check_expr(&c, g->as.var.init);
            if (!g->as.var.type) g->as.var.type = it;
        }
        scope_define(c.scope, g->as.var.name, g->as.var.type);
    }
    for (int i = 0; i < prog->nfuncs; i++) check_func(&c, prog->funcs[i]);
    for (int i = 0; i < prog->nstructs; i++) {
        StructDecl *s = prog->structs[i];
        for (int j = 0; j < s->nfields; j++) {
            if (is_managed_type(prog, s->fields[j].type))
                fatal_at(prog->file, 0, 0,
                         "managed field '%s.%s' is not allowed: managed objects cannot form cycles",
                         s->name, s->fields[j].name);
        }
        for (int j = 0; j < s->nmethods; j++) check_func(&c, s->methods[j]);
    }
    return 0;
}
