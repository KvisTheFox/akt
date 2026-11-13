#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <windows.h>

#define AKT_VERSION_TEXT "0.9.0"
#define PAYLOAD_MAGIC "AKTPAYLOAD1"
#define LLVM_INSTALLER_URL \
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/LLVM-18.1.8-win64.exe"

static int run(const char *cmd) {
    return system(cmd);
}

static void exe_path(char *out, size_t n) {
    GetModuleFileNameA(NULL, out, (DWORD)n);
}

static void exe_dir(char *out, size_t n) {
    exe_path(out, n);
    char *s = strrchr(out, '\\');
    if (s) *s = 0;
}

static int has_clang(void) {
    return system("clang --version >nul 2>&1") == 0;
}

static void add_user_path(const char *dir) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
        "powershell -NoProfile -ExecutionPolicy Bypass -Command "
        "\"$p=[Environment]::GetEnvironmentVariable('Path','User'); "
        "if($null -eq $p){$p=''}; "
        "if($p -notlike '*%s*'){[Environment]::SetEnvironmentVariable('Path',($p.TrimEnd(';')+';%s'),'User')}\"",
        dir, dir);
    run(cmd);
}

static int ask_yes_no(const char *question) {
    char line[64];
    printf("%s [y/N]: ", question);
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin)) return 0;
    return line[0] == 'y' || line[0] == 'Y';
}

static int install_clang_winget(void) {
    printf("Trying winget (LLVM.LLVM)...\n");
    return run("winget install -e --id LLVM.LLVM "
               "--accept-source-agreements --accept-package-agreements") == 0;
}

static int install_clang_download(void) {
    char tmp[4096];
    char tmpdir[4096];
    GetTempPathA(sizeof(tmpdir), tmpdir);
    snprintf(tmp, sizeof(tmp), "%sakt-llvm-setup.exe", tmpdir);
    printf("Downloading LLVM from:\n  %s\n", LLVM_INSTALLER_URL);
    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
        "curl.exe -L --fail --retry 2 -o \"%s\" \"%s\"", tmp, LLVM_INSTALLER_URL);
    if (run(cmd) != 0) {
        printf("Download failed.\n");
        return 0;
    }
    printf("Running the LLVM installer (silent)...\n");
    snprintf(cmd, sizeof(cmd), "\"%s\" /S", tmp);
    int rc = run(cmd);
    remove(tmp);
    return rc == 0;
}

/* The setup executable carries a zip payload:
 *   [exe][zip bytes][8-byte little-endian size]["AKTPAYLOAD1"]
 */
static int extract_payload(const char *exe, const char *dest) {
    FILE *f = fopen(exe, "rb");
    if (!f) return 0;
    if (_fseeki64(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long long fsz = _ftelli64(f);
    if (fsz < 20) { fclose(f); return 0; }
    if (_fseeki64(f, fsz - 20, SEEK_SET) != 0) { fclose(f); return 0; }
    unsigned char footer[20];
    if (fread(footer, 1, 20, f) != 20) { fclose(f); return 0; }
    if (memcmp(footer + 8, PAYLOAD_MAGIC, 12) != 0) { fclose(f); return 0; }

    unsigned long long size = 0;
    for (int i = 7; i >= 0; i--) size = (size << 8) | footer[i];
    if (size == 0 || (long long)size > fsz - 20) { fclose(f); return 0; }
    long long off = fsz - 20 - (long long)size;
    if (off <= 0) { fclose(f); return 0; }

    char tmpdir[4096], zpath[4096];
    GetTempPathA(sizeof(tmpdir), tmpdir);
    snprintf(zpath, sizeof(zpath), "%sakt-payload.zip", tmpdir);

    if (_fseeki64(f, off, SEEK_SET) != 0) { fclose(f); return 0; }
    FILE *o = fopen(zpath, "wb");
    if (!o) { fclose(f); return 0; }
    char buf[65536];
    unsigned long long left = size;
    while (left > 0) {
        size_t want = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
        size_t got = fread(buf, 1, want, f);
        if (got == 0) break;
        fwrite(buf, 1, got, o);
        left -= got;
    }
    fclose(o);
    fclose(f);
    if (left != 0) { remove(zpath); return 0; }

    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "tar.exe -xf \"%s\" -C \"%s\"", zpath, dest);
    int rc = run(cmd);
    if (rc != 0) {
        snprintf(cmd, sizeof(cmd),
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
            zpath, dest);
        rc = run(cmd);
    }
    remove(zpath);
    return rc == 0;
}

int main(int argc, char **argv) {
    int dry = 0, quiet = 0, no_llvm = 0, no_path = 0;
    const char *prefix = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0) dry = 1;
        else if (strcmp(argv[i], "--quiet") == 0) quiet = 1;
        else if (strcmp(argv[i], "--no-llvm") == 0) no_llvm = 1;
        else if (strcmp(argv[i], "--no-path") == 0) no_path = 1;
        else if (strcmp(argv[i], "--prefix") == 0 && i + 1 < argc) prefix = argv[++i];
        else if (strcmp(argv[i], "--help") == 0) {
            printf("akt setup %s\n\n", AKT_VERSION_TEXT);
            printf("usage: akt-setup [--quiet] [--dry-run] [--no-llvm] [--no-path] [--prefix DIR]\n");
            printf("  --quiet        do not ask questions\n");
            printf("  --dry-run      print the plan, change nothing\n");
            printf("  --no-llvm      never install Clang\n");
            printf("  --no-path      do not modify the user PATH\n");
            printf("  --prefix DIR   install into DIR instead of %%LOCALAPPDATA%%\\akt\n");
            return 0;
        }
    }

    char exe[4096], here[4096], root[4096];
    exe_path(exe, sizeof(exe));
    snprintf(here, sizeof(here), "%s", exe);
    exe_dir(here, sizeof(here));
    snprintf(root, sizeof(root), "%s", here);
    char *slash = strrchr(root, '\\');
    if (slash && (strcmp(slash + 1, "bin") == 0 || strcmp(slash + 1, "installer") == 0))
        *slash = 0;

    char dest[4096];
    if (prefix) {
        snprintf(dest, sizeof(dest), "%s", prefix);
    } else {
        const char *local = getenv("LOCALAPPDATA");
        if (!local) local = "C:\\akt";
        snprintf(dest, sizeof(dest), "%s\\akt", local);
    }

    printf("akt setup %s\n", AKT_VERSION_TEXT);
    printf("  destination: %s\n", dest);

    if (dry) {
        printf("dry run: nothing changed\n");
        return 0;
    }

    if (!has_clang()) {
        printf("\nClang was not found in PATH.\n");
        printf("akt needs Clang (LLVM) to compile programs.\n");
        int do_install = 0;
        if (!no_llvm) {
            if (quiet) do_install = 1;
            else do_install = ask_yes_no("Download and install LLVM now?");
        }
        if (do_install) {
            if (!install_clang_winget() && !install_clang_download()) {
                printf("\nCould not install LLVM automatically.\n");
                printf("Install it manually: https://releases.llvm.org/\n");
                printf("Then run this setup again.\n");
                return 1;
            }
            printf("LLVM installed. You may need to reopen the terminal.\n");
        } else {
            printf("\nSkipping LLVM. Install it later from https://releases.llvm.org/\n");
        }
    } else {
        printf("Clang: ok\n");
    }

    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "if not exist \"%s\" mkdir \"%s\"", dest, dest);
    run(cmd);

    printf("Installing files...\n");
    if (extract_payload(exe, dest)) {
        printf("  embedded package extracted\n");
    } else {
        /* development build: copy from the source tree */
        printf("  no embedded package, copying from %s\n", root);
        const char *dirs[] = {"bin", "runtime", "docs", "examples", "std", "benchmarks", "scripts", NULL};
        for (int i = 0; dirs[i]; i++) {
            char src[4096];
            snprintf(src, sizeof(src), "%s\\%s", root, dirs[i]);
            if (GetFileAttributesA(src) == INVALID_FILE_ATTRIBUTES) continue;
            snprintf(cmd, sizeof(cmd), "xcopy /E /I /Y \"%s\" \"%s\\%s\" >nul", src, dest, dirs[i]);
            run(cmd);
        }
        const char *files[] = {"README.md", "README.ru.md", "VERSION", "CHANGELOG.md", "LICENSE", NULL};
        for (int i = 0; files[i]; i++) {
            char src[4096];
            snprintf(src, sizeof(src), "%s\\%s", root, files[i]);
            if (GetFileAttributesA(src) == INVALID_FILE_ATTRIBUTES) continue;
            snprintf(cmd, sizeof(cmd), "copy /Y \"%s\" \"%s\\%s\" >nul", src, dest, files[i]);
            run(cmd);
        }
    }

    if (!no_path) {
        char bindir[4096];
        snprintf(bindir, sizeof(bindir), "%s\\bin", dest);
        add_user_path(bindir);
        printf("PATH updated for new terminals.\n");
    }

    printf("\nakt %s installed to %s\n", AKT_VERSION_TEXT, dest);
    printf("\nOpen a new terminal and run:\n");
    printf("  aktc --version\n");
    printf("  katek new hello\n");
    printf("  cd hello && katek run\n");
    if (!has_clang())
        printf("\nNote: Clang is still not visible in this session. Restart the terminal\n"
               "or install LLVM from https://releases.llvm.org/.\n");
    return 0;
}
