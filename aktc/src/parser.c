#include "aktc.h"

#include <ctype.h>

static Token *cur(Parser *p) { return &p->tokens[p->pos]; }
static Token *nth(Parser *p, int n) {
    int i = p->pos + n;
    if (i >= p->count) i = p->count - 1;
    return &p->tokens[i];
}
static Token *advance(Parser *p) {
    Token *t = cur(p);
    if (p->pos + 1 < p->count) p->pos++;
    return t;
}
static int match(Parser *p, TokenKind k) {
    if (cur(p)->kind == k) {
        advance(p);
        return 1;
    }
    return 0;
}
static Token *expect(Parser *p, TokenKind k, const char *what) {
    if (cur(p)->kind != k) {
        fatal_at(p->file, cur(p)->line, cur(p)->col, "expected %s, got %s '%s'", what,
                 token_kind_name(cur(p)->kind), cur(p)->text ? cur(p)->text : "");
    }
    return advance(p);
}
static int is_ident_text(Token *t, const char *s) {
    return t->kind == TK_IDENT && t->text && strcmp(t->text, s) == 0;
}

static void program_add_func(Program *prog, Func *f) {
    prog->funcs = (Func **)xrealloc(prog->funcs, (size_t)(prog->nfuncs + 1) * sizeof(Func *));
    prog->funcs[prog->nfuncs++] = f;
}
static void program_add_struct(Program *prog, StructDecl *s) {
    prog->structs = (StructDecl **)xrealloc(prog->structs, (size_t)(prog->nstructs + 1) * sizeof(StructDecl *));
    prog->structs[prog->nstructs++] = s;
}
static void program_add_enum(Program *prog, EnumDecl *e) {
    prog->enums = (EnumDecl **)xrealloc(prog->enums, (size_t)(prog->nenums + 1) * sizeof(EnumDecl *));
    prog->enums[prog->nenums++] = e;
}
static void program_add_global(Program *prog, Stmt *s) {
    prog->globals = (Stmt **)xrealloc(prog->globals, (size_t)(prog->nglobals + 1) * sizeof(Stmt *));
    prog->globals[prog->nglobals++] = s;
}
static void program_add_include(Program *prog, const char *inc) {
    prog->c_includes = (char **)xrealloc(prog->c_includes, (size_t)(prog->nc_includes + 1) * sizeof(char *));
    prog->c_includes[prog->nc_includes++] = xstrdup(inc);
}
static void program_add_link(Program *prog, const char *lib) {
    prog->c_links = (char **)xrealloc(prog->c_links, (size_t)(prog->nc_links + 1) * sizeof(char *));
    prog->c_links[prog->nc_links++] = xstrdup(lib);
}
void program_add_import(Program *prog, const char *inc) {
    for (int i = 0; i < prog->nc_imports; i++)
        if (strcmp(prog->c_imports[i], inc) == 0) return;
    prog->c_imports = (char **)xrealloc(prog->c_imports, (size_t)(prog->nc_imports + 1) * sizeof(char *));
    prog->c_imports[prog->nc_imports++] = xstrdup(inc);
}
void program_add_source(Program *prog, const char *src) {
    for (int i = 0; i < prog->nc_sources; i++)
        if (strcmp(prog->c_sources[i], src) == 0) return;
    prog->c_sources = (char **)xrealloc(prog->c_sources, (size_t)(prog->nc_sources + 1) * sizeof(char *));
    prog->c_sources[prog->nc_sources++] = xstrdup(src);
}

static StructDecl *find_struct_decl(Program *prog, const char *name) {
    for (int i = 0; i < prog->nstructs; i++)
        if (strcmp(prog->structs[i]->name, name) == 0) return prog->structs[i];
    return NULL;
}
static EnumDecl *find_enum_decl(Program *prog, const char *name) {
    for (int i = 0; i < prog->nenums; i++)
        if (strcmp(prog->enums[i]->name, name) == 0) return prog->enums[i];
    return NULL;
}

static Type *parse_type(Parser *p);

static Type *parse_type_primary(Parser *p) {
    if (match(p, TK_LBRACKET)) {
        if (match(p, TK_RBRACKET)) {
            Type *elem = parse_type(p);
            return type_slice(elem);
        }
        Type *elem = parse_type(p);
        expect(p, TK_SEMI, "';' in array type");
        Token *n = expect(p, TK_INT, "array length");
        expect(p, TK_RBRACKET, "']'");
        return type_array(elem, n->ival);
    }
    if (match(p, TK_STAR)) {
        int is_const = 0;
        if (cur(p)->kind == TK_CONST) { is_const = 1; advance(p); }
        else if (cur(p)->kind == TK_VOLATILE) advance(p);
        Type *pt = type_ptr(parse_type(p));
        pt->is_const = is_const;
        return pt;
    }
    if (match(p, TK_CARET)) {
        if (cur(p)->kind == TK_CONST || cur(p)->kind == TK_VOLATILE) advance(p);
        return type_ref(parse_type(p));
    }
    if (cur(p)->kind == TK_FN) {
        advance(p);
        expect(p, TK_LPAREN, "'(' after fn");
        Type *ps[16];
        int np = 0;
        while (cur(p)->kind != TK_RPAREN && cur(p)->kind != TK_EOF) {
            if (match(p, TK_COMMA)) continue;
            Type *pt = parse_type(p);
            if (np < 16) ps[np++] = pt;
        }
        expect(p, TK_RPAREN, "')'");
        Type *ret = parse_type(p);
        return type_cfunc(ret, ps, np);
    }
    if (cur(p)->kind == TK_IDENT) {
        Token *name = advance(p);
        char *full = xstrdup(name->text);
        while (match(p, TK_DOT)) {
            Token *part = expect(p, TK_IDENT, "type name component");
            char *n = xasprintf("%s.%s", full, part->text);
            free(full);
            full = n;
        }
        if (match(p, TK_LT)) {
            Type *a = parse_type(p);
            Type *b = NULL;
            if (match(p, TK_COMMA)) b = parse_type(p);
            expect(p, TK_GT, "'>'");
            if (strcmp(full, "Vector") == 0) return type_vector(a);
            if (strcmp(full, "Deque") == 0) return type_vector(a);
            if (strcmp(full, "Set") == 0) return type_set(a);
            if (strcmp(full, "Map") == 0) return type_map(a, b ? b : type_named("int"));
            if (strcmp(full, "Option") == 0) {
                Type *t = type_new(TY_OPTION, "Option");
                t->elem = a;
                return t;
            }
            if (strcmp(full, "Result") == 0) {
                Type *t = type_new(TY_RESULT, "Result");
                t->elem = a;
                t->elem2 = b;
                return t;
            }
            Type *t = type_named(full);
            t->elem = a;
            t->elem2 = b;
            return t;
        }
        return type_named(full);
    }
    fatal_at(p->file, cur(p)->line, cur(p)->col, "expected type, got %s '%s'",
             token_kind_name(cur(p)->kind), cur(p)->text ? cur(p)->text : "");
    return NULL;
}

static Type *parse_type(Parser *p) { return parse_type_primary(p); }

static int looks_like_generic_type(Parser *p) {
    if (cur(p)->kind != TK_IDENT || nth(p, 1)->kind != TK_LT) return 0;
    int depth = 0;
    for (int i = 1; p->pos + i < p->count; i++) {
        TokenKind k = nth(p, i)->kind;
        if (k == TK_LT) depth++;
        else if (k == TK_GT) {
            depth--;
            if (depth == 0) return nth(p, i + 1)->kind == TK_IDENT;
        } else if (k == TK_EOF || k == TK_SEMI || k == TK_LBRACE) {
            return 0;
        }
    }
    return 0;
}

static int looks_like_type(Parser *p) {
    Token *t = cur(p);
    if (t->kind == TK_AUTO || t->kind == TK_LBRACKET || t->kind == TK_STAR || t->kind == TK_CARET)
        return 1;
    if (t->kind == TK_IDENT) {
        if (is_ident_text(t, "const") || is_ident_text(t, "volatile")) return 1;
        if (nth(p, 1)->kind == TK_IDENT) return 1;
        if (looks_like_generic_type(p)) return 1;
    }
    return 0;
}

static Expr *expr_new(ExprKind kind, int line, int col) {
    Expr *e = (Expr *)xcalloc(1, sizeof(Expr));
    e->kind = kind;
    e->line = line;
    e->col = col;
    return e;
}
static Stmt *stmt_new(StmtKind kind, int line, int col) {
    Stmt *s = (Stmt *)xcalloc(1, sizeof(Stmt));
    s->kind = kind;
    s->line = line;
    s->col = col;
    return s;
}

static char *decode_text(const char *s) {
    size_t cap = strlen(s) + 1, len = 0;
    char *out = (char *)xmalloc(cap);
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == '\\' && s[i + 1]) {
            i++;
            switch (s[i]) {
                case 'n': out[len++] = '\n'; break;
                case 't': out[len++] = '\t'; break;
                case 'r': out[len++] = '\r'; break;
                case '0': out[len++] = '\0'; break;
                case '\\': out[len++] = '\\'; break;
                case '"': out[len++] = '"'; break;
                case '\'': out[len++] = '\''; break;
                default: out[len++] = s[i]; break;
            }
        } else {
            out[len++] = s[i];
        }
    }
    out[len] = 0;
    return out;
}

static Expr *parse_expression(Parser *p);
static Stmt *parse_block(Parser *p);

static Expr *parse_fstring(Parser *p, Token *tok) {
    const char *raw = tok->text;
    char **parts = NULL;
    Expr **exprs = NULL;
    int *precs = NULL;
    int nparts = 0, nexprs = 0;
    size_t cap = 64, len = 0;
    char *text = (char *)xmalloc(cap);
    for (size_t i = 0; raw[i];) {
        if (raw[i] == '{' && raw[i + 1] == '{') {
            if (len + 1 >= cap) { cap *= 2; text = (char *)xrealloc(text, cap); }
            text[len++] = '{';
            i += 2;
            continue;
        }
        if (raw[i] == '}' && raw[i + 1] == '}') {
            if (len + 1 >= cap) { cap *= 2; text = (char *)xrealloc(text, cap); }
            text[len++] = '}';
            i += 2;
            continue;
        }
        if (raw[i] == '{') {
            if (len + 1 >= cap) { cap *= 2; text = (char *)xrealloc(text, cap); }
            text[len] = 0;
            parts = (char **)xrealloc(parts, (size_t)(nparts + 1) * sizeof(char *));
            parts[nparts++] = decode_text(text);
            len = 0;
            i++;
            size_t start = i;
            int depth = 1;
            while (raw[i] && depth > 0) {
                if (raw[i] == '"' || raw[i] == '\'') {
                    char q = raw[i++];
                    while (raw[i] && raw[i] != q) {
                        if (raw[i] == '\\' && raw[i + 1]) i++;
                        i++;
                    }
                    if (raw[i] == q) i++;
                    continue;
                }
                if (raw[i] == '{') depth++;
                else if (raw[i] == '}') {
                    depth--;
                    if (depth == 0) break;
                }
                i++;
            }
            if (depth != 0) fatal_at(p->file, tok->line, tok->col, "unterminated expression in f-string");
            char *src = xstrndup(raw + start, i - start);
            int prec = -1;
            {
                int depth2 = 0;
                for (int k = (int)strlen(src) - 1; k >= 0; k--) {
                    char ch = src[k];
                    if (ch == ')' || ch == ']' || ch == '}') depth2++;
                    else if (ch == '(' || ch == '[' || ch == '{') depth2--;
                    else if (ch == ':' && depth2 == 0) {
                        const char *sp = src + k + 1;
                        if (*sp == '.') {
                            int v = 0, ok = 0;
                            sp++;
                            while (*sp >= '0' && *sp <= '9') { v = v * 10 + (*sp - '0'); sp++; ok = 1; }
                            if (ok && *sp == 0) { prec = v; src[k] = 0; }
                        }
                        break;
                    }
                }
            }
            int nt = 0;
            Token *toks = lex_all(src, p->file, &nt);
            Parser sub;
            sub.tokens = toks;
            sub.count = nt;
            sub.pos = 0;
            sub.program = p->program;
            sub.file = p->file;
            sub.had_error = 0;
            Expr *e = parse_expression(&sub);
            if (cur(&sub)->kind != TK_EOF)
                fatal_at(p->file, tok->line, tok->col, "trailing tokens in f-string expression");
            exprs = (Expr **)xrealloc(exprs, (size_t)(nexprs + 1) * sizeof(Expr *));
            precs = (int *)xrealloc(precs, (size_t)(nexprs + 1) * sizeof(int));
            precs[nexprs] = prec;
            exprs[nexprs++] = e;
            i++;
            continue;
        }
        if (len + 1 >= cap) { cap *= 2; text = (char *)xrealloc(text, cap); }
        text[len++] = raw[i++];
    }
    text[len] = 0;
    parts = (char **)xrealloc(parts, (size_t)(nparts + 1) * sizeof(char *));
    parts[nparts++] = decode_text(text);
    Expr *e = expr_new(EX_FSTRING, tok->line, tok->col);
    e->as.fstring.parts = parts;
    e->as.fstring.exprs = exprs;
    e->as.fstring.prec = precs;
    e->as.fstring.nparts = nparts;
    return e;
}

static Expr *parse_primary(Parser *p) {
    Token *t = cur(p);
    if (t->kind == TK_INT) {
        advance(p);
        Expr *e = expr_new(EX_INT, t->line, t->col);
        e->as.ival = t->ival;
        return e;
    }
    if (t->kind == TK_FLOAT) {
        advance(p);
        Expr *e = expr_new(EX_FLOAT, t->line, t->col);
        e->as.fval = t->fval;
        return e;
    }
    if (t->kind == TK_STRING) {
        advance(p);
        Expr *e = expr_new(EX_STRING, t->line, t->col);
        e->as.sval = xstrdup(t->text);
        return e;
    }
    if (t->kind == TK_FSTRING) {
        advance(p);
        return parse_fstring(p, t);
    }
    if (t->kind == TK_CHAR) {
        advance(p);
        Expr *e = expr_new(EX_CHAR, t->line, t->col);
        e->as.ival = t->ival;
        return e;
    }
    if (t->kind == TK_TRUE || t->kind == TK_FALSE) {
        advance(p);
        Expr *e = expr_new(EX_BOOL, t->line, t->col);
        e->as.ival = t->kind == TK_TRUE;
        return e;
    }
    if (t->kind == TK_NULL) {
        advance(p);
        return expr_new(EX_NULL, t->line, t->col);
    }
    if (t->kind == TK_SELF) {
        advance(p);
        Expr *e = expr_new(EX_IDENT, t->line, t->col);
        e->as.sval = xstrdup("self");
        return e;
    }
    if (t->kind == TK_IDENT) {
        advance(p);
        if (cur(p)->kind == TK_LBRACE) {
            Expr *e = expr_new(EX_STRUCT, t->line, t->col);
            e->as.struct_init.type_name = xstrdup(t->text);
            advance(p);
            char **names = NULL;
            Expr **vals = NULL;
            int n = 0;
            while (cur(p)->kind != TK_RBRACE && cur(p)->kind != TK_EOF) {
                if (match(p, TK_COMMA)) continue;
                Token *fn = expect(p, TK_IDENT, "field name");
                expect(p, TK_COLON, "':'");
                Expr *v = parse_expression(p);
                names = (char **)xrealloc(names, (size_t)(n + 1) * sizeof(char *));
                vals = (Expr **)xrealloc(vals, (size_t)(n + 1) * sizeof(Expr *));
                names[n] = xstrdup(fn->text);
                vals[n] = v;
                n++;
                if (match(p, TK_COMMA)) continue;
                if (cur(p)->kind == TK_RBRACE) break;
                if (cur(p)->kind == TK_IDENT && nth(p, 1)->kind == TK_COLON) continue;
                break;
            }
            expect(p, TK_RBRACE, "'}'");
            e->as.struct_init.field_names = names;
            e->as.struct_init.field_vals = vals;
            e->as.struct_init.nfields = n;
            return e;
        }
        if (match(p, TK_COLONCOLON)) {
            Token *name = expect(p, TK_IDENT, "enum variant or associated function");
            Expr *e = expr_new(EX_STATIC, t->line, t->col);
            e->as.path.type_name = xstrdup(t->text);
            e->as.path.name = xstrdup(name->text);
            e->as.path.is_variant = 0;
            return e;
        }
        Expr *e = expr_new(EX_IDENT, t->line, t->col);
        e->as.sval = xstrdup(t->text);
        return e;
    }
    if (match(p, TK_LPAREN)) {
        if (match(p, TK_RPAREN)) return expr_new(EX_UNIT, t->line, t->col);
        Expr *e = parse_expression(p);
        expect(p, TK_RPAREN, "')'");
        return e;
    }
    if (match(p, TK_LBRACKET)) {
        Expr *e = expr_new(EX_ARRAY, t->line, t->col);
        Expr **items = NULL;
        int n = 0;
        while (cur(p)->kind != TK_RBRACKET && cur(p)->kind != TK_EOF) {
            if (match(p, TK_COMMA)) continue;
            Expr *item = parse_expression(p);
            items = (Expr **)xrealloc(items, (size_t)(n + 1) * sizeof(Expr *));
            items[n++] = item;
            if (!match(p, TK_COMMA) && cur(p)->kind != TK_RBRACKET) break;
        }
        expect(p, TK_RBRACKET, "']'");
        e->as.array.items = items;
        e->as.array.count = n;
        return e;
    }
    if (match(p, TK_LBRACE)) {
        Expr *e = expr_new(EX_MAP, t->line, t->col);
        Expr **keys = NULL, **vals = NULL;
        int n = 0;
        while (cur(p)->kind != TK_RBRACE && cur(p)->kind != TK_EOF) {
            if (match(p, TK_COMMA)) continue;
            Expr *k = parse_expression(p);
            expect(p, TK_COLON, "':' in map literal");
            Expr *v = parse_expression(p);
            keys = (Expr **)xrealloc(keys, (size_t)(n + 1) * sizeof(Expr *));
            vals = (Expr **)xrealloc(vals, (size_t)(n + 1) * sizeof(Expr *));
            keys[n] = k;
            vals[n] = v;
            n++;
            if (!match(p, TK_COMMA) && cur(p)->kind != TK_RBRACE && cur(p)->kind != TK_IDENT) break;
        }
        expect(p, TK_RBRACE, "'}'");
        e->as.map.keys = keys;
        e->as.map.vals = vals;
        e->as.map.count = n;
        return e;
    }
    if (t->kind == TK_SWITCH) {
        advance(p);
        expect(p, TK_LPAREN, "'('");
        Expr *value = parse_expression(p);
        expect(p, TK_RPAREN, "')'");
        expect(p, TK_LBRACE, "'{'");
        SwitchCase *cases = NULL;
        Expr **values = NULL;
        int n = 0;
        while (cur(p)->kind != TK_RBRACE && cur(p)->kind != TK_EOF) {
            if (match(p, TK_SEMI) || match(p, TK_COMMA)) continue;
            SwitchCase c;
            memset(&c, 0, sizeof(c));
            if (match(p, TK_DEFAULT)) {
                c.is_default = 1;
                expect(p, TK_COLON, "':'");
            } else if (match(p, TK_CASE)) {
                c.match = parse_expression(p);
                expect(p, TK_COLON, "':'");
            } else {
                fatal_at(p->file, cur(p)->line, cur(p)->col, "expected case or default");
            }
            Expr *v = parse_expression(p);
            cases = (SwitchCase *)xrealloc(cases, (size_t)(n + 1) * sizeof(SwitchCase));
            values = (Expr **)xrealloc(values, (size_t)(n + 1) * sizeof(Expr *));
            cases[n] = c;
            values[n] = v;
            n++;
        }
        expect(p, TK_RBRACE, "'}'");
        Expr *e = expr_new(EX_SWITCH, t->line, t->col);
        e->as.switch_expr.value = value;
        e->as.switch_expr.cases = cases;
        e->as.switch_expr.values = values;
        e->as.switch_expr.ncases = n;
        return e;
    }
    if (t->kind == TK_PIPE || t->kind == TK_OROR) {
        Expr *e = expr_new(EX_CLOSURE, t->line, t->col);
        char **params = NULL;
        int np = 0;
        if (t->kind == TK_PIPE) {
            advance(p);
            while (cur(p)->kind != TK_PIPE && cur(p)->kind != TK_EOF) {
                if (match(p, TK_COMMA)) continue;
                Token *pn = expect(p, TK_IDENT, "closure parameter");
                params = (char **)xrealloc(params, (size_t)(np + 1) * sizeof(char *));
                params[np++] = xstrdup(pn->text);
                if (!match(p, TK_COMMA) && cur(p)->kind != TK_PIPE) break;
            }
            expect(p, TK_PIPE, "'|'");
        } else {
            advance(p); /* || */
        }
        e->as.closure.params = params;
        e->as.closure.nparams = np;
        if (cur(p)->kind == TK_LBRACE) {
            e->as.closure.block = parse_block(p);
            e->as.closure.body = NULL;
        } else {
            e->as.closure.body = parse_expression(p);
        }
        return e;
    }
    if (t->kind == TK_ASYNC && nth(p, 1)->kind == TK_DOT && nth(p, 2)->kind == TK_IDENT) {
        advance(p);
        advance(p);
        Token *name = advance(p);
        Expr *base = expr_new(EX_IDENT, t->line, t->col);
        base->as.sval = xstrdup("async");
        Expr *m = expr_new(EX_MEMBER, t->line, t->col);
        m->as.member.obj = base;
        m->as.member.name = xstrdup(name->text);
        return m;
    }
    fatal_at(p->file, t->line, t->col, "expected expression, got %s '%s'",
             token_kind_name(t->kind), t->text ? t->text : "");
    return NULL;
}

static int question_is_ternary(Parser *p) {
    int depth = 0;
    for (int i = p->pos + 1; i < p->count; i++) {
        TokenKind k = p->tokens[i].kind;
        if (k == TK_LPAREN || k == TK_LBRACKET || k == TK_LBRACE) depth++;
        else if (k == TK_RPAREN || k == TK_RBRACKET || k == TK_RBRACE) {
            if (depth == 0) return 0;
            depth--;
        } else if (k == TK_COLON && depth == 0) {
            return 1;
        } else if ((k == TK_SEMI || k == TK_COMMA || k == TK_EOF) && depth == 0) {
            return 0;
        }
    }
    return 0;
}

static int same_line(Parser *p) {
    return p->pos > 0 && cur(p)->line == p->tokens[p->pos - 1].line;
}

static Expr *parse_postfix(Parser *p) {
    Expr *e = parse_primary(p);
    for (;;) {
        if (cur(p)->kind == TK_LPAREN && same_line(p)) {
            advance(p);
            Expr *call = expr_new(EX_CALL, e->line, e->col);
            call->as.call.callee = e;
            Expr **args = NULL;
            int n = 0;
            while (cur(p)->kind != TK_RPAREN && cur(p)->kind != TK_EOF) {
                if (match(p, TK_COMMA)) continue;
                Expr *a = parse_expression(p);
                args = (Expr **)xrealloc(args, (size_t)(n + 1) * sizeof(Expr *));
                args[n++] = a;
                if (!match(p, TK_COMMA) && cur(p)->kind != TK_RPAREN) break;
            }
            expect(p, TK_RPAREN, "')'");
            call->as.call.args = args;
            call->as.call.argc = n;
            e = call;
            continue;
        }
        if (cur(p)->kind == TK_LBRACKET && same_line(p)) {
            advance(p);
            Expr *idx = expr_new(EX_INDEX, e->line, e->col);
            idx->as.index.obj = e;
            idx->as.index.index = parse_expression(p);
            expect(p, TK_RBRACKET, "']'");
            e = idx;
            continue;
        }
        if (cur(p)->kind == TK_DOT && same_line(p)) {
            advance(p);
            Token *name = expect(p, TK_IDENT, "member name");
            Expr *m = expr_new(EX_MEMBER, e->line, e->col);
            m->as.member.obj = e;
            m->as.member.name = xstrdup(name->text);
            e = m;
            continue;
        }
        if (cur(p)->kind == TK_QUESTION && !question_is_ternary(p) && same_line(p)) {
            advance(p);
            Expr *u = expr_new(EX_UNARY, e->line, e->col);
            u->as.unary.op = TK_QUESTION;
            u->as.unary.expr = e;
            e = u;
            continue;
        }
        if (cur(p)->kind == TK_AS && same_line(p)) {
            advance(p);
            Expr *c = expr_new(EX_CAST, e->line, e->col);
            c->as.cast.target = parse_type(p);
            c->as.cast.expr = e;
            e = c;
            continue;
        }
        break;
    }
    return e;
}

static Expr *parse_unary(Parser *p) {
    Token *t = cur(p);
    if (t->kind == TK_MINUS || t->kind == TK_BANG || t->kind == TK_NOT ||
        t->kind == TK_TILDE || t->kind == TK_STAR || t->kind == TK_AMP ||
        t->kind == TK_CARET || t->kind == TK_AWAIT) {
        advance(p);
        Expr *e = expr_new(EX_UNARY, t->line, t->col);
        e->as.unary.op = t->kind;
        e->as.unary.expr = parse_unary(p);
        return e;
    }
    return parse_postfix(p);
}

static Expr *binary(Expr *l, int op, Expr *r) {
    Expr *e = expr_new(EX_BINARY, l->line, l->col);
    e->as.binary.left = l;
    e->as.binary.op = op;
    e->as.binary.right = r;
    return e;
}

static Expr *parse_pow(Parser *p) {
    Expr *e = parse_unary(p);
    if (match(p, TK_POW)) {
        Expr *r = parse_pow(p);
        return binary(e, TK_POW, r);
    }
    return e;
}
static Expr *parse_mul(Parser *p) {
    Expr *e = parse_pow(p);
    while (cur(p)->kind == TK_STAR || cur(p)->kind == TK_SLASH || cur(p)->kind == TK_PERCENT) {
        int op = advance(p)->kind;
        e = binary(e, op, parse_pow(p));
    }
    return e;
}
static Expr *parse_add(Parser *p) {
    Expr *e = parse_mul(p);
    while (cur(p)->kind == TK_PLUS || cur(p)->kind == TK_MINUS) {
        int op = advance(p)->kind;
        e = binary(e, op, parse_mul(p));
    }
    return e;
}
static Expr *parse_shift(Parser *p) {
    Expr *e = parse_add(p);
    while (cur(p)->kind == TK_SHL || cur(p)->kind == TK_SHR) {
        int op = advance(p)->kind;
        e = binary(e, op, parse_add(p));
    }
    return e;
}
static Expr *parse_cmp(Parser *p) {
    Expr *e = parse_shift(p);
    while (cur(p)->kind == TK_LT || cur(p)->kind == TK_GT ||
           cur(p)->kind == TK_LE || cur(p)->kind == TK_GE) {
        int op = advance(p)->kind;
        e = binary(e, op, parse_shift(p));
    }
    return e;
}
static Expr *parse_eq(Parser *p) {
    Expr *e = parse_cmp(p);
    while (cur(p)->kind == TK_EQEQ || cur(p)->kind == TK_NEQ) {
        int op = advance(p)->kind;
        e = binary(e, op, parse_cmp(p));
    }
    return e;
}
static Expr *parse_bitand(Parser *p) {
    Expr *e = parse_eq(p);
    while (cur(p)->kind == TK_AMP) {
        advance(p);
        e = binary(e, TK_AMP, parse_eq(p));
    }
    return e;
}
static Expr *parse_bitxor(Parser *p) {
    Expr *e = parse_bitand(p);
    while (cur(p)->kind == TK_CARET) {
        advance(p);
        e = binary(e, TK_CARET, parse_bitand(p));
    }
    return e;
}
static Expr *parse_bitor(Parser *p) {
    Expr *e = parse_bitxor(p);
    while (cur(p)->kind == TK_PIPE) {
        advance(p);
        e = binary(e, TK_PIPE, parse_bitxor(p));
    }
    return e;
}
static Expr *parse_and(Parser *p) {
    Expr *e = parse_bitor(p);
    while (cur(p)->kind == TK_ANDAND || cur(p)->kind == TK_AND) {
        advance(p);
        e = binary(e, TK_ANDAND, parse_bitor(p));
    }
    return e;
}
static Expr *parse_or(Parser *p) {
    Expr *e = parse_and(p);
    while (cur(p)->kind == TK_OROR || cur(p)->kind == TK_OR) {
        advance(p);
        e = binary(e, TK_OROR, parse_and(p));
    }
    return e;
}
static Expr *parse_coalesce(Parser *p) {
    Expr *e = parse_or(p);
    if (match(p, TK_QQ)) {
        Expr *c = expr_new(EX_COALESCE, e->line, e->col);
        c->as.ternary.cond = e;
        c->as.ternary.then_expr = e;
        c->as.ternary.else_expr = parse_coalesce(p);
        return c;
    }
    return e;
}
static Expr *parse_ternary(Parser *p) {
    Expr *e = parse_coalesce(p);
    if (match(p, TK_QUESTION)) {
        Expr *t = expr_new(EX_TERNARY, e->line, e->col);
        t->as.ternary.cond = e;
        t->as.ternary.then_expr = parse_expression(p);
        expect(p, TK_COLON, "':'");
        t->as.ternary.else_expr = parse_ternary(p);
        return t;
    }
    return e;
}
static Expr *parse_range(Parser *p) {
    Expr *e = parse_ternary(p);
    if (cur(p)->kind == TK_RANGE || cur(p)->kind == TK_RANGE_INCL) {
        int inc = advance(p)->kind == TK_RANGE_INCL;
        Expr *r = expr_new(EX_RANGE, e->line, e->col);
        r->as.range.start = e;
        r->as.range.end = parse_ternary(p);
        r->as.range.inclusive = inc;
        if (match(p, TK_STEP)) r->as.range.step = parse_ternary(p);
        return r;
    }
    return e;
}

static Expr *parse_expression(Parser *p) { return parse_range(p); }

static Stmt *parse_block(Parser *p) {
    Token *lb = expect(p, TK_LBRACE, "'{'");
    Stmt *b = stmt_new(ST_BLOCK, lb->line, lb->col);
    Stmt **items = NULL;
    int n = 0;
    while (cur(p)->kind != TK_RBRACE && cur(p)->kind != TK_EOF) {
        if (match(p, TK_SEMI)) continue;
        extern Stmt *parse_statement(Parser *p);
        Stmt *s = parse_statement(p);
        if (!s) continue;
        items = (Stmt **)xrealloc(items, (size_t)(n + 1) * sizeof(Stmt *));
        items[n++] = s;
    }
    expect(p, TK_RBRACE, "'}'");
    b->as.block.items = items;
    b->as.block.count = n;
    return b;
}

Stmt *parse_statement(Parser *p) {
    Token *t = cur(p);
    if (getenv("AKT_DEBUG_PARSE"))
        fprintf(stderr, "parse stmt %s:%d:%d kind=%d text=%s looks_type=%d\n",
                p->file, t->line, t->col, t->kind, t->text ? t->text : "",
                looks_like_type(p));
    if (t->kind == TK_LBRACE) return parse_block(p);
    if (t->kind == TK_IF) {
        advance(p);
        expect(p, TK_LPAREN, "'('");
        Expr *cond = parse_expression(p);
        expect(p, TK_RPAREN, "')'");
        Stmt *s = stmt_new(ST_IF, t->line, t->col);
        s->as.if_stmt.cond = cond;
        s->as.if_stmt.then_stmt = parse_statement(p);
        if (match(p, TK_ELSE)) s->as.if_stmt.else_stmt = parse_statement(p);
        return s;
    }
    if (t->kind == TK_WHILE) {
        advance(p);
        expect(p, TK_LPAREN, "'('");
        Expr *cond = parse_expression(p);
        expect(p, TK_RPAREN, "')'");
        Stmt *s = stmt_new(ST_WHILE, t->line, t->col);
        s->as.while_stmt.cond = cond;
        s->as.while_stmt.body = parse_statement(p);
        return s;
    }
    if (t->kind == TK_LOOP) {
        advance(p);
        Stmt *s = stmt_new(ST_WHILE, t->line, t->col);
        Expr *one = expr_new(EX_BOOL, t->line, t->col);
        one->as.ival = 1;
        s->as.while_stmt.cond = one;
        s->as.while_stmt.body = parse_statement(p);
        return s;
    }
    if (t->kind == TK_FOR) {
        advance(p);
        expect(p, TK_LPAREN, "'('");
        if (cur(p)->kind == TK_IDENT && nth(p, 1)->kind == TK_IN) {
            Token *name = advance(p);
            advance(p);
            Expr *iter = parse_expression(p);
            expect(p, TK_RPAREN, "')'");
            Stmt *s = stmt_new(ST_FORIN, t->line, t->col);
            s->as.forin.name = xstrdup(name->text);
            s->as.forin.iter = iter;
            s->as.forin.body = parse_statement(p);
            return s;
        }
        if (cur(p)->kind == TK_IDENT && nth(p, 1)->kind == TK_COMMA && nth(p, 2)->kind == TK_IDENT &&
            nth(p, 3)->kind == TK_IN) {
            Token *idx = advance(p);
            advance(p);
            Token *name = advance(p);
            advance(p);
            Expr *iter = parse_expression(p);
            expect(p, TK_RPAREN, "')'");
            Stmt *s = stmt_new(ST_FORIN, t->line, t->col);
            s->as.forin.name = xstrdup(name->text);
            s->as.forin.index_name = xstrdup(idx->text);
            s->as.forin.iter = iter;
            s->as.forin.body = parse_statement(p);
            return s;
        }
        Stmt *init = NULL;
        if (cur(p)->kind != TK_SEMI) {
            if (looks_like_type(p)) {
                init = parse_statement(p);
            } else {
                Expr *e = parse_expression(p);
                init = stmt_new(ST_EXPR, e->line, e->col);
                init->as.expr = e;
            }
        }
        expect(p, TK_SEMI, "';'");
        Expr *cond = NULL;
        if (cur(p)->kind != TK_SEMI) cond = parse_expression(p);
        expect(p, TK_SEMI, "';'");
        Expr *step = NULL;
        if (cur(p)->kind != TK_RPAREN) step = parse_expression(p);
        expect(p, TK_RPAREN, "')'");
        Stmt *s = stmt_new(ST_FOR, t->line, t->col);
        s->as.for_stmt.init = init;
        s->as.for_stmt.cond = cond;
        s->as.for_stmt.step = step;
        s->as.for_stmt.body = parse_statement(p);
        return s;
    }
    if (t->kind == TK_BREAK) {
        advance(p);
        return stmt_new(ST_BREAK, t->line, t->col);
    }
    if (t->kind == TK_CONTINUE) {
        advance(p);
        return stmt_new(ST_CONTINUE, t->line, t->col);
    }
    if (t->kind == TK_RETURN) {
        advance(p);
        Stmt *s = stmt_new(ST_RETURN, t->line, t->col);
        if (cur(p)->kind != TK_RBRACE && cur(p)->kind != TK_SEMI && cur(p)->kind != TK_EOF) {
            s->as.ret = parse_expression(p);
        }
        match(p, TK_SEMI);
        return s;
    }
    if (t->kind == TK_DEFER) {
        advance(p);
        Stmt *s = stmt_new(ST_DEFER, t->line, t->col);
        s->as.defer_stmt = parse_statement(p);
        return s;
    }
    if (t->kind == TK_SWITCH) {
        advance(p);
        expect(p, TK_LPAREN, "'('");
        Expr *value = parse_expression(p);
        expect(p, TK_RPAREN, "')'");
        expect(p, TK_LBRACE, "'{'");
        SwitchCase *cases = NULL;
        int n = 0;
        while (cur(p)->kind != TK_RBRACE && cur(p)->kind != TK_EOF) {
            if (match(p, TK_SEMI) || match(p, TK_COMMA)) continue;
            SwitchCase c;
            c.match = NULL;
            c.body = NULL;
            c.is_default = 0;
            c.pattern = NULL;
            c.binding = NULL;
            c.enum_type = NULL;
            c.enum_variant = NULL;
            c.bindings = NULL;
            c.nbindings = 0;
            if (match(p, TK_DEFAULT)) {
                c.is_default = 1;
                expect(p, TK_COLON, "':'");
            } else if (match(p, TK_CASE)) {
                if (cur(p)->kind == TK_IDENT &&
                    (strcmp(cur(p)->text, "Some") == 0 || strcmp(cur(p)->text, "None") == 0 ||
                     strcmp(cur(p)->text, "Ok") == 0 || strcmp(cur(p)->text, "Err") == 0) &&
                    (nth(p, 1)->kind == TK_COLON || nth(p, 1)->kind == TK_LPAREN)) {
                    c.pattern = xstrdup(advance(p)->text);
                    if (match(p, TK_LPAREN)) {
                        Token *bn = expect(p, TK_IDENT, "binding name");
                        c.binding = xstrdup(bn->text);
                        expect(p, TK_RPAREN, "')'");
                    }
                } else {
                    c.match = parse_expression(p);
                    if (c.match && c.match->kind == EX_CALL &&
                        c.match->as.call.callee->kind == EX_STATIC) {
                        Expr *st = c.match->as.call.callee;
                        EnumDecl *ed = find_enum_decl(p->program, st->as.path.type_name);
                        if (ed) {
                            for (int vi = 0; vi < ed->nvariants; vi++) {
                                if (strcmp(ed->variants[vi], st->as.path.name) == 0 &&
                                    ed->variant_nfields[vi] > 0) {
                                    c.enum_type = xstrdup(ed->name);
                                    c.enum_variant = xstrdup(ed->variants[vi]);
                                    c.nbindings = c.match->as.call.argc;
                                    c.bindings = (char **)xcalloc((size_t)c.nbindings, sizeof(char *));
                                    for (int bi = 0; bi < c.nbindings; bi++) {
                                        Expr *arg = c.match->as.call.args[bi];
                                        if (arg->kind != EX_IDENT)
                                            fatal_at(p->file, arg->line, arg->col,
                                                     "enum pattern binding must be an identifier");
                                        c.bindings[bi] = xstrdup(arg->as.sval);
                                    }
                                    c.match = NULL;
                                    break;
                                }
                            }
                        }
                    }
                }
                expect(p, TK_COLON, "':'");
            } else {
                fatal_at(p->file, cur(p)->line, cur(p)->col, "expected case or default");
            }
            if (cur(p)->kind != TK_CASE && cur(p)->kind != TK_DEFAULT &&
                cur(p)->kind != TK_RBRACE)
                c.body = parse_statement(p);
            cases = (SwitchCase *)xrealloc(cases, (size_t)(n + 1) * sizeof(SwitchCase));
            cases[n++] = c;
        }
        expect(p, TK_RBRACE, "'}'");
        Stmt *s = stmt_new(ST_SWITCH, t->line, t->col);
        s->as.switch_stmt.value = value;
        s->as.switch_stmt.cases = cases;
        s->as.switch_stmt.ncases = n;
        return s;
    }
    if (t->kind == TK_ARENA) {
        advance(p);
        if (cur(p)->kind == TK_IDENT) {
            Token *name = advance(p);
            Stmt *s = stmt_new(ST_VAR, t->line, t->col);
            s->as.var.type = type_named("arena");
            s->as.var.name = xstrdup(name->text);
            s->as.var.is_auto = 0;
            return s;
        }
    }
    if (t->kind == TK_SHARED || t->kind == TK_MANUAL || t->kind == TK_WEAK) {
        TokenKind mod = t->kind;
        advance(p);
        Stmt *s = stmt_new(ST_VAR, t->line, t->col);
        if (match(p, TK_AUTO)) {
            s->as.var.is_auto = 1;
        } else {
            Type *vt = parse_type(p);
            if (mod == TK_SHARED) vt = type_shared(vt);
            else if (mod == TK_WEAK) vt = type_weak(vt);
            s->as.var.type = vt;
        }
        Token *name = expect(p, TK_IDENT, "variable name");
        s->as.var.name = xstrdup(name->text);
        if (match(p, TK_ASSIGN)) s->as.var.init = parse_expression(p);
        match(p, TK_SEMI);
        return s;
    }

    if (looks_like_type(p)) {
        Stmt *s = stmt_new(ST_VAR, t->line, t->col);
        if (match(p, TK_AUTO)) {
            s->as.var.is_auto = 1;
        } else {
            s->as.var.type = parse_type(p);
        }
        Token *name = expect(p, TK_IDENT, "variable name");
        s->as.var.name = xstrdup(name->text);
        if (match(p, TK_ASSIGN)) s->as.var.init = parse_expression(p);
        match(p, TK_SEMI);
        return s;
    }

    Expr *e = parse_expression(p);
    if (cur(p)->kind == TK_ASSIGN || cur(p)->kind == TK_PLUSEQ ||
        cur(p)->kind == TK_MINUSEQ || cur(p)->kind == TK_STAREQ ||
        cur(p)->kind == TK_SLASHEQ || cur(p)->kind == TK_PERCENTEQ) {
        int op = advance(p)->kind;
        Expr *v = parse_expression(p);
        Stmt *s = stmt_new(ST_ASSIGN, t->line, t->col);
        s->as.assign.target = e;
        s->as.assign.op = op;
        s->as.assign.value = v;
        match(p, TK_SEMI);
        return s;
    }
    Stmt *s = stmt_new(ST_EXPR, t->line, t->col);
    s->as.expr = e;
    match(p, TK_SEMI);
    return s;
}

static Func *parse_function(Parser *p, int is_pub, int is_static, StructDecl *owner, const char *forced_name) {
    Token *fn = expect(p, TK_FN, "'fn'");
    (void)fn;
    Token *name = forced_name ? NULL : expect(p, TK_IDENT, "function name");
    if (!forced_name && cur(p)->kind == TK_LT) {
        int depth = 0;
        do {
            if (cur(p)->kind == TK_LT) depth++;
            else if (cur(p)->kind == TK_GT) depth--;
            advance(p);
        } while (depth > 0 && cur(p)->kind != TK_EOF);
    }
    Func *f = (Func *)xcalloc(1, sizeof(Func));
    f->name = xstrdup(forced_name ? forced_name : name->text);
    f->is_pub = is_pub;
    f->is_static = is_static;
    f->owner = owner;
    f->is_method = owner && !is_static;
    expect(p, TK_LPAREN, "'('");
    Param *params = NULL;
    int np = 0;
    while (cur(p)->kind != TK_RPAREN && cur(p)->kind != TK_EOF) {
        if (match(p, TK_COMMA)) continue;
        Type *pt = NULL;
        char *pn = NULL;
        if (cur(p)->kind == TK_IDENT && (nth(p, 1)->kind == TK_COMMA || nth(p, 1)->kind == TK_RPAREN)) {
            pn = xstrdup(advance(p)->text);
            pt = type_new(TY_UNKNOWN, "auto");
        } else {
            pt = parse_type(p);
            Token *nt = expect(p, TK_IDENT, "parameter name");
            pn = xstrdup(nt->text);
        }
        params = (Param *)xrealloc(params, (size_t)(np + 1) * sizeof(Param));
        params[np].type = pt;
        params[np].name = pn;
        np++;
        if (!match(p, TK_COMMA) && cur(p)->kind != TK_RPAREN) break;
    }
    expect(p, TK_RPAREN, "')'");
    if (cur(p)->kind != TK_LBRACE && cur(p)->kind != TK_SEMI) {
        f->ret = parse_type(p);
    } else {
        f->ret = type_new(TY_VOID, "void");
        if (strcmp(f->name, "main") == 0) f->ret = type_new(TY_INT, "int");
    }
    f->params = params;
    f->nparams = np;
    if (match(p, TK_SEMI)) {
        f->body = NULL;
    } else {
        f->body = parse_block(p);
    }
    return f;
}

static void parse_struct(Parser *p, int is_pub) {
    (void)is_pub;
    expect(p, TK_STRUCT, "'struct'");
    Token *name = expect(p, TK_IDENT, "struct name");
    StructDecl *s = (StructDecl *)xcalloc(1, sizeof(StructDecl));
    s->name = xstrdup(name->text);
    expect(p, TK_LBRACE, "'{'");
    while (cur(p)->kind != TK_RBRACE && cur(p)->kind != TK_EOF) {
        if (match(p, TK_SEMI)) continue;
        int st = 0;
        if (match(p, TK_STATIC)) st = 1;
        if (cur(p)->kind == TK_FN) {
            Func *f = parse_function(p, 1, st, s, NULL);
            s->methods = (Func **)xrealloc(s->methods, (size_t)(s->nmethods + 1) * sizeof(Func *));
            s->methods[s->nmethods++] = f;
            continue;
        }
        Type *ft = parse_type(p);
        Token *fn = expect(p, TK_IDENT, "field name");
        s->fields = (Field *)xrealloc(s->fields, (size_t)(s->nfields + 1) * sizeof(Field));
        s->fields[s->nfields].type = ft;
        s->fields[s->nfields].name = xstrdup(fn->text);
        s->fields[s->nfields].default_value = NULL;
        if (match(p, TK_ASSIGN)) s->fields[s->nfields].default_value = parse_expression(p);
        s->nfields++;
        match(p, TK_SEMI);
    }
    expect(p, TK_RBRACE, "'}'");
    program_add_struct(p->program, s);
}

static void parse_cstruct(Parser *p, int is_union) {
    advance(p);
    Token *name = expect(p, TK_IDENT, "C struct name");
    StructDecl *s = (StructDecl *)xcalloc(1, sizeof(StructDecl));
    s->name = xstrdup(name->text);
    s->is_cstruct = 1;
    s->is_cunion = is_union;
    expect(p, TK_LBRACE, "'{'");
    while (cur(p)->kind != TK_RBRACE && cur(p)->kind != TK_EOF) {
        if (match(p, TK_SEMI) || match(p, TK_COMMA)) continue;
        Type *ft = parse_type(p);
        Token *fn = expect(p, TK_IDENT, "field name");
        s->fields = (Field *)xrealloc(s->fields, (size_t)(s->nfields + 1) * sizeof(Field));
        s->fields[s->nfields].type = ft;
        s->fields[s->nfields].name = xstrdup(fn->text);
        s->fields[s->nfields].default_value = NULL;
        s->nfields++;
        match(p, TK_SEMI);
    }
    expect(p, TK_RBRACE, "'}'");
    match(p, TK_SEMI);
    program_add_struct(p->program, s);
}

static void parse_cfn(Parser *p) {
    advance(p);
    Token *name = expect(p, TK_IDENT, "C function name");
    Func *f = (Func *)xcalloc(1, sizeof(Func));
    f->name = xstrdup(name->text);
    f->is_pub = 1;
    f->is_cfunc = 1;
    expect(p, TK_LPAREN, "'('");
    while (cur(p)->kind != TK_RPAREN && cur(p)->kind != TK_EOF) {
        if (match(p, TK_COMMA)) continue;
        if (match(p, TK_ELLIPSIS)) { f->is_variadic = 1; continue; }
        Type *pt = parse_type(p);
        char *pn;
        if (cur(p)->kind == TK_IDENT) pn = xstrdup(advance(p)->text);
        else pn = xasprintf("a%d", f->nparams);
        f->params = (Param *)xrealloc(f->params, (size_t)(f->nparams + 1) * sizeof(Param));
        f->params[f->nparams].type = pt;
        f->params[f->nparams].name = pn;
        f->nparams++;
    }
    expect(p, TK_RPAREN, "')'");
    f->ret = parse_type(p);
    match(p, TK_SEMI);
    program_add_func(p->program, f);
}

static void parse_enum(Parser *p) {
    expect(p, TK_ENUM, "'enum'");
    Token *name = expect(p, TK_IDENT, "enum name");
    EnumDecl *e = (EnumDecl *)xcalloc(1, sizeof(EnumDecl));
    e->name = xstrdup(name->text);
    expect(p, TK_LBRACE, "'{'");
    while (cur(p)->kind != TK_RBRACE && cur(p)->kind != TK_EOF) {
        if (match(p, TK_COMMA)) continue;
        Token *v = expect(p, TK_IDENT, "enum variant");
        Type **types = NULL;
        char **names = NULL;
        int n = 0;
        if (match(p, TK_LPAREN)) {
            while (cur(p)->kind != TK_RPAREN && cur(p)->kind != TK_EOF) {
                if (match(p, TK_COMMA)) continue;
                Type *t = parse_type(p);
                char *fn = NULL;
                if (cur(p)->kind == TK_IDENT) fn = xstrdup(advance(p)->text);
                else fn = xasprintf("v%d", n);
                types = (Type **)xrealloc(types, (size_t)(n + 1) * sizeof(Type *));
                names = (char **)xrealloc(names, (size_t)(n + 1) * sizeof(char *));
                types[n] = t;
                names[n] = fn;
                n++;
                if (!match(p, TK_COMMA) && cur(p)->kind != TK_RPAREN) break;
            }
            expect(p, TK_RPAREN, "')'");
            e->has_payload = 1;
        }
        e->variants = (char **)xrealloc(e->variants, (size_t)(e->nvariants + 1) * sizeof(char *));
        e->variant_types = (Type ***)xrealloc(e->variant_types, (size_t)(e->nvariants + 1) * sizeof(Type **));
        e->variant_field_names = (char ***)xrealloc(e->variant_field_names, (size_t)(e->nvariants + 1) * sizeof(char **));
        e->variant_nfields = (int *)xrealloc(e->variant_nfields, (size_t)(e->nvariants + 1) * sizeof(int));
        e->variants[e->nvariants] = xstrdup(v->text);
        e->variant_types[e->nvariants] = types;
        e->variant_field_names[e->nvariants] = names;
        e->variant_nfields[e->nvariants] = n;
        e->nvariants++;
        if (!match(p, TK_COMMA) && cur(p)->kind != TK_RBRACE && cur(p)->kind != TK_IDENT) break;
    }
    expect(p, TK_RBRACE, "'}'");
    program_add_enum(p->program, e);
}

static void parse_impl(Parser *p) {
    expect(p, TK_IMPL, "'impl'");
    Token *name = expect(p, TK_IDENT, "type name");
    StructDecl *s = find_struct_decl(p->program, name->text);
    if (!s) fatal_at(p->file, name->line, name->col, "unknown type '%s' in impl", name->text);
    expect(p, TK_LBRACE, "'{'");
    while (cur(p)->kind != TK_RBRACE && cur(p)->kind != TK_EOF) {
        if (match(p, TK_SEMI)) continue;
        int st = 0;
        if (match(p, TK_STATIC)) st = 1;
        if (cur(p)->kind != TK_FN)
            fatal_at(p->file, cur(p)->line, cur(p)->col, "expected fn in impl");
        Func *f = parse_function(p, 1, st, s, NULL);
        s->methods = (Func **)xrealloc(s->methods, (size_t)(s->nmethods + 1) * sizeof(Func *));
        s->methods[s->nmethods++] = f;
    }
    expect(p, TK_RBRACE, "'}'");
}

static void skip_c_decl(Parser *p) {
    int line = cur(p)->line;
    if (cur(p)->kind == TK_LT) {
        int depth = 0;
        do {
            if (cur(p)->kind == TK_LT) depth++;
            else if (cur(p)->kind == TK_GT) depth--;
            advance(p);
        } while (depth > 0 && cur(p)->kind != TK_EOF);
        return;
    }
    while (cur(p)->kind != TK_EOF && cur(p)->line == line && cur(p)->kind != TK_SEMI &&
           cur(p)->kind != TK_RBRACE)
        advance(p);
    match(p, TK_SEMI);
}

Program *parse_program(const char *src, const char *file) {
    int count = 0;
    Token *tokens = lex_all(src, file, &count);
    Parser p;
    p.tokens = tokens;
    p.count = count;
    p.pos = 0;
    p.program = (Program *)xcalloc(1, sizeof(Program));
    p.program->file = xstrdup(file);
    p.file = file;
    p.had_error = 0;

    int test_counter = 0;
    while (cur(&p)->kind != TK_EOF) {
        if (match(&p, TK_SEMI)) continue;
        int is_pub = 0;
        int is_async_fn = 0;
        if (match(&p, TK_PUB)) is_pub = 1;
        if (match(&p, TK_ASYNC)) {
            if (cur(&p)->kind != TK_FN)
                fatal_at(file, cur(&p)->line, cur(&p)->col, "expected fn after async");
            is_async_fn = 1;
        }
        if (cur(&p)->kind == TK_USE) {
            advance(&p);
            while (cur(&p)->kind == TK_IDENT || cur(&p)->kind == TK_DOT) advance(&p);
            if (match(&p, TK_AS)) match(&p, TK_IDENT);
            continue;
        }
        if (cur(&p)->kind == TK_CINCLUDE || cur(&p)->kind == TK_CIMPORT) {
            int is_import = cur(&p)->kind == TK_CIMPORT;
            advance(&p);
            if (cur(&p)->kind == TK_LT) {
                char *buf = xstrdup("<");
                advance(&p);
                while (cur(&p)->kind != TK_GT && cur(&p)->kind != TK_EOF) {
                    char *n = xasprintf("%s%s", buf, cur(&p)->text ? cur(&p)->text : "");
                    free(buf);
                    buf = n;
                    advance(&p);
                }
                match(&p, TK_GT);
                char *n = xasprintf("%s>", buf);
                free(buf);
                program_add_include(p.program, n);
                if (is_import) program_add_import(p.program, n);
                free(n);
            } else if (cur(&p)->kind == TK_STRING) {
                char *n = xasprintf("\"%s\"", cur(&p)->text);
                program_add_include(p.program, n);
                if (is_import) program_add_import(p.program, n);
                free(n);
                advance(&p);
            } else {
                skip_c_decl(&p);
            }
            match(&p, TK_SEMI);
            continue;
        }
        if (cur(&p)->kind == TK_CLINK) {
            advance(&p);
            if (cur(&p)->kind == TK_STRING) {
                program_add_link(p.program, cur(&p)->text);
                advance(&p);
            } else {
                skip_c_decl(&p);
            }
            match(&p, TK_SEMI);
            continue;
        }
        if (cur(&p)->kind == TK_CFN) {
            parse_cfn(&p);
            continue;
        }
        if (cur(&p)->kind == TK_CSTRUCT || cur(&p)->kind == TK_CUNION) {
            int is_union = cur(&p)->kind == TK_CUNION;
            parse_cstruct(&p, is_union);
            continue;
        }
        if (cur(&p)->kind == TK_TYPE) {
            advance(&p);
            Token *alias_name = expect(&p, TK_IDENT, "type alias name");
            Type *alias_type = NULL;
            if (match(&p, TK_ASSIGN)) alias_type = parse_type(&p);
            if (alias_type) type_alias_define(alias_name->text, alias_type);
            match(&p, TK_SEMI);
            continue;
        }
        if (cur(&p)->kind == TK_STRUCT) {
            parse_struct(&p, is_pub);
            continue;
        }
        if (cur(&p)->kind == TK_ENUM) {
            parse_enum(&p);
            continue;
        }
        if (cur(&p)->kind == TK_IMPL) {
            parse_impl(&p);
            continue;
        }
        if (cur(&p)->kind == TK_TEST) {
            advance(&p);
            char *name = NULL;
            if (cur(&p)->kind == TK_STRING) name = xstrdup(advance(&p)->text);
            char buf[64];
            snprintf(buf, sizeof(buf), "__test_%d", test_counter++);
            Func *f = parse_function(&p, 0, 0, NULL, buf);
            f->is_test = 1;
            (void)name;
            program_add_func(p.program, f);
            continue;
        }
        if (match(&p, TK_CONST)) {
            if (cur(&p)->kind == TK_FN) {
                Func *f = parse_function(&p, is_pub, 0, NULL, NULL);
                program_add_func(p.program, f);
            } else {
                Stmt *s = stmt_new(ST_VAR, cur(&p)->line, cur(&p)->col);
                s->as.var.type = parse_type(&p);
                Token *n = expect(&p, TK_IDENT, "constant name");
                s->as.var.name = xstrdup(n->text);
                if (match(&p, TK_ASSIGN)) s->as.var.init = parse_expression(&p);
                match(&p, TK_SEMI);
                program_add_global(p.program, s);
            }
            continue;
        }
        if (match(&p, TK_STATIC)) {
            if (cur(&p)->kind == TK_FN) {
                Func *f = parse_function(&p, is_pub, 1, NULL, NULL);
                program_add_func(p.program, f);
            } else {
                Stmt *s = stmt_new(ST_VAR, cur(&p)->line, cur(&p)->col);
                s->as.var.type = parse_type(&p);
                Token *n = expect(&p, TK_IDENT, "global name");
                s->as.var.name = xstrdup(n->text);
                if (match(&p, TK_ASSIGN)) s->as.var.init = parse_expression(&p);
                match(&p, TK_SEMI);
                program_add_global(p.program, s);
            }
            continue;
        }
        if (cur(&p)->kind == TK_FN) {
            Func *f = parse_function(&p, is_pub, 0, NULL, NULL);
            f->is_async = is_async_fn;
            program_add_func(p.program, f);
            continue;
        }
        if (looks_like_type(&p)) {
            Stmt *s = stmt_new(ST_VAR, cur(&p)->line, cur(&p)->col);
            s->as.var.type = parse_type(&p);
            Token *n = expect(&p, TK_IDENT, "global name");
            s->as.var.name = xstrdup(n->text);
            if (match(&p, TK_ASSIGN)) s->as.var.init = parse_expression(&p);
            match(&p, TK_SEMI);
            program_add_global(p.program, s);
            continue;
        }
        fatal_at(file, cur(&p)->line, cur(&p)->col, "unexpected %s '%s' at top level",
                 token_kind_name(cur(&p)->kind), cur(&p)->text ? cur(&p)->text : "");
    }
    if (file && strlen(file) > 5 && strcmp(file + strlen(file) - 5, ".akti") == 0) {
        for (int i = 0; i < p.program->nstructs; i++)
            if (p.program->structs[i]->is_cstruct) p.program->structs[i]->from_binding = 1;
    }
    return p.program;
}