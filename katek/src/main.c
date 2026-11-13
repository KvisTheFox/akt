#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(p) _mkdir(p)
#else
#include <dirent.h>
#include <unistd.h>
#define MKDIR(p) mkdir((p), 0755)
#endif

#ifndef AKT_ROOT
#define AKT_ROOT "."
#endif

#define KATEK_VERSION "0.9.0"

static char *fmt(const char *f, ...);

static char *g_tool_root = NULL;

static char *exe_root(void) {
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
            if (slash) { *slash = 0; return fmt("%s", exe); }
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
            if (slash) { *slash = 0; return fmt("%s", exe); }
        }
    }
#endif
    return NULL;
}

static char *fmt(const char *f, ...) {
    va_list ap, ap2;
    va_start(ap, f);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, f, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return NULL; }
    char *p = (char *)malloc((size_t)n + 1);
    vsnprintf(p, (size_t)n + 1, f, ap2);
    va_end(ap2);
    return p;
}

static int exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static char *read_small(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *p = (char *)malloc((size_t)n + 1);
    size_t got = fread(p, 1, (size_t)n, f);
    fclose(f);
    p[got] = 0;
    return p;
}

static int write_file(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fputs(data, f);
    fclose(f);
    return 1;
}

static char *dirname_of(const char *path) {
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bs = strrchr(path, '\\');
    if (!slash || (bs && bs > slash)) slash = bs;
#endif
    if (!slash) return fmt(".");
    return fmt("%.*s", (int)(slash - path), path);
}

static char *join_path(const char *a, const char *b) {
    if (!a || !*a) return fmt("%s", b);
    size_t n = strlen(a);
    if (a[n - 1] == '/' || a[n - 1] == '\\') return fmt("%s%s", a, b);
#ifdef _WIN32
    return fmt("%s\\%s", a, b);
#else
    return fmt("%s/%s", a, b);
#endif
}

static char *find_project_root(void) {
    char cwd[4096];
#ifdef _WIN32
    if (!_getcwd(cwd, sizeof(cwd))) return fmt(".");
#else
    if (!getcwd(cwd, sizeof(cwd))) return fmt(".");
#endif
    char *cur = fmt("%s", cwd);
    for (;;) {
        char *mf = join_path(cur, "katek.toml");
        if (exists(mf)) { free(mf); return cur; }
        free(mf);
        char *src = join_path(cur, "src");
        char *main = join_path(src, "main.akt");
        int has = exists(main);
        free(src); free(main);
        if (has) return cur;
        char *up = dirname_of(cur);
        if (strcmp(up, cur) == 0) { free(up); return cur; }
        free(cur);
        cur = up;
    }
}

static void cmd_new(const char *name, int bare, int lib) {
    if (!name || !*name) {
        fprintf(stderr, "katek new: project name required\n");
        exit(2);
    }
    if (exists(name)) {
        fprintf(stderr, "katek new: '%s' already exists\n", name);
        exit(1);
    }
    if (MKDIR(name) != 0) {
        fprintf(stderr, "katek new: cannot create '%s'\n", name);
        exit(1);
    }
    char *src = join_path(name, "src");
    char *tests = join_path(name, "tests");
    MKDIR(src);
    MKDIR(tests);
    if (!bare) {
        char *mf = join_path(name, "katek.toml");
        char *manifest = lib ? fmt(
            "[package]\nname = \"%s\"\nversion = \"0.9.0\"\nedition = \"2026\"\n\n"
            "[targets.lib]\nkind = \"library\"\nsrc = \"src/lib.akt\"\n", name) : fmt(
            "[package]\nname = \"%s\"\nversion = \"0.9.0\"\nedition = \"2026\"\n\n"
            "[build]\nbackend = \"internal\"\n", name);
        write_file(mf, manifest);
        free(mf); free(manifest);
    }
    if (lib) {
        char *l = join_path(src, "lib.akt");
        write_file(l,
            "pub fn add(int a, int b) int {\n"
            "    return a + b\n"
            "}\n");
        free(l);
    } else {
        char *main = join_path(src, "main.akt");
        write_file(main,
            "fn main() int {\n"
            "    println(\"Hello from akt!\")\n"
            "    return 0\n"
            "}\n");
        free(main);
    }
    free(src); free(tests);
    printf("created %s\n", name);
    if (lib) printf("  cd %s\n  katek build --shared\n", name);
    else printf("  cd %s\n  katek run\n", name);
}

static int is_ndk_dir(const char *ndk) {
#ifdef _WIN32
    char *p = fmt("%s\\toolchains\\llvm\\prebuilt\\windows-x86_64\\bin\\clang.exe", ndk);
#elif defined(__APPLE__)
    char *p = fmt("%s/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang", ndk);
#else
    char *p = fmt("%s/toolchains/llvm/prebuilt/linux-x86_64/bin/clang", ndk);
#endif
    int ok = exists(p);
    free(p);
    return ok;
}

static char *find_ndk(void) {
    const char *env = getenv("ANDROID_NDK_HOME");
    if (!env || !*env) env = getenv("ANDROID_NDK_ROOT");
    if (env && *env && is_ndk_dir(env)) return fmt("%s", env);
#ifdef _WIN32
    const char *local = getenv("LOCALAPPDATA");
    if (local) {
        char *base = fmt("%s\\Android\\Sdk\\ndk", local);
        char *pat = fmt("%s\\*", base);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                char *cand = fmt("%s\\%s", base, fd.cFileName);
                if (is_ndk_dir(cand)) { FindClose(h); free(pat); free(base); return cand; }
                free(cand);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        free(pat); free(base);
    }
#endif
    const char *fallbacks[] = {"C:\\Android\\ndk", "C:\\Android\\Sdk\\ndk", NULL};
    for (int i = 0; fallbacks[i]; i++)
        if (is_ndk_dir(fallbacks[i])) return fmt("%s", fallbacks[i]);
    return NULL;
}

static const char *android_triple(const char *target) {
    if (strcmp(target, "android-arm64") == 0) return "aarch64-linux-android21";
    if (strcmp(target, "android-arm") == 0) return "armv7a-linux-androideabi21";
    if (strcmp(target, "android-x86_64") == 0) return "x86_64-linux-android21";
    if (strcmp(target, "android-x86") == 0) return "i686-linux-android21";
    return target;
}

static char *find_aktc(void) {
    const char *root = getenv("AKT_ROOT");
    if (!root || !*root) root = g_tool_root;
    if (getenv("AKT_DEBUG")) fprintf(stderr, "katek: tool root = %s\n", root ? root : "(null)");
    if (root && *root) {
        char *p = join_path(root, "bin/aktc.exe");
        if (exists(p)) return p;
        free(p);
        p = join_path(root, "bin/aktc");
        if (exists(p)) return p;
        free(p);
    }
    return fmt("aktc");
}

static char *find_katek(void) {
    const char *root = getenv("AKT_ROOT");
    if (!root || !*root) root = g_tool_root;
    if (root && *root) {
        char *p = join_path(root, "bin/katek.exe");
        if (exists(p)) return p;
        free(p);
        p = join_path(root, "bin/katek");
        if (exists(p)) return p;
        free(p);
    }
    return fmt("katek");
}

static char *arg_quote(const char *s) {
    if (strchr(s, ' ') || strchr(s, '\t')) return fmt("\"%s\"", s);
    return fmt("%s", s);
}

static int run_cmd(const char *cmd) {
    if (getenv("AKT_DEBUG")) fprintf(stderr, "katek: %s\n", cmd);
    int rc = system(cmd);
    return rc;
}

static char *appendf(char *dst, const char *f, ...) {
    va_list ap, ap2;
    va_start(ap, f);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, f, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return dst; }
    size_t old = dst ? strlen(dst) : 0;
    char *p = (char *)realloc(dst, old + (size_t)n + 1);
    vsnprintf(p + old, (size_t)n + 1, f, ap2);
    va_end(ap2);
    return p;
}

static char *trim(char *s) {    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
    *e = 0;
    return s;
}

static int dir_exists(const char *path) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static int list_subdirs(const char *path, char ***out) {
    *out = NULL;
    int n = 0;
    char **names = NULL;
#ifdef _WIN32
    char pat[4096];
    snprintf(pat, sizeof(pat), "%s/*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        names = (char **)realloc(names, (size_t)(n + 1) * sizeof(char *));
        names[n++] = strdup(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char *full = fmt("%s/%s", path, e->d_name);
        int isdir = dir_exists(full);
        free(full);
        if (!isdir) continue;
        names = (char **)realloc(names, (size_t)(n + 1) * sizeof(char *));
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
#endif
    *out = names;
    return n;
}

static char *xrepo_package_root(void) {
    const char *local = getenv("LOCALAPPDATA");
    if (local) {
        char *p = fmt("%s/.xmake/packages", local);
        if (dir_exists(p)) return p;
        free(p);
    }
    const char *home = getenv("USERPROFILE");
    if (!home) home = getenv("HOME");
    if (!home) return NULL;
    char *p = fmt("%s/.xmake/packages", home);
    if (dir_exists(p)) return p;
    free(p);
    return NULL;
}

static void xrepo_add_libs(char **flags, const char *libdir) {
#ifdef _WIN32
    char pat[4096];
    snprintf(pat, sizeof(pat), "%s/*", libdir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char *dot = strrchr(fd.cFileName, '.');
        if (!dot) continue;
        if (strcmp(dot, ".lib") != 0 && strcmp(dot, ".a") != 0) continue;
        char base[512];
        snprintf(base, sizeof(base), "%s", fd.cFileName);
        base[dot - fd.cFileName] = 0;
        char *name = base;
        if (strncmp(name, "lib", 3) == 0) name += 3;
        *flags = appendf(*flags, " -l%s", name);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(libdir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot) continue;
        if (strcmp(dot, ".a") != 0 && strcmp(dot, ".so") != 0) continue;
        char base[512];
        snprintf(base, sizeof(base), "%s", e->d_name);
        base[dot - e->d_name] = 0;
        char *name = base;
        if (strncmp(name, "lib", 3) == 0) name += 3;
        if (strchr(name, '.')) continue;
        *flags = appendf(*flags, " -l%s", name);
    }
    closedir(d);
#endif
}

static void xrepo_add_flags(char **flags, const char *name, const char *version) {
    char *root = xrepo_package_root();
    if (!root || !name || !name[0]) { free(root); return; }
    char letter[2];
    letter[0] = (char)tolower((unsigned char)name[0]);
    letter[1] = 0;
    char *pkg = fmt("%s/%s/%s", root, letter, name);
    char **vers = NULL;
    int nv = list_subdirs(pkg, &vers);
    int done = 0;
    for (int i = nv - 1; i >= 0 && !done; i--) {
        if (version && *version && strncmp(vers[i], version, strlen(version)) != 0) continue;
        char *vdir = fmt("%s/%s", pkg, vers[i]);
        char **hashes = NULL;
        int nh = list_subdirs(vdir, &hashes);
        for (int j = nh - 1; j >= 0 && !done; j--) {
            char *hdir = fmt("%s/%s", vdir, hashes[j]);
            char *inc = fmt("%s/include", hdir);
            char *lib = fmt("%s/lib", hdir);
            if (dir_exists(inc) || dir_exists(lib)) {
                if (dir_exists(inc)) *flags = appendf(*flags, " -I\"%s\"", inc);
                if (dir_exists(lib)) {
                    *flags = appendf(*flags, " -L\"%s\"", lib);
                    xrepo_add_libs(flags, lib);
                }
                done = 1;
            }
            free(inc);
            free(lib);
            free(hdir);
            free(hashes[j]);
        }
        free(hashes);
        free(vdir);
        free(vers[i]);
    }
    free(vers);
    free(pkg);
    free(root);
}

static char *manifest_flags(const char *root) {
    char *mf = join_path(root, "katek.toml");
    FILE *f = fopen(mf, "rb");
    free(mf);
    if (!f) return NULL;
    char line[1024];
    int section = 0;
    char *flags = NULL;
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (!*s || *s == '#') continue;
        if (s[0] == '[') {
            if (strncmp(s, "[native.dependencies]", 21) == 0) section = 2;
            else if (strncmp(s, "[native]", 8) == 0) section = 1;
            else if (strncmp(s, "[dependencies]", 14) == 0) section = 3;
            else section = 0;
            continue;
        }
        if (section == 1) {
            char *eq = strchr(s, '=');
            if (!eq) continue;
            *eq = 0;
            char *key = trim(s);
            char *val = trim(eq + 1);
            const char *prefix = NULL;
            if (strcmp(key, "include") == 0) prefix = "-I";
            else if (strcmp(key, "lib") == 0) prefix = "-L";
            else if (strcmp(key, "link") == 0) prefix = "-l";
            else if (strcmp(key, "defines") == 0) prefix = "-D";
            if (!prefix) continue;
            char *p = val;
            while ((p = strchr(p, '"')) != NULL) {
                char *e = strchr(p + 1, '"');
                if (!e) break;
                *e = 0;
                if (prefix[1] == 'I' || prefix[1] == 'L')
                    flags = appendf(flags, " %s \"%s\"", prefix, p + 1);
                else
                    flags = appendf(flags, " %s%s", prefix, p + 1);
                p = e + 1;
            }
        } else if (section == 2 || section == 3) {
            char *eq = strchr(s, '=');
            if (!eq) continue;
            *eq = 0;
            char *name = trim(s);
            char *body = eq + 1;
            while (*body == ' ' || *body == '\t') body++;
            if (section == 3 && body[0] == '"') {
                char *q2 = strchr(body + 1, '"');
                if (q2) {
                    *q2 = 0;
                    xrepo_add_flags(&flags, name, body + 1);
                    *q2 = '"';
                }
                continue;
            }
            char *prov = strstr(body, "provider");
            if (prov && strstr(prov, "\"xrepo\"")) {
                char ver[64] = "";
                char *v = strstr(body, "version");
                if (v) {
                    char *q1 = strchr(v, '"');
                    char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
                    if (q1 && q2) {
                        *q2 = 0;
                        snprintf(ver, sizeof(ver), "%s", q1 + 1);
                        *q2 = '"';
                    }
                }
                xrepo_add_flags(&flags, name, ver);
            }
            if (prov && strstr(prov, "\"system\""))
                flags = appendf(flags, " -l%s", name);
            else if (prov && strstr(prov, "\"pkg-config\"")) {
                char *pkg = strstr(body, "package");
                if (pkg) {
                    char *q1 = strchr(pkg, '"');
                    char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
                    if (q1 && q2) {
                        *q2 = 0;
                        char *cmd = fmt("pkg-config --cflags --libs \"%s\" 2>nul", q1 + 1);
#ifdef _WIN32
                        FILE *pp = _popen(cmd, "r");
#else
                        FILE *pp = popen(cmd, "r");
#endif
                        if (pp) {
                            char buf[2048];
                            while (fgets(buf, sizeof(buf), pp)) flags = appendf(flags, " %s", trim(buf));
#ifdef _WIN32
                            _pclose(pp);
#else
                            pclose(pp);
#endif
                        }
                        free(cmd);
                    }
                }
            }
        }
    }
    fclose(f);
    return flags;
}

static char *manifest_value(const char *root, const char *key, const char *def) {
    char *mf = join_path(root, "katek.toml");
    char *data = read_small(mf);
    free(mf);
    if (!data) return fmt("%s", def);
    char *out = NULL;
    char *p = data;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        char *s = trim(line);
        if (strncmp(s, key, strlen(key)) == 0) {
            char *eq = strchr(s, '=');
            if (eq) {
                char *q1 = strchr(eq, '"');
                char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
                if (q1 && q2) { *q2 = 0; out = fmt("%s", q1 + 1); break; }
            }
        }
    }
    free(data);
    return out ? out : fmt("%s", def);
}

static int cmd_lock(const char *root) {
    char *name = manifest_value(root, "name", "app");
    char *version = manifest_value(root, "version", "1.0.0");
    char *path = join_path(root, "katek.lock");
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "katek lock: cannot write %s\n", path);
        free(name); free(version); free(path);
        return 1;
    }
    fprintf(f, "version = 2\n\n[[package]]\nname = \"%s\"\nversion = \"%s\"\nkind = \"akt\"\n",
            name, version);
    fclose(f);
    printf("wrote %s\n", path);
    free(name); free(version); free(path);
    return 0;
}

static int cmd_build(const char *root, int release, int run_after, const char *target_name, int shared) {
    char *src = join_path(root, "src");
    char *main = join_path(src, "main.akt");
    if (!exists(main)) {
        free(main);
        main = join_path(src, "lib.akt");
        shared = 1;
    }
    if (!exists(main)) {
        fprintf(stderr, "katek: no src/main.akt or src/lib.akt in '%s'\n", root);
        free(src); free(main);
        return 1;
    }
    char *target = join_path(root, "target");
    MKDIR(target);
    char *out = NULL;
    char *cc = NULL, *sysroot = NULL, *triple = NULL;
    char *ndk = NULL;
    if (target_name && strncmp(target_name, "android", 7) == 0) {
        ndk = find_ndk();
        if (!ndk) {
            fprintf(stderr, "katek: Android NDK not found.\n");
            fprintf(stderr, "  set ANDROID_NDK_HOME or install NDK via Android Studio.\n");
            free(src); free(main); free(target);
            return 1;
        }
#ifdef _WIN32
        const char *prebuilt = "windows-x86_64";
        const char *clang_name = "clang.exe";
#elif defined(__APPLE__)
        const char *prebuilt = "darwin-x86_64";
        const char *clang_name = "clang";
#else
        const char *prebuilt = "linux-x86_64";
        const char *clang_name = "clang";
#endif
        char *pre = fmt("%s/toolchains/llvm/prebuilt/%s", ndk, prebuilt);
        cc = fmt("%s/bin/%s", pre, clang_name);
        sysroot = fmt("%s/sysroot", pre);
        triple = fmt("%s", android_triple(target_name));
        shared = 1;
        out = join_path(target, "libapp.so");
        free(pre);
    } else {
        if (target_name) triple = fmt("%s", target_name);
#ifdef _WIN32
        out = join_path(target, shared ? "app.dll" : "app.exe");
#else
        out = join_path(target, shared ? "libapp.so" : "app");
#endif
    }
    char *aktc = find_aktc();
    char *aktc_a = arg_quote(aktc);

    /* optional build script: build.akt in the project root */
    char *script = join_path(root, "build.akt");
    if (exists(script)) {
        printf("running build script: %s\n", script);
        char *scmd = fmt("%s run \"%s\"", aktc_a, script);
        if (run_cmd(scmd) != 0) {
            fprintf(stderr, "katek: build script failed\n");
            free(scmd); free(script); free(aktc_a); free(aktc);
            free(src); free(main); free(target); free(out);
            free(cc); free(sysroot); free(triple); free(ndk);
            return 1;
        }
        free(scmd);
    }
    free(script);

    /* optional pre-build command from [build] pre = "..." */
    char *pre = manifest_value(root, "pre", "");
    if (pre && pre[0]) {
        printf("pre-build: %s\n", pre);
        if (run_cmd(pre) != 0) {
            fprintf(stderr, "katek: pre-build command failed\n");
            free(pre); free(aktc_a); free(aktc);
            free(src); free(main); free(target); free(out);
            free(cc); free(sysroot); free(triple); free(ndk);
            return 1;
        }
    }
    free(pre);

    char *flags = manifest_flags(root);
    char *extra_file = join_path(root, ".kache/extra.flags");
    char *extra = read_small(extra_file);
    free(extra_file);
    if (extra && extra[0]) {
        char *all = fmt("%s %s", flags ? flags : "", extra);
        free(flags);
        flags = all;
    }
    free(extra);
    char *tflag = triple ? fmt(" --target %s", triple) : fmt("");
    char *sflag = sysroot ? fmt(" --sysroot \"%s\"", sysroot) : fmt("");
    char *cflag = cc ? fmt(" --cc \"%s\"", cc) : fmt("");
    char *shflag = shared ? fmt(" --shared") : fmt("");
    char *cmd = fmt("%s build \"%s\" -o \"%s\"%s%s%s%s%s%s", aktc_a, main, out,
                    release ? " --release" : "", shflag, tflag, sflag, cflag,
                    flags ? flags : "");
    int rc = run_cmd(cmd);
    free(cmd); free(aktc_a); free(flags); free(tflag); free(sflag); free(cflag); free(shflag);
    if (rc == 0) {
        char *post = manifest_value(root, "post", "");
        if (post && post[0]) {
            printf("post-build: %s\n", post);
            run_cmd(post);
        }
        free(post);
    }
    if (rc == 0 && run_after && !shared) {
        char *q = arg_quote(out);
        rc = run_cmd(q);
        free(q);
    }
    if (rc == 0 && shared) printf("built shared library: %s\n", out);
    free(src); free(main); free(target); free(out); free(aktc);
    free(cc); free(sysroot); free(triple); free(ndk);
    return rc == 0 ? 0 : 1;
}

static int cmd_test(const char *root) {
    char *dir = join_path(root, "tests");
    if (!exists(dir)) {
        printf("no tests directory\n");
        free(dir);
        return 0;
    }
    int failed = 0, total = 0;
    char *tdir = join_path(root, ".kache/test");
    MKDIR(tdir);
#ifdef _WIN32
    char *pat = join_path(dir, "*.akt");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    free(pat);
    if (h == INVALID_HANDLE_VALUE) {
        printf("no tests found\n");
        free(dir);
        return 0;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        total++;
        char *file = join_path(dir, fd.cFileName);
        char *out = join_path(tdir, "test_app.exe");
        char *aktc = find_aktc();
        char *aktc_a = arg_quote(aktc);
        char *cmd = fmt("%s build \"%s\" -o \"%s\" --keep-c", aktc_a, file, out);
        int rc = run_cmd(cmd);
        free(cmd); free(aktc_a);
        if (rc == 0) {
            char *q = arg_quote(out);
            rc = run_cmd(q);
            free(q);
        }
        printf("test %s ... %s\n", fd.cFileName, rc == 0 ? "ok" : "FAILED");
        if (rc != 0) failed++;
        free(file); free(out); free(aktc);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) { printf("no tests found\n"); free(dir); return 0; }
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n < 4 || strcmp(e->d_name + n - 4, ".akt") != 0) continue;
        total++;
        char *file = join_path(dir, e->d_name);
        char *out = join_path(tdir, "test_app");
        char *aktc = find_aktc();
        char *aktc_a = arg_quote(aktc);
        char *cmd = fmt("%s build \"%s\" -o \"%s\" --keep-c", aktc_a, file, out);
        int rc = run_cmd(cmd);
        free(cmd); free(aktc_a);
        if (rc == 0) {
            char *q = arg_quote(out);
            rc = run_cmd(q);
            free(q);
        }
        printf("test %s ... %s\n", e->d_name, rc == 0 ? "ok" : "FAILED");
        if (rc != 0) failed++;
        free(file); free(out); free(aktc);
    }
    closedir(d);
#endif
    printf("tests: %d passed, %d failed\n", total - failed, failed);
    free(tdir);
    free(dir);
    return failed ? 1 : 0;
}

typedef struct { char type[32]; char name[64]; } JniParam;
typedef struct { char name[64]; char ret[32]; JniParam params[16]; int n; } JniFn;

static const char *jni_jtype(const char *t) {
    if (!strcmp(t, "int") || !strcmp(t, "i32")) return "jint";
    if (!strcmp(t, "i64")) return "jlong";
    if (!strcmp(t, "f32")) return "jfloat";
    if (!strcmp(t, "f64")) return "jdouble";
    if (!strcmp(t, "bool")) return "jboolean";
    if (!strcmp(t, "string")) return "jstring";
    if (!strcmp(t, "*void")) return "jobject";
    if (!strcmp(t, "[]i32")) return "jintArray";
    if (!strcmp(t, "[]f64")) return "jdoubleArray";
    if (!strcmp(t, "void")) return "void";
    return NULL;
}
static const char *jni_ctype(const char *t) {
    if (!strcmp(t, "string")) return "AktString";
    if (!strcmp(t, "*void")) return "void*";
    if (!strcmp(t, "[]i32")) return "AktVec";
    if (!strcmp(t, "[]f64")) return "AktVec";
    if (!strcmp(t, "int") || !strcmp(t, "i32")) return "int";
    if (!strcmp(t, "i64")) return "int64_t";
    if (!strcmp(t, "f32")) return "float";
    if (!strcmp(t, "f64")) return "double";
    if (!strcmp(t, "bool")) return "bool";
    if (!strcmp(t, "void")) return "void";
    return NULL;
}

static char *find_java_home(void) {
    const char *env = getenv("JAVA_HOME");
    if (env && *env) return fmt("%s", env);
#ifdef _WIN32
    FILE *p = _popen("where javac 2>nul", "r");
    if (p) {
        char line[1024];
        if (fgets(line, sizeof(line), p)) {
            char *s = trim(line);
            char *bin = strstr(s, "\\bin\\javac");
            if (bin) {
                *bin = 0;
                _pclose(p);
                return fmt("%s", s);
            }
        }
        _pclose(p);
    }
#endif
    return NULL;
}

static char *akt_gen_path(const char *file) {
    size_t n = strlen(file);
    if (n > 4 && strcmp(file + n - 4, ".akt") == 0)
        return fmt("%.*s.gen.c", (int)(n - 4), file);
    return fmt("%s.gen.c", file);
}

static int cmd_jni(const char *root, const char *package, const char *klass, int no_build) {
    if (!package) package = "com.example";
    if (!klass) klass = "Main";
    char *src = join_path(root, "src");
    char *file = join_path(src, "lib.akt");
    if (!exists(file)) { free(file); file = join_path(src, "main.akt"); }
    if (!exists(file)) { fprintf(stderr, "katek jni: no src/lib.akt or src/main.akt\n"); free(src); return 1; }
    FILE *in = fopen(file, "rb");
    if (!in) { free(src); free(file); return 1; }
    JniFn fns[64];
    int nf = 0;
    char line[1024];
    while (fgets(line, sizeof(line), in) && nf < 64) {
        char *s = trim(line);
        if (!strstr(s, "pub ") || !strstr(s, "fn ")) continue;
        char *fn = strstr(s, "fn ") + 3;
        while (*fn == ' ') fn++;
        char *lp = strchr(fn, '(');
        if (!lp) continue;
        JniFn f;
        memset(&f, 0, sizeof(f));
        size_t nl = (size_t)(lp - fn);
        if (nl >= sizeof(f.name)) nl = sizeof(f.name) - 1;
        memcpy(f.name, fn, nl); f.name[nl] = 0;
        char *rp = strchr(lp, ')');
        if (!rp) continue;
        char *ret = rp + 1;
        while (*ret == ' ') ret++;
        size_t rl = 0;
        while (ret[rl] && ret[rl] != ' ' && ret[rl] != '{' && ret[rl] != ';' && ret[rl] != '\r' && ret[rl] != '\n') rl++;
        if (rl == 0) strcpy(f.ret, "void");
        else { if (rl >= sizeof(f.ret)) rl = sizeof(f.ret) - 1; memcpy(f.ret, ret, rl); f.ret[rl] = 0; }
        if (!jni_jtype(f.ret) || !jni_ctype(f.ret)) continue;
        char *p = lp + 1;
        while (p < rp && f.n < 16) {
            while (*p == ' ') p++;
            char *e = p;
            while (e < rp && *e != ',') e++;
            char part[128];
            size_t pl = (size_t)(e - p);
            if (pl >= sizeof(part)) pl = sizeof(part) - 1;
            memcpy(part, p, pl); part[pl] = 0;
            char *t = trim(part);
            char *sp = strrchr(t, ' ');
            if (sp) {
                *sp = 0;
                char *ty = trim(t);
                char *nm = trim(sp + 1);
                if (jni_jtype(ty) && jni_ctype(ty)) {
                    snprintf(f.params[f.n].type, sizeof(f.params[f.n].type), "%s", ty);
                    snprintf(f.params[f.n].name, sizeof(f.params[f.n].name), "%s", nm);
                    f.n++;
                }
            }
            p = e + 1;
        }
        fns[nf++] = f;
    }
    fclose(in);
    char *target = join_path(root, "target");
    MKDIR(target);
    char *out = join_path(target, "jni_bindings.c");
    FILE *o = fopen(out, "wb");
    if (!o) { free(src); free(file); free(target); free(out); return 1; }
    fprintf(o, "#include <jni.h>\n#include \"akt_runtime.h\"\n\n");
    for (int i = 0; i < nf; i++) {
        fprintf(o, "%s %s(", jni_ctype(fns[i].ret), fns[i].name);
        for (int p = 0; p < fns[i].n; p++) {
            if (p) fprintf(o, ", ");
            fprintf(o, "%s %s", jni_ctype(fns[i].params[p].type), fns[i].params[p].name);
        }
        if (fns[i].n == 0) fprintf(o, "void");
        fprintf(o, ");\n");
    }
    fprintf(o, "\n");
    char pkg[256];
    snprintf(pkg, sizeof(pkg), "%s", package);
    for (char *p = pkg; *p; p++) if (*p == '.') *p = '_';
    for (int i = 0; i < nf; i++) {
        JniFn *f = &fns[i];
        fprintf(o, "JNIEXPORT %s JNICALL Java_%s_%s_%s(JNIEnv* env, jobject thiz",
                jni_jtype(f->ret), pkg, klass, f->name);
        for (int p = 0; p < f->n; p++)
            fprintf(o, ", %s %s", jni_jtype(f->params[p].type), f->params[p].name);
        fprintf(o, ") {\n    (void)env; (void)thiz;\n");
        for (int p = 0; p < f->n; p++) {
            if (!strcmp(f->params[p].type, "string"))
                fprintf(o, "    const char* %s_c = (*env)->GetStringUTFChars(env, %s, 0);\n"
                           "    AktString %s_akt = akt_str_cstr(%s_c);\n",
                        f->params[p].name, f->params[p].name, f->params[p].name, f->params[p].name);
            else if (!strcmp(f->params[p].type, "[]i32") || !strcmp(f->params[p].type, "[]f64")) {
                const char *jt = !strcmp(f->params[p].type, "[]i32") ? "jint" : "jdouble";
                const char *ct = !strcmp(f->params[p].type, "[]i32") ? "int32_t" : "double";
                fprintf(o, "    jsize %s_n = (*env)->GetArrayLength(env, %s);\n",
                        f->params[p].name, f->params[p].name);
                fprintf(o, "    %s* %s_e = (*env)->%s(env, %s, 0);\n", jt, f->params[p].name,
                        !strcmp(f->params[p].type, "[]i32") ? "GetIntArrayElements" : "GetDoubleArrayElements",
                        f->params[p].name);
                fprintf(o, "    AktVec %s_vec = akt_vec_new(sizeof(%s));\n", f->params[p].name, ct);
                fprintf(o, "    for (int _i = 0; _i < %s_n; _i++) { %s _v = (%s)%s_e[_i]; akt_vec_push(&%s_vec, &_v); }\n",
                        f->params[p].name, ct, ct, f->params[p].name, f->params[p].name);
            }
        }
        fprintf(o, "    ");
        if (strcmp(f->ret, "void") != 0) fprintf(o, "%s _r = ", jni_ctype(f->ret));
        fprintf(o, "%s(", f->name);
        for (int p = 0; p < f->n; p++) {
            if (p) fprintf(o, ", ");
            if (!strcmp(f->params[p].type, "string")) fprintf(o, "%s_akt", f->params[p].name);
            else if (!strcmp(f->params[p].type, "[]i32") || !strcmp(f->params[p].type, "[]f64"))
                fprintf(o, "%s_vec", f->params[p].name);
            else if (!strcmp(f->params[p].type, "*void"))
                fprintf(o, "(void*)%s", f->params[p].name);
            else fprintf(o, "(%s)%s", jni_ctype(f->params[p].type), f->params[p].name);
        }
        fprintf(o, ");\n");
        for (int p = 0; p < f->n; p++) {
            if (!strcmp(f->params[p].type, "string"))
                fprintf(o, "    (*env)->ReleaseStringUTFChars(env, %s, %s_c);\n",
                        f->params[p].name, f->params[p].name);
            else if (!strcmp(f->params[p].type, "[]i32"))
                fprintf(o, "    (*env)->ReleaseIntArrayElements(env, %s, %s_e, 0);\n",
                        f->params[p].name, f->params[p].name);
            else if (!strcmp(f->params[p].type, "[]f64"))
                fprintf(o, "    (*env)->ReleaseDoubleArrayElements(env, %s, %s_e, 0);\n",
                        f->params[p].name, f->params[p].name);
        }
        if (!strcmp(f->ret, "void")) fprintf(o, "    return;\n");
        else if (!strcmp(f->ret, "string"))
            fprintf(o, "    return (*env)->NewStringUTF(env, _r.data);\n");
        else if (!strcmp(f->ret, "[]i32")) {
            fprintf(o, "    jintArray _a = (*env)->NewIntArray(env, (jsize)_r.len);\n");
            fprintf(o, "    for (size_t _i = 0; _i < _r.len; _i++) { jint _v = (jint)*(int32_t*)akt_vec_get(&_r, _i); (*env)->SetIntArrayRegion(env, _a, (jsize)_i, 1, &_v); }\n");
            fprintf(o, "    return _a;\n");
        } else if (!strcmp(f->ret, "[]f64")) {
            fprintf(o, "    jdoubleArray _a = (*env)->NewDoubleArray(env, (jsize)_r.len);\n");
            fprintf(o, "    for (size_t _i = 0; _i < _r.len; _i++) { jdouble _v = (jdouble)*(double*)akt_vec_get(&_r, _i); (*env)->SetDoubleArrayRegion(env, _a, (jsize)_i, 1, &_v); }\n");
            fprintf(o, "    return _a;\n");
        } else if (!strcmp(f->ret, "*void"))
            fprintf(o, "    return (jobject)_r;\n");
        else fprintf(o, "    return (%s)_r;\n", jni_jtype(f->ret));
        fprintf(o, "}\n\n");
    }
    fclose(o);
    printf("wrote %s (%d functions)\n", out, nf);
    if (no_build) { free(src); free(file); free(target); free(out); return 0; }

    char *aktc = find_aktc();
    char *a = arg_quote(aktc);
    char *lib = join_path(target, "libapp.so");
    char *cmd = fmt("%s build \"%s\" --keep-c --shared -o \"%s\"", a, file, lib);
    int rc = run_cmd(cmd);
    free(cmd);
    if (rc != 0) {
        fprintf(stderr, "katek jni: akt compilation failed\n");
        free(a); free(aktc); free(lib); free(src); free(file); free(target); free(out);
        return 1;
    }
    char *gen = akt_gen_path(file);
    char *ndk = find_ndk();
    const char *cc = "clang";
    char *inc1 = NULL, *inc2 = NULL, *outso = NULL, *sysroot = NULL;
    int android = ndk != NULL;
    if (android) {
#ifdef _WIN32
        const char *prebuilt = "windows-x86_64";
        const char *clang_name = "clang.exe";
#elif defined(__APPLE__)
        const char *prebuilt = "darwin-x86_64";
        const char *clang_name = "clang";
#else
        const char *prebuilt = "linux-x86_64";
        const char *clang_name = "clang";
#endif
        char *pre = fmt("%s/toolchains/llvm/prebuilt/%s", ndk, prebuilt);
        cc = fmt("%s/bin/%s", pre, clang_name);
        sysroot = fmt("%s/sysroot", pre);
        inc1 = fmt("%s/usr/include", sysroot);
        inc2 = fmt("%s/usr/include/aarch64-linux-android", sysroot);
        outso = join_path(target, "libapp.so");
        free(pre);
    } else {
        char *jh = find_java_home();
        if (!jh) {
            fprintf(stderr, "katek jni: JAVA_HOME/javac not found; bindings generated only\n");
            free(gen); free(ndk); free(a); free(aktc); free(lib); free(src); free(file); free(target); free(out);
            return 1;
        }
        inc1 = fmt("%s/include", jh);
#ifdef _WIN32
        inc2 = fmt("%s/include/win32", jh);
        outso = join_path(target, "libapp.dll");
#else
        inc2 = fmt("%s/include/linux", jh);
        outso = join_path(target, "libapp.so");
#endif
        free(jh);
    }
    char *rtinc = join_path(root, "runtime/include");
    char *rtsrc = join_path(root, "runtime/src/akt_runtime.c");
    const char *tool_root = (getenv("AKT_ROOT") && *getenv("AKT_ROOT")) ? getenv("AKT_ROOT") : g_tool_root;
    if (tool_root && *tool_root) {
        free(rtinc); free(rtsrc);
        rtinc = join_path(tool_root, "runtime/include");
        rtsrc = join_path(tool_root, "runtime/src/akt_runtime.c");
    }
    char *ccq = arg_quote(cc);
    char *targetflag = android ? fmt(" --target=aarch64-linux-android21 --sysroot=\"%s\"", sysroot) : fmt("");
#ifdef _WIN32
    char *cmd2 = fmt("%s -shared -D_CRT_SECURE_NO_WARNINGS%s -I\"%s\" -I\"%s\" -I\"%s\" \"%s\" \"%s\" \"%s\" -o \"%s\"",
                     ccq, targetflag, rtinc, inc1, inc2, gen, out, rtsrc, outso);
#else
    char *cmd2 = fmt("%s -shared -fPIC -D_CRT_SECURE_NO_WARNINGS%s -I\"%s\" -I\"%s\" -I\"%s\" \"%s\" \"%s\" \"%s\" -o \"%s\"",
                     ccq, targetflag, rtinc, inc1, inc2, gen, out, rtsrc, outso);
#endif
    rc = run_cmd(cmd2);
    if (rc == 0) printf("built JNI library: %s\n", outso);
    else fprintf(stderr, "katek jni: JNI shared library build failed\n");
    free(cmd2); free(targetflag); free(ccq); free(rtinc); free(rtsrc);
    free(inc1); free(inc2); free(sysroot); free(outso); free(gen); free(ndk);
    free(a); free(aktc); free(lib); free(src); free(file); free(target); free(out);
    return rc == 0 ? 0 : 1;
}

static int cmd_c_install(const char *root, int dry) {
    char *mf = join_path(root, "katek.toml");
    char *data = read_small(mf);
    free(mf);
    if (!data) { fprintf(stderr, "katek c install: katek.toml not found\n"); return 1; }
    char *p = data;
    int section = 0, rc = 0;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        char *s = trim(line);
        if (s[0] == '[') { section = strncmp(s, "[native.dependencies]", 21) == 0; continue; }
        if (!section || !*s || *s == '#') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *name = trim(s);
        char *body = eq + 1;
        char provider[64] = "auto";
        char version[64] = "";
        char package[64] = "";
        char *pkgf = strstr(body, "package");
        if (pkgf) {
            char *q1 = strchr(pkgf, '"');
            char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
            if (q1 && q2) { *q2 = 0; snprintf(package, sizeof(package), "%s", q1 + 1); }
        }
        char *ver = strstr(body, "version");
        if (ver) {
            char *q1 = strchr(ver, '"');
            char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
            if (q1 && q2) { *q2 = 0; snprintf(version, sizeof(version), "%s", q1 + 1); }
        }
        char *prov = strstr(body, "provider");
        if (prov) {
            char *q1 = strchr(prov, '"');
            char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
            if (q1 && q2) { *q2 = 0; snprintf(provider, sizeof(provider), "%s", q1 + 1); }
        }
        if (!strcmp(provider, "xrepo") || !strcmp(provider, "auto")) {
            char *cmd = version[0] ? fmt("xrepo install -y \"%s %s\"", name, version)
                                   : fmt("xrepo install -y \"%s\"", name);
            printf("%s %s\n", dry ? "[dry]" : "[xrepo]", cmd);
            if (!dry) rc |= run_cmd(cmd);
            free(cmd);
        } else if (!strcmp(provider, "pkg-config")) {
            char *cmd = fmt("pkg-config --exists \"%s\"", name);
            printf("%s pkg-config %s\n", dry ? "[dry]" : "[check]", name);
            if (!dry && run_cmd(cmd) != 0) { fprintf(stderr, "missing pkg-config package: %s\n", name); rc = 1; }
            free(cmd);
        } else if (!strcmp(provider, "vcpkg")) {
            char *cmd = fmt("vcpkg install %s", name);
            printf("%s %s\n", dry ? "[dry]" : "[vcpkg]", cmd);
            if (!dry) rc |= run_cmd(cmd);
            free(cmd);
        } else if (!strcmp(provider, "conan")) {
            char *cmd = version[0] ? fmt("conan install --requires=%s/%s@", name, version)
                                   : fmt("conan install --requires=%s", name);
            printf("%s %s\n", dry ? "[dry]" : "[conan]", cmd);
            if (!dry) rc |= run_cmd(cmd);
            free(cmd);
        } else if (!strcmp(provider, "system")) {
            printf("[system] %s (no action)\n", name);
        } else if (!strcmp(provider, "cmake")) {
            const char *pkg = package[0] ? package : name;
            char *cmd = fmt("cmake --find-package -DNAME=%s -DCOMPILER_ID=GNU -DLANGUAGE=C -DMODE=EXIST", pkg);
            printf("%s cmake find_package %s\n", dry ? "[dry]" : "[cmake]", pkg);
            if (!dry && run_cmd(cmd) != 0) { fprintf(stderr, "cmake package not found: %s\n", pkg); rc = 1; }
            free(cmd);
        } else {
            printf("[manual] %s provider=%s (not automated)\n", name, provider);
        }
    }
    free(data);
    return rc;
}

static int cmd_c_sync(const char *root) {
    char *mf = join_path(root, "katek.toml");
    char *data = read_small(mf);
    free(mf);
    if (!data) { fprintf(stderr, "katek c sync: katek.toml not found\n"); return 1; }
    char *dir = join_path(root, ".katek");
    MKDIR(dir);
    char *path = join_path(dir, "native.json");
    FILE *f = fopen(path, "wb");
    if (!f) { free(data); free(dir); free(path); return 1; }
    fprintf(f, "{\n  \"dependencies\": [\n");
    char *p = data;
    int section = 0, first = 1;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        char *s = trim(line);
        if (s[0] == '[') { section = strncmp(s, "[native.dependencies]", 21) == 0; continue; }
        if (!section || !*s || *s == '#') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *name = trim(s);
        char *prov = strstr(eq + 1, "provider");
        char *pv = "auto";
        if (prov) {
            char *q1 = strchr(prov, '"');
            char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
            if (q1 && q2) { *q2 = 0; pv = q1 + 1; }
        }
        if (!first) fprintf(f, ",\n");
        fprintf(f, "    {\"name\": \"%s\", \"provider\": \"%s\"}", name, pv);
        first = 0;
    }
    fprintf(f, "\n  ]\n}\n");
    fclose(f);
    printf("wrote %s\n", path);
    free(data); free(dir); free(path);
    return 0;
}

static int cmd_c_add(const char *name, const char *provider, const char *version, const char *package) {
    if (!name) {
        fprintf(stderr, "katek c add: package name required\n");
        return 2;
    }
    char *root = find_project_root();
    char *mf = join_path(root, "katek.toml");
    char *data = read_small(mf);
    char *line = fmt("%s = { provider = \"%s\"", name, provider ? provider : "xrepo");
    if (version) {
        char *l2 = fmt("%s, version = \"%s\"", line, version);
        free(line); line = l2;
    }
    if (package) {
        char *l2 = fmt("%s, package = \"%s\"", line, package);
        free(line); line = l2;
    }
    char *l3 = fmt("%s }\n", line);
    free(line);
    FILE *f = fopen(mf, "ab");
    if (!f) {
        fprintf(stderr, "katek c add: cannot open %s\n", mf);
        free(root); free(mf); free(data); free(l3);
        return 1;
    }
    if (!data || !strstr(data, "[native.dependencies]")) {
        fputs("\n[native.dependencies]\n", f);
    }
    fputs(l3, f);
    fclose(f);
    printf("added native dependency %s\n", name);
    printf("  provider = %s\n", provider ? provider : "xrepo");
    free(root); free(mf); free(data); free(l3);
    return 0;
}

static void cmd_clean(const char *root) {
    char *target = join_path(root, "target");
#ifdef _WIN32
    char *cmd = fmt("rmdir /s /q \"%s\" 2>nul", target);
#else
    char *cmd = fmt("rm -rf \"%s\"", target);
#endif
    run_cmd(cmd);
    free(cmd);
    printf("cleaned %s\n", target);
    free(target);
}

static void cmd_doctor(const char *root) {
    printf("katek doctor\n");
#ifdef _WIN32
    int clang_ok = system("clang --version >nul 2>&1") == 0;
#else
    int clang_ok = system("clang --version >/dev/null 2>&1") == 0;
#endif
    printf("  clang: %s\n", clang_ok ? "ok" : "MISSING (install LLVM)");
    char *aktc = find_aktc();
    printf("  aktc:  %s (%s)\n", aktc, exists(aktc) ? "ok" : "MISSING");
    char *rt = join_path(root, "runtime");
    printf("  runtime: %s\n", exists(rt) ? "ok" : "MISSING");
    char *ndk = find_ndk();
    printf("  NDK:     %s\n", ndk ? ndk : "not found (Android builds disabled)");
    free(ndk);
    printf("  root: %s\n", root);
    free(aktc); free(rt);
}

static int cmd_export(const char *root, const char *kind) {
    if (!kind) {
        fprintf(stderr, "katek export: expected xmake or cmake\n");
        return 2;
    }
    char *main = join_path(root, "src/main.akt");
    char *gen = join_path(root, "src/main.gen.c");
    char *aktc = find_aktc();
    char *a = arg_quote(aktc);
    char *cmd = fmt("%s build \"%s\" --keep-c", a, main);
    int rc = run_cmd(cmd);
    free(cmd); free(aktc); free(a);
    if (rc != 0) { free(main); free(gen); return 1; }
    const char *tool_root = (getenv("AKT_ROOT") && *getenv("AKT_ROOT")) ? getenv("AKT_ROOT") : g_tool_root;
    if (!tool_root) tool_root = ".";
    char *tr = fmt("%s", tool_root);
    for (char *p = tr; *p; p++) if (*p == '\\') *p = '/';
    if (strcmp(kind, "xmake") == 0) {
        char *path = join_path(root, "xmake.lua");
        FILE *f = fopen(path, "wb");
        if (!f) { free(path); free(main); free(gen); return 1; }
        fprintf(f, "set_project(\"akt-app\")\nset_languages(\"c11\")\n\n");
        fprintf(f, "target(\"app\")\n    set_kind(\"binary\")\n");
        fprintf(f, "    add_files(\"src/main.gen.c\")\n");
        fprintf(f, "    add_files(\"%s/runtime/src/akt_runtime.c\")\n", tr);
        fprintf(f, "    add_includedirs(\"%s/runtime/include\")\n", tr);
        fprintf(f, "    set_targetdir(\"target\")\n");
        fclose(f);
        printf("wrote %s\n", path);
        free(path);
    } else if (strcmp(kind, "cmake") == 0) {
        char *path = join_path(root, "CMakeLists.txt");
        FILE *f = fopen(path, "wb");
        if (!f) { free(path); free(main); free(gen); return 1; }
        fprintf(f, "cmake_minimum_required(VERSION 3.20)\nproject(akt_app C)\n");
        fprintf(f, "set(CMAKE_C_STANDARD 11)\n");
        fprintf(f, "add_executable(app src/main.gen.c \"%s/runtime/src/akt_runtime.c\")\n", tr);
        fprintf(f, "target_include_directories(app PRIVATE \"%s/runtime/include\")\n", tr);
        fprintf(f, "if(UNIX)\n  target_link_libraries(app PRIVATE m)\nendif()\n");
        fclose(f);
        printf("wrote %s\n", path);
        free(path);
    } else {
        fprintf(stderr, "katek export: unknown backend '%s'\n", kind);
        free(main); free(gen);
        return 2;
    }
    free(main); free(gen);
    return 0;
}

static void usage(void) {
    printf("katek %s ??? build and package manager for akt\n\n", KATEK_VERSION);
    printf("usage:\n");
    printf("  katek new <name> [--bare] [--lib]\n");
    printf("  katek build [--release] [--shared] [--target android-arm64|android-arm|android-x86_64]\n");
    printf("  katek run [file.akt] [--release]\n");
    printf("  katek check\n");
    printf("  katek test\n");
    printf("  katek clean\n");
    printf("  katek doctor\n");
    printf("  katek lock\n");
    printf("  katek jni [--package pkg] [--class Name] [--no-build]\n");
    printf("  katek export xmake|cmake\n");
    printf("  katek c add <name> [--provider p] [--version v] [--package pkg]\n");
    printf("  katek c sync\n");
    printf("  katek c install [--dry-run]\n");
    printf("  katek --version\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 2; }
    g_tool_root = exe_root();
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("katek %s\n", KATEK_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "new") == 0) {
        const char *name = NULL;
        int bare = 0, lib = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--bare") == 0) bare = 1;
            else if (strcmp(argv[i], "--lib") == 0) lib = 1;
            else if (!name) name = argv[i];
        }
        cmd_new(name, bare, lib);
        return 0;
    }
    char *root = find_project_root();
    {
        char *cache = join_path(root, ".kache");
        MKDIR(cache);
#ifdef _WIN32
        _putenv_s("AKT_CACHE", cache);
#else
        setenv("AKT_CACHE", cache, 1);
#endif
        free(cache);
    }
    if (strcmp(argv[1], "build") == 0) {
        int release = 0, shared = 0, locked = 0;
        const char *target = NULL;
        const char *member = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--release") == 0) release = 1;
            else if (strcmp(argv[i], "--shared") == 0) shared = 1;
            else if (strcmp(argv[i], "--locked") == 0) locked = 1;
            else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) member = argv[++i];
            else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) target = argv[++i];
            else if (strcmp(argv[i], "--android-arm64") == 0) target = "android-arm64";
            else if (strcmp(argv[i], "--android-arm") == 0) target = "android-arm";
            else if (strcmp(argv[i], "--android-x86_64") == 0) target = "android-x86_64";
        }
        if (member) {
            char *p1 = join_path(root, "packages");
            char *p2 = join_path(p1, member);
            char *p3 = join_path(root, "apps");
            char *p4 = join_path(p3, member);
            const char *dir = exists(p2) ? p2 : (exists(p4) ? p4 : NULL);
            if (!dir) {
                fprintf(stderr, "katek: workspace member '%s' not found\n", member);
                free(p1); free(p2); free(p3); free(p4); free(root);
                return 1;
            }
            int rc = cmd_build(dir, release, 0, target, shared);
            free(p1); free(p2); free(p3); free(p4); free(root);
            return rc;
        }
        if (locked) {
            char *lk = join_path(root, "katek.lock");
            if (!exists(lk)) {
                fprintf(stderr, "katek: --locked but katek.lock is missing (run: katek lock)\n");
                free(lk); free(root);
                return 1;
            }
            free(lk);
        }
        int rc = cmd_build(root, release, 0, target, shared);
        free(root);
        return rc;
    }
    if (strcmp(argv[1], "run") == 0) {
        const char *file = NULL;
        const char *target = NULL;
        int release = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--release") == 0) release = 1;
            else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) target = argv[++i];
            else if (!file) file = argv[i];
        }
        if (file && exists(file)) {
            char *aktc = find_aktc();
            char *a = arg_quote(aktc);
            char *cmd = fmt("%s run \"%s\"%s", a, file, release ? " --release" : "");
            int rc = run_cmd(cmd);
            free(a); free(aktc); free(cmd); free(root);
            return rc == 0 ? 0 : 1;
        }
        int rc = cmd_build(root, release, 1, target, 0);
        free(root);
        return rc;
    }
    if (strcmp(argv[1], "check") == 0) {
        char *src = join_path(root, "src");
        char *main = join_path(src, "main.akt");
        char *aktc = find_aktc();
        char *cmd = fmt("\"%s\" check \"%s\"", aktc, main);
        int rc = run_cmd(cmd);
        free(src); free(main); free(aktc); free(cmd); free(root);
        return rc == 0 ? 0 : 1;
    }
    if (strcmp(argv[1], "test") == 0) {
        int rc = cmd_test(root);
        free(root);
        return rc;
    }
    if (strcmp(argv[1], "clean") == 0) {
        cmd_clean(root);
        free(root);
        return 0;
    }
    if (strcmp(argv[1], "doctor") == 0) {
        cmd_doctor(root);
        free(root);
        return 0;
    }
    if (strcmp(argv[1], "lock") == 0) {
        int rc = cmd_lock(root);
        free(root);
        return rc;
    }
    if (strcmp(argv[1], "jni") == 0) {
        const char *pkg = NULL, *klass = NULL;
        int no_build = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--package") == 0 && i + 1 < argc) pkg = argv[++i];
            else if (strcmp(argv[i], "--class") == 0 && i + 1 < argc) klass = argv[++i];
            else if (strcmp(argv[i], "--no-build") == 0) no_build = 1;
        }
        int rc = cmd_jni(root, pkg, klass, no_build);
        free(root);
        return rc;
    }
    if (strcmp(argv[1], "export") == 0) {
        int rc = cmd_export(root, argc >= 3 ? argv[2] : NULL);
        free(root);
        return rc;
    }
    if (strcmp(argv[1], "c") == 0 && argc >= 3 && strcmp(argv[2], "sync") == 0) {
        int rc = cmd_c_sync(root);
        free(root);
        return rc;
    }
    if (strcmp(argv[1], "c") == 0 && argc >= 3 && strcmp(argv[2], "install") == 0) {
        int dry = 0;
        for (int i = 3; i < argc; i++) if (strcmp(argv[i], "--dry-run") == 0) dry = 1;
        int rc = cmd_c_install(root, dry);
        free(root);
        return rc;
    }
    if (strcmp(argv[1], "c") == 0 && argc >= 4 && strcmp(argv[2], "add") == 0) {
        const char *name = argv[3];
        const char *provider = NULL, *version = NULL, *package = NULL;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--provider") == 0 && i + 1 < argc) provider = argv[++i];
            else if (strcmp(argv[i], "--version") == 0 && i + 1 < argc) version = argv[++i];
            else if (strcmp(argv[i], "--package") == 0 && i + 1 < argc) package = argv[++i];
        }
        int rc = cmd_c_add(name, provider, version, package);
        free(root);
        return rc;
    }
    fprintf(stderr, "katek: unknown command '%s'\n", argv[1]);
    usage();
    free(root);
    return 2;
}

