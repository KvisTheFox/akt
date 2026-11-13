#include "aktc.h"

#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <windows.h>
#include <direct.h>
#endif

#ifndef AKT_ROOT
#define AKT_ROOT "."
#endif

static char *g_includes[128];
static char *g_libs[128];
static char *g_libdirs[128];
static char *g_defines[128];
static int g_nincludes, g_nlibs, g_nlibdirs, g_ndefines;
static const char *g_target = NULL;
static const char *g_sysroot = NULL;
static int g_shared = 0;
static char *g_cache_dir = NULL;
static const char *g_cc_override = NULL;

static void add_flag(char **arr, int *n, const char *v) {
    if (*n < 128) arr[(*n)++] = xstrdup(v);
}

static char *dirname_copy(const char *path);
static int exists(const char *path);

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) fatal("cannot open '%s': %s", path, strerror(errno));
    if (fseek(f, 0, SEEK_END) != 0) fatal("cannot seek '%s'", path);
    long n = ftell(f);
    if (n < 0) fatal("cannot tell '%s'", path);
    rewind(f);
    char *p = (char *)xmalloc((size_t)n + 1);
    size_t got = fread(p, 1, (size_t)n, f);
    fclose(f);
    p[got] = 0;
    return p;
}

typedef struct {
    char **items;
    int count;
} Visited;

static int visited_has(Visited *v, const char *path) {
    for (int i = 0; i < v->count; i++)
        if (strcmp(v->items[i], path) == 0) return 1;
    return 0;
}
static void visited_add(Visited *v, const char *path) {
    v->items = (char **)xrealloc(v->items, (size_t)(v->count + 1) * sizeof(char *));
    v->items[v->count++] = xstrdup(path);
}

static char *dirname_copy(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bs = strrchr(path, '\\');
    if (!slash || (bs && bs > slash)) slash = bs;
    if (!slash) return xstrdup(".");
    return xstrndup(path, (size_t)(slash - path));
}

static char *source_root(const char *file) {
    const char *p = file;
    const char *best = NULL;
    while ((p = strstr(p, "src")) != NULL) {
        char prev = (p == file) ? 0 : p[-1];
        char next = p[3];
        if ((prev == 0 || prev == '/' || prev == '\\') &&
            (next == '/' || next == '\\')) {
            best = p + 3;
        }
        p += 3;
    }
    if (best) return xstrndup(file, (size_t)(best - file));
    return dirname_copy(file);
}

static char *resolve_module(const char *root, const char *module) {
    char *rel = xstrdup(module);
    for (char *p = rel; *p; p++) if (*p == '.') *p = '/';
    char *path = xasprintf("%s/%s.akt", root, rel);
    free(rel);
#ifdef _WIN32
    for (char *p = path; *p; p++) if (*p == '/') *p = '\\';
#endif
    return path;
}

/* ---------- project cache (.kache) ---------- */

static int is_dir(const char *path) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static void make_dir(const char *path) {
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

static void make_dirs(const char *path) {
    char *tmp = xstrdup(path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char save = *p;
            *p = 0;
            if (!is_dir(tmp)) make_dir(tmp);
            *p = save;
        }
    }
    if (!is_dir(tmp)) make_dir(tmp);
    free(tmp);
}

static char *cache_root(const char *input) {
    const char *env = getenv("AKT_CACHE");
    char *c = NULL;
    if (env && *env) {
        c = xstrdup(env);
    } else {
        char *dir = dirname_copy(input);
        for (int depth = 0; depth < 64; depth++) {
            char *mf = xasprintf("%s/katek.toml", dir);
            FILE *f = fopen(mf, "rb");
            free(mf);
            if (f) {
                fclose(f);
                c = xasprintf("%s/.kache", dir);
                free(dir);
                dir = NULL;
                break;
            }
            char *up = dirname_copy(dir);
            if (strcmp(up, dir) == 0) { free(up); break; }
            free(dir);
            dir = up;
        }
        if (!c) {
            c = xasprintf("%s/.kache", dir);
            free(dir);
        }
    }
#ifdef _WIN32
    {
        char buf[4096];
        DWORD n = GetFullPathNameA(c, (DWORD)sizeof(buf), buf, NULL);
        if (n > 0 && n < sizeof(buf)) {
            free(c);
            c = xstrdup(buf);
        }
    }
#endif
    return c;
}

static char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bs = strrchr(path, '\\');
    if (!slash || (bs && bs > slash)) slash = bs;
    char *name = xstrdup(slash ? slash + 1 : path);
    char *dot = strrchr(name, '.');
    if (dot) *dot = 0;
    return name;
}

static char *cache_sub(const char *cache, const char *sub) {
    char *dir = xasprintf("%s/%s", cache, sub);
    make_dirs(dir);
    return dir;
}

static char *read_file_opt(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    char *p = (char *)xmalloc((size_t)n + 1);
    size_t got = fread(p, 1, (size_t)n, f);
    fclose(f);
    p[got] = 0;
    return p;
}

static unsigned long long hash_text(const char *s) {
    unsigned long long h = 1469598103934665603ULL;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        h ^= *p;
        h *= 1099511628211ULL;
    }
    return h;
}

/* ---------- C header lookup ---------- */

static char *g_src_dir = NULL;

static char *find_header_path(const char *inc, const char *src_dir) {
    if (!inc) return NULL;
    int quoted = inc[0] == '"';
    char *name = xstrdup(inc);
    size_t n = strlen(name);
    if (n >= 2) {
        name[n - 1] = 0;
        memmove(name, name + 1, n - 1);
    }
    char *cand = NULL;
    if (quoted && src_dir) {
        cand = xasprintf("%s/%s", src_dir, name);
        if (exists(cand)) { free(name); return cand; }
        free(cand);
    }
    for (int i = 0; i < g_nincludes; i++) {
        cand = xasprintf("%s/%s", g_includes[i], name);
        if (exists(cand)) { free(name); return cand; }
        free(cand);
    }
    if (src_dir) {
        cand = xasprintf("%s/%s", src_dir, name);
        if (exists(cand)) { free(name); return cand; }
        free(cand);
    }
    free(name);
    return NULL;
}

static void merge_import(Program *dst, Program *src) {
    for (int i = 0; i < src->nfuncs; i++) {
        Func *f = src->funcs[i];
        int exists_already = 0;
        for (int j = 0; j < dst->nfuncs; j++)
            if (strcmp(dst->funcs[j]->name, f->name) == 0) { exists_already = 1; break; }
        if (exists_already) continue;
        f->is_cfunc = 1;
        f->is_declared = 1;
        dst->funcs = (Func **)xrealloc(dst->funcs, (size_t)(dst->nfuncs + 1) * sizeof(Func *));
        dst->funcs[dst->nfuncs++] = f;
    }
    for (int i = 0; i < src->nstructs; i++) {
        StructDecl *s = src->structs[i];
        int exists_already = 0;
        for (int j = 0; j < dst->nstructs; j++)
            if (strcmp(dst->structs[j]->name, s->name) == 0) { exists_already = 1; break; }
        if (exists_already) continue;
        s->from_header = 1;
        s->imported = 1;
        dst->structs = (StructDecl **)xrealloc(dst->structs, (size_t)(dst->nstructs + 1) * sizeof(StructDecl *));
        dst->structs[dst->nstructs++] = s;
    }
    for (int i = 0; i < src->nglobals; i++) {
        Stmt *g = src->globals[i];
        int exists_already = 0;
        for (int j = 0; j < dst->nglobals; j++)
            if (strcmp(dst->globals[j]->as.var.name, g->as.var.name) == 0) { exists_already = 1; break; }
        if (exists_already) continue;
        g->as.var.is_c_macro = 1;
        dst->globals = (Stmt **)xrealloc(dst->globals, (size_t)(dst->nglobals + 1) * sizeof(Stmt *));
        dst->globals[dst->nglobals++] = g;
    }
}

static void import_one_header(Program *prog, const char *inc, const char *src_dir,
                              const char *cache, int is_import) {
    const char *builtin = cimport_builtin(inc);
    char *decls = NULL;
    char *path = NULL;
    char *cdir = cache_sub(cache, "cimport");
    if (builtin) {
        decls = xstrdup(builtin);
    } else {
        path = find_header_path(inc, src_dir);
        if (!path) { free(cdir); return; }
        if (is_import) {
            char *dir = dirname_copy(path);
            char *base = base_name(path);
            char *csrc = xasprintf("%s/%s.c", dir, base);
            if (exists(csrc)) program_add_source(prog, csrc);
            free(csrc);
            free(base);
            free(dir);
        }
        char *text = read_file_opt(path);
        if (!text) { free(path); free(cdir); return; }
        unsigned long long h = hash_text(text);
        char *decl_path = xasprintf("%s/%016llx.decl", cdir, h);
        decls = read_file_opt(decl_path);
        if (!decls) {
            decls = cimport_from_source(text);
            FILE *f = fopen(decl_path, "wb");
            if (f) { fputs(decls, f); fclose(f); }
        }
        free(decl_path);
        free(text);
    }
    if (decls && decls[0]) {
        Program *imp = parse_program(decls, path ? path : "<builtin>");
        merge_import(prog, imp);
    }
    free(decls);
    free(path);
    free(cdir);
}

static void import_source_includes(Program *out, const char *src, const char *file,
                                   const char *cache) {
    if (!cache) return;
    char *src_dir = dirname_copy(file);
    const char *p = src;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        int is_import = 0;
        const char *kw = NULL;
        if (strncmp(p, "cimport", 7) == 0 && (p[7] == ' ' || p[7] == '\t')) {
            is_import = 1;
            kw = p + 7;
        } else if (strncmp(p, "cinclude", 8) == 0 && (p[8] == ' ' || p[8] == '\t')) {
            kw = p + 8;
        }
        if (kw) {
            const char *q = kw;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '<') {
                const char *e = strchr(q, '>');
                if (e) {
                    char *inc = xstrndup(q, (size_t)(e - q + 1));
                    import_one_header(out, inc, src_dir, cache, is_import);
                    free(inc);
                }
            } else if (*q == '"') {
                const char *e = strchr(q + 1, '"');
                if (e) {
                    char *inc = xstrndup(q, (size_t)(e - q + 1));
                    import_one_header(out, inc, src_dir, cache, is_import);
                    free(inc);
                }
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    free(src_dir);
}

static void merge_program(Program *dst, Program *src, int is_root) {
    for (int i = 0; i < src->nfuncs; i++) {
        Func *f = src->funcs[i];
        if (!is_root && strcmp(f->name, "main") == 0) continue;
        dst->funcs = (Func **)xrealloc(dst->funcs, (size_t)(dst->nfuncs + 1) * sizeof(Func *));
        dst->funcs[dst->nfuncs++] = f;
    }
    for (int i = 0; i < src->nstructs; i++) {
        StructDecl *s = src->structs[i];
        int dup = 0;
        for (int j = 0; j < dst->nstructs; j++)
            if (strcmp(dst->structs[j]->name, s->name) == 0) dup = 1;
        if (dup) continue;
        dst->structs = (StructDecl **)xrealloc(dst->structs, (size_t)(dst->nstructs + 1) * sizeof(StructDecl *));
        dst->structs[dst->nstructs++] = s;
    }
    for (int i = 0; i < src->nenums; i++) {
        EnumDecl *e = src->enums[i];
        int dup = 0;
        for (int j = 0; j < dst->nenums; j++)
            if (strcmp(dst->enums[j]->name, e->name) == 0) dup = 1;
        if (dup) continue;
        dst->enums = (EnumDecl **)xrealloc(dst->enums, (size_t)(dst->nenums + 1) * sizeof(EnumDecl *));
        dst->enums[dst->nenums++] = e;
    }
    for (int i = 0; i < src->nglobals; i++) {
        Stmt *g = src->globals[i];
        dst->globals = (Stmt **)xrealloc(dst->globals, (size_t)(dst->nglobals + 1) * sizeof(Stmt *));
        dst->globals[dst->nglobals++] = g;
    }
    for (int i = 0; i < src->nc_includes; i++) {
        char *inc = src->c_includes[i];
        int seen = 0;
        for (int j = 0; j < dst->nc_includes; j++)
            if (strcmp(dst->c_includes[j], inc) == 0) seen = 1;
        if (!seen) {
            dst->c_includes = (char **)xrealloc(dst->c_includes, (size_t)(dst->nc_includes + 1) * sizeof(char *));
            dst->c_includes[dst->nc_includes++] = inc;
        }
    }
    for (int i = 0; i < src->nc_links; i++) {
        char *lib = src->c_links[i];
        int seen = 0;
        for (int j = 0; j < dst->nc_links; j++)
            if (strcmp(dst->c_links[j], lib) == 0) seen = 1;
        if (!seen) {
            dst->c_links = (char **)xrealloc(dst->c_links, (size_t)(dst->nc_links + 1) * sizeof(char *));
            dst->c_links[dst->nc_links++] = lib;
        }
    }
    for (int i = 0; i < src->nc_imports; i++)
        program_add_import(dst, src->c_imports[i]);
    for (int i = 0; i < src->nc_sources; i++)
        program_add_source(dst, src->c_sources[i]);
}

static void load_module_file(const char *path, const char *root, Program *out, Visited *visited, int is_root);

static void scan_imports(const char *src, const char *root, Program *out, Visited *visited) {
    const char *p = src;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (p[0] == 'u' && p[1] == 's' && p[2] == 'e' && (p[3] == ' ' || p[3] == '\t')) {
            p += 3;
            while (*p == ' ' || *p == '\t') p++;
            const char *start = p;
            while (*p && *p != '\n' && *p != '\r' && *p != ' ' && *p != '\t' && *p != ';' && *p != '/') p++;
            char *mod = xstrndup(start, (size_t)(p - start));
            if (mod[0] && strncmp(mod, "std.", 4) != 0 && strcmp(mod, "std") != 0) {
                char *mp = resolve_module(root, mod);
                if (exists(mp) && !visited_has(visited, mp))
                    load_module_file(mp, root, out, visited, 0);
                free(mp);
            }
            free(mod);
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

static int exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static void load_module_file(const char *path, const char *root, Program *out, Visited *visited, int is_root) {
    if (getenv("AKT_DEBUG")) fprintf(stderr, "aktc: load %s\n", path);
    if (visited_has(visited, path)) return;
    visited_add(visited, path);
    char *src = read_file(path);
    scan_imports(src, root, out, visited);
    import_source_includes(out, src, path, g_cache_dir);
    Program *p = parse_program(src, path);
    merge_program(out, p, is_root);
    free(src);
}

static Program *load_program(const char *main_file) {
    Program *out = (Program *)xcalloc(1, sizeof(Program));
    out->file = xstrdup(main_file);
    Visited visited = {0};
    char *root = source_root(main_file);
    load_module_file(main_file, root, out, &visited, 1);
    for (int i = 0; i < visited.count; i++) free(visited.items[i]);
    free(visited.items);
    free(root);
    return out;
}

static char *replace_ext(const char *path, const char *ext) {
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bslash = strrchr(path, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    size_t base = (dot && (!slash || dot > slash)) ? (size_t)(dot - path) : strlen(path);
    char *out = (char *)xmalloc(base + strlen(ext) + 1);
    memcpy(out, path, base);
    strcpy(out + base, ext);
    return out;
}

static char *root_dir(void) {
    const char *env = getenv("AKT_ROOT");
    if (env && *env) return xstrdup(env);
#ifdef _WIN32
    char exe[4096];
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
    if (n > 0 && n < sizeof(exe)) {
        char shortbuf[4096];
        if (GetShortPathNameA(exe, shortbuf, (DWORD)sizeof(shortbuf)) > 0)
            strcpy(exe, shortbuf);
        char *slash = strrchr(exe, '\\');
        if (slash) {
            *slash = 0;
            slash = strrchr(exe, '\\');
            if (slash) { *slash = 0; return xstrdup(exe); }
        }
    }
#else
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = 0;
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            slash = strrchr(exe, '/');
            if (slash) { *slash = 0; return xstrdup(exe); }
        }
    }
#endif
    return xstrdup(AKT_ROOT);
}

static char *short_path(const char *path) {
#ifdef _WIN32
    char buf[4096];
    DWORD n = GetShortPathNameA(path, buf, (DWORD)sizeof(buf));
    if (n > 0 && n < sizeof(buf)) return xstrdup(buf);
#endif
    return xstrdup(path);
}

static char *short_path_for_output(const char *path) {
#ifdef _WIN32
    const char *slash = strrchr(path, '/');
    const char *bs = strrchr(path, '\\');
    if (!slash || (bs && bs > slash)) slash = bs;
    if (!slash) return short_path(path);
    char *dir = (char *)xmalloc((size_t)(slash - path) + 1);
    memcpy(dir, path, (size_t)(slash - path));
    dir[slash - path] = 0;
    char *sd = short_path(dir);
    char *out = xasprintf("%s\\%s", sd, slash + 1);
    free(dir);
    free(sd);
    return out;
#else
    return xstrdup(path);
#endif
}

static char *quote(const char *s) {
    size_t n = strlen(s);
    char *out = (char *)xmalloc(n * 2 + 3);
    size_t o = 0;
    out[o++] = '"';
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '"') out[o++] = '\\';
        out[o++] = s[i];
    }
    out[o++] = '"';
    out[o] = 0;
    return out;
}

static int run_cc(const char *gen_c, const char *exe, int release, const char *cc_override, Program *prog) {
    char *root = short_path(root_dir());
    char *inc = xasprintf("%s/runtime/include", root);
    char *rt = xasprintf("%s/runtime/src/akt_runtime.c", root);
    char *gen_short = short_path(gen_c);
    char *exe_short = short_path_for_output(exe);
    const char *cc = cc_override;
    if (!cc || !*cc) {
        const char *env = getenv("AKT_CC");
        cc = (env && *env) ? env : "clang";
    }
    char *qcc = strchr(cc, ' ') ? quote(cc) : xstrdup(cc);
    char *qgen = quote(gen_short);
    char *qrt = quote(rt);
    char *qexe = quote(exe_short);
    char *qinc = quote(inc);
    char *linkflags = xstrdup("");
    char optflags[768];
    snprintf(optflags, sizeof(optflags), "%s%s%s",
             release ? "-O3 -DNDEBUG -flto -fuse-ld=lld -funroll-loops -fstrict-aliasing -fomit-frame-pointer -fno-math-errno" : "-O0 -g",
             (release && !g_target) ? " -march=native -mtune=native" : "",
             (release && !g_shared) ? " -fvisibility=hidden" : "");
    const char *linkopt =
#ifdef _WIN32
        " -Wl,/OPT:REF,/OPT:ICF";
#else
        " -Wl,--gc-sections,--icf=all";
#endif
    char targetflag[512] = "";
    if (g_target) snprintf(targetflag, sizeof(targetflag), " --target=%s", g_target);
    char sysrootflag[4096] = "";
    if (g_sysroot) snprintf(sysrootflag, sizeof(sysrootflag), " --sysroot=\"%s\"", g_sysroot);
    const char *sharedflag = g_shared ? " -shared" : "";
#ifdef _WIN32
    const char *picflag = (g_shared && g_target) ? " -fPIC" : "";
#else
    const char *picflag = g_shared ? " -fPIC" : "";
#endif
    for (int i = 0; i < g_nincludes; i++) {
        char *n = xasprintf("%s -I\"%s\"", linkflags, g_includes[i]);
        free(linkflags); linkflags = n;
    }
    if (g_src_dir) {
        char *n = xasprintf("%s -I\"%s\"", linkflags, g_src_dir);
        free(linkflags); linkflags = n;
    }
    for (int i = 0; i < g_nlibdirs; i++) {
        char *n = xasprintf("%s -L\"%s\"", linkflags, g_libdirs[i]);
        free(linkflags); linkflags = n;
    }
    for (int i = 0; i < g_ndefines; i++) {
        char *n = xasprintf("%s -D%s", linkflags, g_defines[i]);
        free(linkflags); linkflags = n;
    }
    for (int i = 0; i < g_nlibs; i++) {
#ifdef _WIN32
        if (strcmp(g_libs[i], "m") == 0) continue;
#endif
        char *n = xasprintf("%s -l%s", linkflags, g_libs[i]);
        free(linkflags); linkflags = n;
    }
    for (int i = 0; i < prog->nc_links; i++) {
#ifdef _WIN32
        if (strcmp(prog->c_links[i], "m") == 0) continue;
#endif
        char *n = xasprintf("%s -l%s", linkflags, prog->c_links[i]);
        free(linkflags);
        linkflags = n;
    }
    char *extra_src = xstrdup("");
    for (int i = 0; i < prog->nc_sources; i++) {
        char *q = quote(prog->c_sources[i]);
        char *n = xasprintf("%s %s", extra_src, q);
        free(q);
        free(extra_src);
        extra_src = n;
    }
    char *cmd = xasprintf(
        "%s -x c -std=c11 %s -D_CRT_SECURE_NO_WARNINGS -Wno-parentheses-equality -Wno-format-security -Wno-return-type%s%s%s%s %s %s -I%s -I%s %s %s%s%s -o %s",
        qcc, optflags, targetflag, sysrootflag, sharedflag, picflag,
        getenv("AKT_CFLAGS") ? getenv("AKT_CFLAGS") : "", linkopt,
        qinc, qinc, qgen, qrt, linkflags, extra_src, qexe);
#ifdef _WIN32
    char *cmd2 = xasprintf("%s -lmsvcrt", cmd);
    free(cmd);
    cmd = cmd2;
#else
    char *cmd2 = xasprintf("%s -lm", cmd);
    free(cmd);
    cmd = cmd2;
#endif
    int rc = system(cmd);
    if (getenv("AKT_DEBUG")) fprintf(stderr, "aktc: cc command: %s\n", cmd);
    free(root); free(inc); free(rt); free(qcc); free(qgen); free(qrt); free(qexe); free(qinc); free(cmd); free(linkflags); free(extra_src);
    return rc;
}

static int run_exe(const char *exe, int argc, char **argv) {
    (void)argc;
    (void)argv;
    char *q = quote(exe);
    int rc = system(q);
    free(q);
    return rc;
}

static void usage(void) {
    printf("aktc %s - compiler for the akt language\n\n", AKT_VERSION);
    printf("usage:\n");
    printf("  aktc build <file.akt> [-o output] [--release] [--keep-c] [--cc compiler]\n");
    printf("            [--shared] [--target triple] [--sysroot dir]\n");
    printf("  aktc run <file.akt> [--release] [--cc compiler]\n");
    printf("  aktc check <file.akt>\n");
    printf("  aktc --version\n");
}

static int cc_resolve(const char *cc_override, char **out) {
    const char *cc = cc_override;
    if (!cc || !*cc) {
        const char *env = getenv("AKT_CC");
        cc = (env && *env) ? env : "clang";
    }
    *out = strchr(cc, ' ') ? quote(cc) : xstrdup(cc);
    return 1;
}

static int has_word(const char *data, const char *word) {
    size_t n = strlen(word);
    const char *p = data;
    while ((p = strstr(p, word)) != NULL) {
        int left = (p == data) || !(isalnum((unsigned char)p[-1]) || p[-1] == '_');
        int right = !(isalnum((unsigned char)p[n]) || p[n] == '_');
        if (left && right) return 1;
        p += n;
    }
    return 0;
}

static void probe_c_declarations(Program *prog, const char *input, const char *cc_override,
                                 const char *build_dir) {
    if (prog->nc_includes == 0) return;
    if (prog->nstructs == 0 && prog->nfuncs == 0) return;
    char *base = base_name(input);
    char *probe = xasprintf("%s/%s.probe.c", build_dir, base);
    char *out = xasprintf("%s/%s.probe.i", build_dir, base);
    free(base);
    FILE *f = fopen(probe, "wb");
    if (!f) { free(probe); free(out); return; }
    for (int i = 0; i < prog->nc_includes; i++)
        fprintf(f, "#include %s\n", prog->c_includes[i]);
    fprintf(f, "\n");
    fclose(f);
    char *qcc = NULL;
    cc_resolve(cc_override, &qcc);
    char incflags[4096] = "";
    for (int i = 0; i < g_nincludes && i < 32; i++) {
        char piece[256];
        snprintf(piece, sizeof(piece), " -I\"%s\"", g_includes[i]);
        strncat(incflags, piece, sizeof(incflags) - strlen(incflags) - 1);
    }
    for (int i = 0; i < g_ndefines && i < 32; i++) {
        char piece[256];
        snprintf(piece, sizeof(piece), " -D%s", g_defines[i]);
        strncat(incflags, piece, sizeof(incflags) - strlen(incflags) - 1);
    }
    if (g_src_dir) {
        char piece[512];
        snprintf(piece, sizeof(piece), " -I\"%s\"", g_src_dir);
        strncat(incflags, piece, sizeof(incflags) - strlen(incflags) - 1);
    }
#ifdef _WIN32
    const char *null = ">nul 2>&1";
#else
    const char *null = ">/dev/null 2>&1";
#endif
    char *cmd = xasprintf("%s -E -x c -std=c11 -D_CRT_SECURE_NO_WARNINGS%s -o \"%s\" \"%s\" %s",
                          qcc, incflags, out, probe, null);
    if (system(cmd) == 0) {
        char *data = read_file_opt(out);
        if (data) {
            for (int i = 0; i < prog->nstructs; i++) {
                StructDecl *s = prog->structs[i];
                if ((s->is_cstruct || s->is_cunion) && has_word(data, s->name))
                    s->from_header = 1;
            }
            for (int i = 0; i < prog->nfuncs; i++) {
                Func *fn = prog->funcs[i];
                if (fn->is_cfunc && !fn->owner && has_word(data, fn->name))
                    fn->is_declared = 1;
            }
            free(data);
        }
    }
    free(cmd);
    free(qcc);
    free(probe);
    free(out);
}

static int compile_one(const char *input, const char *output, int release, int keep_c,
                       int run_after, const char *cc) {
    char *input_short = short_path(input);
    (void)keep_c;
    char *cache = cache_root(input_short);
    char *build = cache_sub(cache, "build");
    char *bindir = cache_sub(cache, "bin");
    g_src_dir = dirname_copy(input_short);
    g_cache_dir = cache;
    g_cc_override = cc;
    Program *prog = load_program(input_short);
    check_program(prog);
    probe_c_declarations(prog, input_short, cc, build);
    char *base = base_name(input);
    char *gen_c = xasprintf("%s/%s.gen.c", build, base);
    generate_c(prog, gen_c, input);
    char *exe = output ? xstrdup(output)
                       : xasprintf("%s/%s%s", bindir, base, g_shared ? ".so" : ".exe");
    int rc = run_cc(gen_c, exe, release, cc, prog);
    if (rc != 0) {
        fprintf(stderr, "aktc: C compiler failed for %s\n", input);
        free(gen_c); free(exe); free(base); free(input_short);
        free(build); free(bindir); free(cache); free(g_src_dir);
        return 1;
    }
    if (run_after) {
        rc = run_exe(short_path(exe), 0, NULL);
    }
    if (getenv("AKT_VERBOSE"))
        fprintf(stderr, "aktc: cache %s\n", cache);
    free(gen_c); free(exe); free(base); free(input_short);
    free(build); free(bindir); free(cache); free(g_src_dir);
    g_src_dir = NULL;
    return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("aktc %s\n", AKT_VERSION);
        return 0;
    }
    const char *cmd = argv[1];
    const char *input = NULL;
    const char *output = NULL;
    const char *cc = NULL;
    int release = 0, keep_c = 0, run_after = 0, check_only = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) output = argv[++i];
        else if (strcmp(argv[i], "--release") == 0) release = 1;
        else if (strcmp(argv[i], "--fast") == 0) release = 1;
        else if (strcmp(argv[i], "--shared") == 0) g_shared = 1;
        else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) g_target = argv[++i];
        else if (strcmp(argv[i], "--sysroot") == 0 && i + 1 < argc) g_sysroot = argv[++i];
        else if (strcmp(argv[i], "--keep-c") == 0) keep_c = 1;
        else if (strcmp(argv[i], "--cc") == 0 && i + 1 < argc) cc = argv[++i];
        else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc) add_flag(g_includes, &g_nincludes, argv[++i]);
        else if (strcmp(argv[i], "-L") == 0 && i + 1 < argc) add_flag(g_libdirs, &g_nlibdirs, argv[++i]);
        else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) add_flag(g_libs, &g_nlibs, argv[++i]);
        else if (strcmp(argv[i], "-D") == 0 && i + 1 < argc) add_flag(g_defines, &g_ndefines, argv[++i]);
        else if (argv[i][0] == '-' && argv[i][1] == 'I' && argv[i][2]) add_flag(g_includes, &g_nincludes, argv[i] + 2);
        else if (argv[i][0] == '-' && argv[i][1] == 'L' && argv[i][2]) add_flag(g_libdirs, &g_nlibdirs, argv[i] + 2);
        else if (argv[i][0] == '-' && argv[i][1] == 'l' && argv[i][2]) add_flag(g_libs, &g_nlibs, argv[i] + 2);
        else if (argv[i][0] == '-' && argv[i][1] == 'D' && argv[i][2]) add_flag(g_defines, &g_ndefines, argv[i] + 2);
        else if (argv[i][0] == '-') {
            fprintf(stderr, "aktc: unknown option '%s'\n", argv[i]);
            return 2;
        } else if (!input) input = argv[i];
    }
    if (strcmp(cmd, "build") == 0) {
        if (!input) { usage(); return 2; }
    } else if (strcmp(cmd, "run") == 0) {
        if (!input) { usage(); return 2; }
        run_after = 1;
    } else if (strcmp(cmd, "check") == 0) {
        if (!input) { usage(); return 2; }
        check_only = 1;
    } else {
        input = cmd;
    }
    if (check_only) {
        Program *prog = load_program(input);
        check_program(prog);
        printf("%s: ok\n", input);
        return 0;
    }
    return compile_one(input, output, release, keep_c, run_after, cc);
}
