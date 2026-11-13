// cimport.c - turns C header declarations into akt declarations.
//
// This is not a user-facing bindgen step: aktc runs the importer
// when it sees `cinclude`, and stores the result in the project cache.

#include "aktc.h"

#include <ctype.h>
#include <stdarg.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} TBuf;

static void tb_init(TBuf *b) {
    b->cap = 256;
    b->data = (char *)xmalloc(b->cap);
    b->data[0] = 0;
    b->len = 0;
}

static void tb_add(TBuf *b, const char *s) {
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap) b->cap *= 2;
        b->data = (char *)xrealloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void tb_addf(TBuf *b, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n <= 0) { va_end(ap2); return; }
    char *tmp = (char *)xmalloc((size_t)n + 1);
    vsnprintf(tmp, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    tb_add(b, tmp);
    free(tmp);
}

static char *strtok_s_compat(char *s, const char *delim, char **save);

static char *g_seen[8192];
static int g_nseen;

static int seen_add(const char *name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < g_nseen; i++)
        if (strcmp(g_seen[i], name) == 0) return 0;
    if (g_nseen < 8192) g_seen[g_nseen++] = xstrdup(name);
    return 1;
}

static void seen_reset(void) {
    for (int i = 0; i < g_nseen; i++) free(g_seen[i]);
    g_nseen = 0;
}

static char *strip_comments(const char *src) {
    size_t cap = strlen(src) + 1, len = 0;
    char *out = (char *)xmalloc(cap);
    const char *p = src;
    while (*p) {
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            out[len++] = ' ';
            continue;
        }
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            continue;
        }
        out[len++] = *p++;
    }
    out[len] = 0;
    return out;
}

static char *read_word(char **pp) {
    char *p = *pp;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    char *start = p;
    while (isalnum((unsigned char)*p) || *p == '_') p++;
    if (p == start) { *pp = p; return NULL; }
    char *w = (char *)xmalloc((size_t)(p - start) + 1);
    memcpy(w, start, (size_t)(p - start));
    w[p - start] = 0;
    *pp = p;
    return w;
}

static int is_noise_word(const char *w) {
    static const char *noise[] = {
        "const", "volatile", "restrict", "struct", "enum", "union",
        "static", "extern", "inline", "register", "auto",
        "__cdecl", "__stdcall", "__fastcall", "__restrict", "__inline",
        "GLFWAPI", "GLFWCALL", "GLFWAPIENTRY", "GLAPI", "APIENTRY",
        "WINGDIAPI", "WINAPI", "CALLBACK", "APIENTRYP", "DECLSPEC",
        "_In_", "_Out_", "_Inout_", "_In_opt_", "_Out_opt_",
        "_Success_", "__declspec", NULL
    };
    for (int i = 0; noise[i]; i++)
        if (strcmp(w, noise[i]) == 0) return 1;
    return 0;
}

// C type -> akt type. Returns NULL if the type is not recognized.
static char *map_type(const char *in) {
    int stars = 0;    char words[8][40];
    int nw = 0;
    const char *p = in;
    while (*p) {
        if (*p == '*') { stars++; p++; continue; }
        if (isspace((unsigned char)*p)) { p++; continue; }
        if (*p == '[' || *p == ']' || *p == '(' || *p == ')') { p++; continue; }
        if (isalpha((unsigned char)*p) || *p == '_') {
            int n = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && n < 39) words[nw][n++] = *p++;
            words[nw][n] = 0;
            if (is_noise_word(words[nw])) continue;
            nw++;
            if (nw >= 8) break;
            continue;
        }
        p++;
    }
    if (nw == 0) return NULL;
    const char *base = NULL;
    char comb[96] = "";
    if (nw == 1) {
        const char *w = words[0];
        if (!strcmp(w, "void")) base = "void";
        else if (!strcmp(w, "char")) base = "cchar";
        else if (!strcmp(w, "int")) base = "int";
        else if (!strcmp(w, "float")) base = "f32";
        else if (!strcmp(w, "double")) base = "f64";
        else if (!strcmp(w, "size_t")) base = "size";
        else if (!strcmp(w, "ptrdiff_t")) base = "isize";
        else if (!strcmp(w, "short")) base = "i16";
        else if (!strcmp(w, "long")) base = "i32";
        else if (!strcmp(w, "bool")) base = "bool";
        else if (!strcmp(w, "int8_t")) base = "i8";
        else if (!strcmp(w, "uint8_t")) base = "u8";
        else if (!strcmp(w, "int16_t")) base = "i16";
        else if (!strcmp(w, "uint16_t")) base = "u16";
        else if (!strcmp(w, "int32_t")) base = "i32";
        else if (!strcmp(w, "uint32_t")) base = "u32";
        else if (!strcmp(w, "int64_t")) base = "i64";
        else if (!strcmp(w, "uint64_t")) base = "u64";
        else base = w;
    } else {
        int u = 0, sg = 0;
        for (int i = 0; i < nw; i++) {
            if (!strcmp(words[i], "unsigned")) u = 1;
            if (!strcmp(words[i], "signed")) sg = 1;
        }
        const char *last = words[nw - 1];
        snprintf(comb, sizeof(comb), "%s", last);
        if (u) {
            if (!strcmp(last, "char")) base = "u8";
            else if (!strcmp(last, "short")) base = "u16";
            else if (!strcmp(last, "int")) base = "u32";
            else if (!strcmp(last, "long")) base = "u32";
            else base = NULL;
            if (nw >= 3 && !strcmp(words[nw - 2], "long") && !strcmp(last, "long")) base = "u64";
        } else if (sg) {
            if (!strcmp(last, "char")) base = "i8";
            else base = "int";
        } else if (!strcmp(last, "long") && nw >= 2 && !strcmp(words[nw - 2], "long")) {
            base = "i64";
        } else {
            base = last;
        }
    }
    if (!base) return NULL;
    char *out = xasprintf("%s", base);
    for (int i = 0; i < stars; i++) {
        char *n = xasprintf("* %s", out);
        free(out);
        out = n;
    }
    return out;
}

static void strip_param_name(char *t) {
    char *e = t + strlen(t);
    while (e > t && isspace((unsigned char)e[-1])) e--;
    char *q = e;
    while (q > t && (isalnum((unsigned char)q[-1]) || q[-1] == '_')) q--;
    if (q == e || q == t) return;
    char *word = (char *)xmalloc((size_t)(e - q) + 1);
    memcpy(word, q, (size_t)(e - q));
    word[e - q] = 0;
    static const char *keywords[] = {"void", "char", "int", "float", "double", "long", "short",
                                     "unsigned", "signed", "size_t", "ptrdiff_t", "bool", "const", NULL};
    int is_kw = 0;
    for (int i = 0; keywords[i]; i++) if (!strcmp(word, keywords[i])) is_kw = 1;
    free(word);
    if (is_kw) return;
    char *w = q;
    while (w > t && isspace((unsigned char)w[-1])) w--;
    *w = 0;
}

static char *map_funcptr(const char *in) {
    const char *p = strstr(in, "(*");
    if (!p) return NULL;
    char *ret = xasprintf("%.*s", (int)(p - in), in);
    char *ret_akt = map_type(ret);
    free(ret);
    if (!ret_akt) return NULL;
    const char *q = p + 2;
    while (*q == ' ' || *q == '\t') q++;
    const char *e = q;
    while (*e && *e != ')') e++;
    if (*e != ')') { free(ret_akt); return NULL; }
    const char *args = strchr(e, '(');
    if (!args) { free(ret_akt); return NULL; }
    const char *args_end = strchr(args, ')');
    if (!args_end) { free(ret_akt); return NULL; }
    char *args_src = xasprintf("%.*s", (int)(args_end - args - 1), args + 1);
    char *plist = NULL;
    int count = 0;
    char *save = NULL;
    for (char *prm = strtok_s_compat(args_src, ",", &save); prm; prm = strtok_s_compat(NULL, ",", &save)) {
        char *t = prm;
        while (*t == ' ' || *t == '\t') t++;
        char *end = t + strlen(t);
        while (end > t && isspace((unsigned char)end[-1])) *--end = 0;
        if (!t[0] || !strcmp(t, "void")) continue;
        strip_param_name(t);
        char *pt = map_type(t);
        if (!pt) { free(plist); free(args_src); free(ret_akt); return NULL; }
        char *n = xasprintf("%s%s", plist ? plist : "", count ? ", " : "");
        free(plist);
        plist = xasprintf("%s%s", n, pt);
        free(n);
        free(pt);
        count++;
    }
    char *out = xasprintf("fn(%s) %s", plist ? plist : "", ret_akt);
    free(plist);
    free(args_src);
    free(ret_akt);
    return out;
}

static char *strtok_s_compat(char *s, const char *delim, char **save) {
    if (!s) s = *save;
    if (!s) return NULL;
    char *start = s;
    while (*s && !strchr(delim, *s)) s++;
    if (*s) { *s = 0; *save = s + 1; }
    else *save = NULL;
    return start;
}

static void emit_struct(TBuf *o, const char *name, int is_union, const char *body) {
    if (!seen_add(name)) return;
    tb_addf(o, "c%s %s {\n", is_union ? "union" : "struct", name);
    char *copy = xstrdup(body);
    char *save = NULL;
    for (char *field = strtok_s_compat(copy, ";", &save); field; field = strtok_s_compat(NULL, ";", &save)) {
        char *f = field;
        while (*f == ' ' || *f == '\t' || *f == '\r' || *f == '\n') f++;
        char *end = f + strlen(f);
        while (end > f && isspace((unsigned char)end[-1])) *--end = 0;
        if (!f[0]) continue;
        if (strchr(f, ':')) continue;  // bitfield
        char *arr = NULL;
        char *br = strchr(f, '[');
        if (br && f[strlen(f) - 1] == ']') {
            *br = 0;
            char *endbr = f + strlen(f) + strlen(br + 1);
            arr = xstrndup(br + 1, (size_t)(endbr - (br + 1)));
            if (arr[0]) arr[strlen(arr) - 1] = 0;
        }
        char *q = f + strlen(f);
        while (q > f && isspace((unsigned char)q[-1])) q--;
        while (q > f && (isalnum((unsigned char)q[-1]) || q[-1] == '_')) q--;
        if (q == f) { free(arr); continue; }
        char *fname = xstrdup(q);
        char *ftype = xasprintf("%.*s", (int)(q - f), f);
        char *at = map_funcptr(ftype);
        if (!at) at = map_type(ftype);
        if (!at) { free(ftype); free(fname); free(arr); continue; }
        if (arr && arr[0]) {
            tb_addf(o, "    [%s; %s] %s\n", at, arr, fname);
        } else if (!arr) {
            tb_addf(o, "    %s %s\n", at, fname);
        }
        free(at); free(ftype); free(fname); free(arr);
    }
    tb_addf(o, "}\n\n");
    free(copy);
}

static void emit_enum(TBuf *o, const char *body) {
    char *copy = xstrdup(body);
    char *save = NULL;
    long long next = 0;
    for (char *item = strtok_s_compat(copy, ",", &save); item; item = strtok_s_compat(NULL, ",", &save)) {
        char *s = item;
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        char *end = s + strlen(s);
        while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
        if (!s[0]) continue;
        char *eq = strchr(s, '=');
        char *name = s;
        char *val = NULL;
        if (eq) {
            *eq = 0;
            val = eq + 1;
            while (*val == ' ' || *val == '\t') val++;
            char *e2 = val + strlen(val);
            while (e2 > val && isspace((unsigned char)e2[-1])) *--e2 = 0;
        }
        name[strcspn(name, " \t")] = 0;
        if (!name[0]) continue;
        if (!seen_add(name)) continue;
        if (val && val[0]) {
            tb_addf(o, "const i64 %s = %s\n", name, val);
            next = 0;
        } else {
            tb_addf(o, "const i64 %s = %lld\n", name, next);
            next++;
        }
    }
    free(copy);
}

static void emit_define(TBuf *o, const char *name, const char *value) {
    if (!seen_add(name)) return;
    const char *v = value;
    while (*v == ' ' || *v == '\t') v++;
    if (!v[0]) return;
    if (strchr(v, '(')) return;
    char *copy = xstrdup(v);
    char *e = copy + strlen(copy);
    while (e > copy && isspace((unsigned char)e[-1])) *--e = 0;
    e = copy + strlen(copy);
    while (e > copy && (e[-1] == 'u' || e[-1] == 'U' || e[-1] == 'l' || e[-1] == 'L')) *--e = 0;
    if (!copy[0]) { free(copy); return; }
    int ok = 1;
    const char *p = copy;
    if (*p == '-' || *p == '+') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        if (!isxdigit((unsigned char)*p)) ok = 0;
        while (*p) { if (!isxdigit((unsigned char)*p)) { ok = 0; break; } p++; }
    } else {
        if (!isdigit((unsigned char)*p)) ok = 0;
        while (*p) { if (!isdigit((unsigned char)*p)) { ok = 0; break; } p++; }
    }
    if (ok) tb_addf(o, "const i64 %s = %s\n", name, copy);
    free(copy);
}

static void emit_proto(TBuf *o, const char *stmt) {
    const char *open = strchr(stmt, '(');
    if (!open) return;
    int depth = 0;
    const char *close = open;
    for (; *close; close++) {
        if (*close == '(') depth++;
        else if (*close == ')') { depth--; if (!depth) break; }
    }
    if (!*close) return;
    char *head = xasprintf("%.*s", (int)(open - stmt), stmt);
    char *ret = head;
    while (*ret == ' ' || *ret == '\t') ret++;
    char *q = ret + strlen(ret);
    while (q > ret && isspace((unsigned char)q[-1])) q--;
    while (q > ret && (isalnum((unsigned char)q[-1]) || q[-1] == '_')) q--;
    if (q == ret) { free(head); return; }
    char *name = xstrdup(q);
    char *rtype = xasprintf("%.*s", (int)(q - ret), ret);
    char *at = map_type(rtype);
    if (!at || !name[0] || strchr(name, ' ')) { free(head); free(name); free(rtype); free(at); return; }
    if (!seen_add(name)) { free(head); free(name); free(rtype); free(at); return; }
    char *params = xasprintf("%.*s", (int)(close - open - 1), open + 1);
    char *plist = NULL;
    int count = 0;
    char *save = NULL;
    for (char *prm = strtok_s_compat(params, ",", &save); prm; prm = strtok_s_compat(NULL, ",", &save)) {
        char *t = prm;
        while (*t == ' ' || *t == '\t') t++;
        char *end = t + strlen(t);
        while (end > t && isspace((unsigned char)end[-1])) *--end = 0;
        if (!t[0]) continue;
        if (!strcmp(t, "...")) {
            char *n = xasprintf("%s%s...", plist ? plist : "", count ? ", " : "");
            free(plist);
            plist = n;
            count++;
            continue;
        }
        if (!strcmp(t, "void")) continue;
        if (strchr(t, '(')) {
            char *fp = map_funcptr(t);
            if (!fp) { free(plist); free(params); free(at); free(name); free(rtype); free(head); return; }
            char *n = xasprintf("%s%s%s a%d", plist ? plist : "", count ? ", " : "", fp, count);
            free(plist);
            plist = n;
            free(fp);
            count++;
            continue;
        }
        strip_param_name(t);
        char *pt = map_type(t);
        if (!pt) { free(plist); free(params); free(at); free(name); free(rtype); free(head); return; }
        char *n = xasprintf("%s%s%s a%d", plist ? plist : "", count ? ", " : "", pt, count);
        free(plist);
        plist = n;
        free(pt);
        count++;
    }
    tb_addf(o, "cfn %s(%s) %s\n", name, plist ? plist : "", at);
    free(plist);
    free(params);
    free(at);
    free(name);
    free(rtype);
    free(head);
}

char *cimport_from_source(const char *src) {
    seen_reset();
    char *clean = strip_comments(src);
    TBuf out;
    tb_init(&out);

    /* #define NAME <number> */
    {
        const char *p = clean;
        while (*p) {
            const char *line = p;
            while (*p && *p != '\n') p++;
            size_t n = (size_t)(p - line);
            if (*p == '\n') p++;
            if (n < 8 || strncmp(line, "#define", 7) != 0) continue;
            const char *q = line + 7;
            while (*q == ' ' || *q == '\t') q++;
            const char *name_end = q;
            while ((isalnum((unsigned char)*name_end) || *name_end == '_') && name_end < line + n) name_end++;
            if (name_end == q) continue;
            char *name = xstrndup(q, (size_t)(name_end - q));
            const char *val = name_end;
            if (val < line + n) {
                char *value = xstrndup(val, (size_t)(line + n - val));
                emit_define(&out, name, value);
                free(value);
            }
            free(name);
        }
    }

    char *p = clean;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (!strncmp(p, "typedef", 7) && (p[7] == ' ' || p[7] == '\t')) {
            char *q = p + 7;
            while (*q == ' ' || *q == '\t') q++;
            int is_union = 0, is_struct = 0;
            if (!strncmp(q, "struct", 6)) { is_struct = 1; q += 6; }
            else if (!strncmp(q, "union", 5)) { is_union = 1; q += 5; }
            if (is_struct || is_union) {
                while (*q == ' ' || *q == '\t') q++;
                char *tag = NULL;
                if (isalpha((unsigned char)*q) || *q == '_') tag = read_word(&q);
                while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
                if (*q == '{') {
                    q++;
                    char *body_start = q;
                    int depth = 1;
                    while (*q && depth) {
                        if (*q == '{') depth++;
                        else if (*q == '}') { depth--; if (!depth) break; }
                        q++;
                    }
                    char *body = xasprintf("%.*s", (int)(q - body_start), body_start);
                    if (*q == '}') q++;
                    while (*q == ' ' || *q == '\t') q++;
                    char *name = NULL;
                    if (isalpha((unsigned char)*q) || *q == '_') name = read_word(&q);
                    if (!name) name = tag ? xstrdup(tag) : xstrdup("Anonymous");
                    while (*q && *q != ';') q++;
                    if (*q == ';') q++;
                    emit_struct(&out, name, is_union, body);
                    free(body);
                    if (tag && (!name || strcmp(tag, name) != 0)) free(tag);
                    free(name);
                    p = q;
                    continue;
                }
                /* typedef struct Tag Alias; - opaque type */
                char *alias = NULL;
                if (isalpha((unsigned char)*q) || *q == '_') alias = read_word(&q);
                while (*q && *q != ';') q++;
                if (*q == ';') q++;
                if (tag) {
                    tb_addf(&out, "cstruct %s {\n}\n\n", tag);
                    if (alias && strcmp(alias, tag) != 0 && seen_add(alias))
                        tb_addf(&out, "type %s = %s\n", alias, tag);
                } else if (alias) {
                    tb_addf(&out, "cstruct %s {\n}\n\n", alias);
                }
                free(tag);
                free(alias);
                p = q;
                continue;
            }
            /* typedef funcptr / plain alias */
            char *semi = q;
            int d = 0;
            while (*semi && !(*semi == ';' && d == 0)) {
                if (*semi == '(') d++;
                else if (*semi == ')') d--;
                semi++;
            }
            char *stmt = xasprintf("%.*s", (int)(semi - q), q);
            if (*semi == ';') semi++;
            char *fp = map_funcptr(stmt);
            if (fp) {
                const char *pfp = strstr(stmt, "(*");
                const char *qn = pfp + 2;
                while (*qn == ' ' || *qn == '\t') qn++;
                const char *en = qn;
                while (*en && *en != ')') en++;
                char *alias = xasprintf("%.*s", (int)(en - qn), qn);
                if (alias[0] && !strchr(alias, ' ') && seen_add(alias))
                    tb_addf(&out, "type %s = %s\n", alias, fp);
                free(alias);
                free(fp);
            } else {
                char *t = stmt;
                char *end = t + strlen(t);
                while (end > t && isspace((unsigned char)end[-1])) *--end = 0;
                char *qn = end;
                while (qn > t && (isalnum((unsigned char)qn[-1]) || qn[-1] == '_')) qn--;
                if (qn > t) {
                    char *alias = xstrdup(qn);
                    char *base = xasprintf("%.*s", (int)(qn - t), t);
                    char *at = map_type(base);
                    if (at && alias[0] && strcmp(alias, at) != 0 && seen_add(alias))
                        tb_addf(&out, "type %s = %s\n", alias, at);
                    free(at);
                    free(alias);
                    free(base);
                }
            }
            free(stmt);
            p = semi;
            continue;
        }
        if (!strncmp(p, "enum", 4) && (p[4] == ' ' || p[4] == '\t' || p[4] == '{')) {
            char *q = p + 4;
            while (*q == ' ' || *q == '\t') q++;
            if (isalpha((unsigned char)*q) || *q == '_') { char *t = read_word(&q); free(t); }
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
            if (*q == '{') {
                q++;
                char *body_start = q;
                int depth = 1;
                while (*q && depth) {
                    if (*q == '{') depth++;
                    else if (*q == '}') { depth--; if (!depth) break; }
                    q++;
                }
                char *body = xasprintf("%.*s", (int)(q - body_start), body_start);
                if (*q == '}') q++;
                while (*q && *q != ';') q++;
                if (*q == ';') q++;
                emit_enum(&out, body);
                free(body);
                p = q;
                continue;
            }
        }
        if ((!strncmp(p, "struct", 6) || !strncmp(p, "union", 5)) && strchr(p, '{')) {
            char *q = (char *)p + (*p == 'u' ? 5 : 6);
            while (*q == ' ' || *q == '\t') q++;
            char *tag = NULL;
            if (isalpha((unsigned char)*q) || *q == '_') tag = read_word(&q);
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
            if (tag && *q == '{') {
                const char *open = q;
                const char *close = open;
                int depth = 0;
                for (; *close; close++) {
                    if (*close == '{') depth++;
                    else if (*close == '}') { depth--; if (!depth) break; }
                }
                if (*close) {
                    char *body = xasprintf("%.*s", (int)(close - open - 1), open + 1);
                    /* no typedef: declare a cstruct under the tag */
                    int is_union = (*p == 'u');
                    TBuf tmp;
                    tb_init(&tmp);
                    emit_struct(&tmp, tag, is_union, body);
                    tb_add(&out, tmp.data);
                    free(tmp.data);
                    free(body);
                    p = (char *)close + 1;
                    if (*p == ';') p++;
                    free(tag);
                    continue;
                }
            }
            free(tag);
        }
        char *q = p;
        int depth = 0;
        while (*q && !(*q == ';' && depth == 0)) {
            if (*q == '(') depth++;
            else if (*q == ')') depth--;
            q++;
        }
        char *stmt = xasprintf("%.*s", (int)(q - p), p);
        if (*q == ';') q++;
        char *s = stmt;
        while (*s == ' ' || *s == '\t') s++;
        if (s[0] && strchr(s, '(') && !strchr(s, '=') && !strstr(s, "typedef") &&
            !strstr(s, "extern") && !strstr(s, "static"))
            emit_proto(&out, s);
        free(stmt);
        p = q;
    }
    free(clean);
    return out.data;
}

/* ---------- built-in C standard library declarations ---------- */

static const char *k_builtin_string =
    "cfn strlen(*const cchar s) size\n"
    "cfn strcmp(*const cchar a, *const cchar b) int\n"
    "cfn strncmp(*const cchar a, *const cchar b, size n) int\n"
    "cfn strcpy(*cchar dst, *const cchar src) *cchar\n"
    "cfn strncpy(*cchar dst, *const cchar src, size n) *cchar\n"
    "cfn strcat(*cchar dst, *const cchar src) *cchar\n"
    "cfn strchr(*const cchar s, int c) *cchar\n"
    "cfn strrchr(*const cchar s, int c) *cchar\n"
    "cfn strstr(*const cchar s, *const cchar sub) *cchar\n"
    "cfn memcpy(*void dst, *const void src, size n) *void\n"
    "cfn memmove(*void dst, *const void src, size n) *void\n"
    "cfn memset(*void dst, int c, size n) *void\n"
    "cfn memcmp(*const void a, *const void b, size n) int\n";

static const char *k_builtin_stdio =
    "cfn printf(*const cchar fmt, ...) int\n"
    "cfn snprintf(*cchar buf, size n, *const cchar fmt, ...) int\n"
    "cfn sprintf(*cchar buf, *const cchar fmt, ...) int\n"
    "cfn fprintf(*void stream, *const cchar fmt, ...) int\n"
    "cfn puts(*const cchar s) int\n"
    "cfn putchar(int c) int\n"
    "cfn fopen(*const cchar path, *const cchar mode) *void\n"
    "cfn fclose(*void f) int\n"
    "cfn fread(*void buf, size sz, size n, *void f) size\n"
    "cfn fwrite(*const void buf, size sz, size n, *void f) size\n";

static const char *k_builtin_stdlib =
    "cstruct div_t {\n"
    "    int quot\n"
    "    int rem\n"
    "}\n"
    "cfn malloc(size n) *void\n"
    "cfn calloc(size n, size sz) *void\n"
    "cfn realloc(*void p, size n) *void\n"
    "cfn free(*void p) void\n"
    "cfn exit(int code) void\n"
    "cfn abs(int x) int\n"
    "cfn labs(i64 x) i64\n"
    "cfn atoi(*const cchar s) int\n"
    "cfn atof(*const cchar s) f64\n"
    "cfn strtol(*const cchar s, * *cchar end, int base) i64\n"
    "cfn div(int a, int b) div_t\n"
    "cfn rand() int\n"
    "cfn srand(u32 seed) void\n"
    "cfn atexit(fn() void cb) int\n"
    "cfn qsort(*void base, size n, size size, fn(*const void, *const void) int cmp) void\n";

static const char *k_builtin_math =
    "cfn sin(f64 x) f64\n"
    "cfn cos(f64 x) f64\n"
    "cfn tan(f64 x) f64\n"
    "cfn asin(f64 x) f64\n"
    "cfn acos(f64 x) f64\n"
    "cfn atan(f64 x) f64\n"
    "cfn atan2(f64 y, f64 x) f64\n"
    "cfn sqrt(f64 x) f64\n"
    "cfn pow(f64 x, f64 y) f64\n"
    "cfn fabs(f64 x) f64\n"
    "cfn floor(f64 x) f64\n"
    "cfn ceil(f64 x) f64\n"
    "cfn round(f64 x) f64\n"
    "cfn fmod(f64 x, f64 y) f64\n"
    "cfn log(f64 x) f64\n"
    "cfn exp(f64 x) f64\n";

const char *cimport_builtin(const char *inc) {
    if (!inc) return NULL;
    const char *name = inc;
    if (*name == '<' || *name == '"') name++;
    const char *slash = strrchr(name, '/');
    const char *bs = strrchr(name, '\\');
    if (!slash || (bs && bs > slash)) slash = bs;
    if (slash) name = slash + 1;
    if (!strncmp(name, "string.h", 8) || !strncmp(name, "strings.h", 9)) return k_builtin_string;
    if (!strncmp(name, "stdio.h", 7)) return k_builtin_stdio;
    if (!strncmp(name, "stdlib.h", 8)) return k_builtin_stdlib;
    if (!strncmp(name, "math.h", 6)) return k_builtin_math;
    return NULL;
}

