/* util.c - implementation of the helper library used by the compiler */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/* memory                                                              */
/* ------------------------------------------------------------------ */

static void out_of_memory(void) {
    fputs("kasturi: out of memory\n", stderr);
    exit(2);
}

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) out_of_memory();
    return p;
}

void *xcalloc(size_t count, size_t size) {
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p) out_of_memory();
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) out_of_memory();
    return q;
}

char *xstrdup(const char *s) {
    return xstrndup(s, strlen(s));
}

char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *xvformat(const char *fmt, va_list ap) {
    va_list cp;
    va_copy(cp, ap);
    int n = vsnprintf(NULL, 0, fmt, cp);
    va_end(cp);
    if (n < 0) return xstrdup("");
    char *buf = xmalloc((size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, fmt, ap);
    return buf;
}

char *xformat(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = xvformat(fmt, ap);
    va_end(ap);
    return s;
}

/* ------------------------------------------------------------------ */
/* StrBuf / Vec / StrMap                                               */
/* ------------------------------------------------------------------ */

void sb_init(StrBuf *sb) {
    sb->data = xmalloc(64);
    sb->data[0] = '\0';
    sb->len = 0;
    sb->cap = 64;
}

static void sb_reserve(StrBuf *sb, size_t extra) {
    if (!sb->data) sb_init(sb);
    if (sb->len + extra + 1 <= sb->cap) return;
    size_t cap = sb->cap * 2;
    while (cap < sb->len + extra + 1) cap *= 2;
    sb->data = xrealloc(sb->data, cap);
    sb->cap = cap;
}

void sb_putc(StrBuf *sb, char c) {
    sb_reserve(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

void sb_putn(StrBuf *sb, const char *s, size_t n) {
    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_puts(StrBuf *sb, const char *s) {
    sb_putn(sb, s, strlen(s));
}

void sb_printf(StrBuf *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = xvformat(fmt, ap);
    va_end(ap);
    sb_puts(sb, s);
    free(s);
}

void sb_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

void vec_push(Vec *v, void *item) {
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->items = xrealloc(v->items, (size_t)v->cap * sizeof(void *));
    }
    v->items[v->len++] = item;
}

static size_t hash_str(const char *s) {
    size_t h = 1469598103934665603ULL & SIZE_MAX;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= (size_t)1099511628211ULL;
    }
    return h;
}

void *map_get(const StrMap *m, const char *key) {
    if (!m->cap) return NULL;
    size_t i = hash_str(key) & (m->cap - 1);
    while (m->keys[i]) {
        if (strcmp(m->keys[i], key) == 0) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
    return NULL;
}

static void map_grow(StrMap *m) {
    size_t old_cap = m->cap;
    char **old_keys = m->keys;
    void **old_vals = m->vals;
    m->cap = old_cap ? old_cap * 2 : 64;
    m->keys = xcalloc(m->cap, sizeof(char *));
    m->vals = xcalloc(m->cap, sizeof(void *));
    m->count = 0;
    for (size_t i = 0; i < old_cap; i++)
        if (old_keys[i]) map_put(m, old_keys[i], old_vals[i]);
    free(old_keys);
    free(old_vals);
}

void map_put(StrMap *m, const char *key, void *val) {
    if ((m->count + 1) * 2 > m->cap) map_grow(m);
    size_t i = hash_str(key) & (m->cap - 1);
    while (m->keys[i]) {
        if (strcmp(m->keys[i], key) == 0) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
    m->keys[i] = xstrdup(key);
    m->vals[i] = val;
    m->count++;
}

/* ------------------------------------------------------------------ */
/* UTF-8                                                               */
/* ------------------------------------------------------------------ */

int utf8_decode(const char *str, size_t len, unsigned *cp) {
    const unsigned char *s = (const unsigned char *)str;
    if (len == 0) {
        *cp = 0;
        return 1;
    }
    unsigned c = s[0];
    int n;
    unsigned min;
    if (c < 0x80) {
        *cp = c;
        return 1;
    } else if ((c & 0xE0) == 0xC0) {
        n = 2; c &= 0x1F; min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        n = 3; c &= 0x0F; min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        n = 4; c &= 0x07; min = 0x10000;
    } else {
        *cp = 0xFFFD;
        return 1;
    }
    if ((size_t)n > len) {
        *cp = 0xFFFD;
        return 1;
    }
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return 1;
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
    *cp = c;
    return n;
}

int utf8_encode(unsigned cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
}

bool str_is_ascii(const char *s) {
    for (; *s; s++)
        if ((unsigned char)*s >= 0x80) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* Windows wide-string helpers                                         */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
static wchar_t *to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = xmalloc(sizeof(wchar_t) * (size_t)(n > 0 ? n : 1));
    if (n <= 0) {
        w[0] = 0;
        return w;
    }
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static char *from_wide(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = xmalloc((size_t)(n > 0 ? n : 1));
    if (n <= 0) {
        s[0] = 0;
        return s;
    }
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}
#endif

/* ------------------------------------------------------------------ */
/* files                                                               */
/* ------------------------------------------------------------------ */

static FILE *open_utf8(const char *path, const char *mode) {
#ifdef _WIN32
    wchar_t *wp = to_wide(path);
    wchar_t *wm = to_wide(mode);
    FILE *f = _wfopen(wp, wm);
    free(wp);
    free(wm);
    return f;
#else
    return fopen(path, mode);
#endif
}

char *read_file(const char *path, size_t *len) {
    FILE *f = open_utf8(path, "rb");
    if (!f) return NULL;
    StrBuf sb;
    sb_init(&sb);
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) sb_putn(&sb, buf, n);
    fclose(f);
    if (len) *len = sb.len;
    return sb.data;
}

bool write_file(const char *path, const char *data, size_t len) {
    FILE *f = open_utf8(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    if (fclose(f) != 0) ok = false;
    return ok;
}

bool file_exists(const char *path) {
#ifdef _WIN32
    wchar_t *w = to_wide(path);
    DWORD a = GetFileAttributesW(w);
    free(w);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool dir_exists(const char *path) {
#ifdef _WIN32
    wchar_t *w = to_wide(path);
    DWORD a = GetFileAttributesW(w);
    free(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool remove_file(const char *path) {
#ifdef _WIN32
    wchar_t *w = to_wide(path);
    BOOL ok = DeleteFileW(w);
    free(w);
    return ok != 0;
#else
    return unlink(path) == 0;
#endif
}

bool move_file(const char *from, const char *to) {
#ifdef _WIN32
    wchar_t *a = to_wide(from), *b = to_wide(to);
    BOOL ok = MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
    free(a);
    free(b);
    return ok != 0;
#else
    if (rename(from, to) == 0) return true;
    /* cross-device: copy */
    size_t n;
    char *data = read_file(from, &n);
    if (!data) return false;
    bool ok = write_file(to, data, n);
    free(data);
    if (ok) {
        chmod(to, 0755);
        unlink(from);
    }
    return ok;
#endif
}

bool make_dir(const char *path) {
#ifdef _WIN32
    wchar_t *w = to_wide(path);
    int r = _wmkdir(w);
    free(w);
    return r == 0;
#else
    return mkdir(path, 0700) == 0;
#endif
}

bool remove_dir(const char *path) {
#ifdef _WIN32
    wchar_t *w = to_wide(path);
    int r = _wrmdir(w);
    free(w);
    return r == 0;
#else
    return rmdir(path) == 0;
#endif
}

static bool is_sep(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

char *path_dirname(const char *path) {
    size_t n = strlen(path);
    while (n > 0 && !is_sep(path[n - 1])) n--;
    if (n == 0) return xstrdup(".");
    if (n == 1) return xstrndup(path, 1);
#ifdef _WIN32
    if (n == 3 && path[1] == ':') return xstrndup(path, 3);
#endif
    return xstrndup(path, n - 1);
}

char *path_basename(const char *path) {
    size_t n = strlen(path);
    size_t i = n;
    while (i > 0 && !is_sep(path[i - 1])) i--;
    return xstrdup(path + i);
}

char *path_strip_ext(const char *path) {
    size_t n = strlen(path);
    size_t i = n;
    while (i > 0 && !is_sep(path[i - 1])) {
        if (path[i - 1] == '.') return xstrndup(path, i - 1);
        i--;
    }
    return xstrdup(path);
}

bool path_is_absolute(const char *p) {
#ifdef _WIN32
    if (is_sep(p[0])) return true;
    return isalpha((unsigned char)p[0]) && p[1] == ':';
#else
    return p[0] == '/';
#endif
}

char *path_join(const char *a, const char *b) {
    if (!a || !*a || path_is_absolute(b)) return xstrdup(b);
    size_t n = strlen(a);
#ifdef _WIN32
    const char *sep = "\\";
#else
    const char *sep = "/";
#endif
    if (is_sep(a[n - 1])) sep = "";
    return xformat("%s%s%s", a, sep, b);
}

char *path_absolute(const char *path) {
#ifdef _WIN32
    wchar_t *w = to_wide(path);
    wchar_t *full = _wfullpath(NULL, w, 0);
    free(w);
    if (!full) return xstrdup(path);
    char *r = from_wide(full);
    free(full);
    return r;
#else
    char buf[PATH_MAX];
    if (realpath(path, buf)) return xstrdup(buf);
    if (path_is_absolute(path)) return xstrdup(path);
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) return xstrdup(path);
    return path_join(cwd, path);
#endif
}

bool str_ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && memcmp(s + a - b, suffix, b) == 0;
}

char *ascii_safe_path(const char *path) {
#ifdef _WIN32
    if (str_is_ascii(path)) return xstrdup(path);
    wchar_t *w = to_wide(path);
    DWORD n = GetShortPathNameW(w, NULL, 0);
    if (n == 0) {
        free(w);
        return xstrdup(path);
    }
    wchar_t *s = xmalloc(sizeof(wchar_t) * n);
    GetShortPathNameW(w, s, n);
    char *r = from_wide(s);
    free(w);
    free(s);
    return r;
#else
    return xstrdup(path);
#endif
}

char *temp_directory(void) {
#ifdef _WIN32
    wchar_t buf[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, buf);
    if (n == 0 || n > MAX_PATH) return xstrdup(".");
    while (n > 3 && (buf[n - 1] == L'\\' || buf[n - 1] == L'/')) buf[--n] = 0;
    char *u = from_wide(buf);
    char *r = ascii_safe_path(u);
    free(u);
    return r;
#else
    const char *t = getenv("TMPDIR");
    if (!t || !*t) t = "/tmp";
    return xstrdup(t);
#endif
}

char *exe_path(const char *argv0) {
#ifdef _WIN32
    (void)argv0;
    wchar_t buf[32768];
    DWORD n = GetModuleFileNameW(NULL, buf, 32768);
    if (n == 0 || n >= 32768) return NULL;
    return from_wide(buf);
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = 0;
        return xstrdup(buf);
    }
    if (argv0 && strchr(argv0, '/')) return path_absolute(argv0);
    if (argv0) {
        char *p = find_in_path(argv0);
        if (p) return p;
    }
    return NULL;
#endif
}

char *find_in_path(const char *program) {
#ifdef _WIN32
    wchar_t *w = to_wide(program);
    wchar_t buf[MAX_PATH + 1];
    DWORD n = SearchPathW(NULL, w, L".exe", MAX_PATH + 1, buf, NULL);
    free(w);
    if (n == 0 || n > MAX_PATH) return NULL;
    return from_wide(buf);
#else
    if (strchr(program, '/')) return access(program, X_OK) == 0 ? xstrdup(program) : NULL;
    const char *path = getenv("PATH");
    if (!path) return NULL;
    const char *p = path;
    while (*p) {
        const char *e = strchr(p, ':');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char *dir = n ? xstrndup(p, n) : xstrdup(".");
        char *full = path_join(dir, program);
        free(dir);
        if (access(full, X_OK) == 0) return full;
        free(full);
        if (!e) break;
        p = e + 1;
    }
    return NULL;
#endif
}

char *get_env(const char *name) {
#ifdef _WIN32
    wchar_t *w = to_wide(name);
    DWORD n = GetEnvironmentVariableW(w, NULL, 0);
    if (n == 0) {
        free(w);
        return NULL;
    }
    wchar_t *buf = xmalloc(sizeof(wchar_t) * n);
    GetEnvironmentVariableW(w, buf, n);
    char *r = from_wide(buf);
    free(w);
    free(buf);
    return r;
#else
    const char *v = getenv(name);
    return v ? xstrdup(v) : NULL;
#endif
}

int current_pid(void) {
#ifdef _WIN32
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}

/* ------------------------------------------------------------------ */
/* processes                                                           */
/* ------------------------------------------------------------------ */

int run_command(const char *cmd) {
    fflush(stdout);
    fflush(stderr);
#ifdef _WIN32
    /* cmd.exe strips the outermost pair of quotes, so add one */
    char *wrapped = xformat("\"%s\"", cmd);
    wchar_t *w = to_wide(wrapped);
    int r = _wsystem(w);
    free(w);
    free(wrapped);
    return r;
#else
    int r = system(cmd);
    if (r == -1) return -1;
    if (WIFEXITED(r)) return WEXITSTATUS(r);
    return 128 + (WIFSIGNALED(r) ? WTERMSIG(r) : 0);
#endif
}

char *capture_command(const char *cmd) {
#ifdef _WIN32
    char *wrapped = xformat("\"%s\"", cmd);
    wchar_t *w = to_wide(wrapped);
    FILE *p = _wpopen(w, L"rt");
    free(w);
    free(wrapped);
#else
    FILE *p = popen(cmd, "r");
#endif
    if (!p) return NULL;
    char line[4096];
    char *result = NULL;
    if (fgets(line, sizeof line, p)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (n > 0) result = xstrdup(line);
    }
    while (fgets(line, sizeof line, p)) {
    }
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    return result;
}

/* ------------------------------------------------------------------ */
/* console                                                             */
/* ------------------------------------------------------------------ */

void con_write(FILE *f, const char *s, size_t n) {
#ifdef _WIN32
    fflush(f);
    HANDLE h = (HANDLE)_get_osfhandle(_fileno(f));
    DWORD mode;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        int wn = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, NULL, 0);
        if (wn > 0) {
            wchar_t *w = xmalloc(sizeof(wchar_t) * (size_t)wn);
            MultiByteToWideChar(CP_UTF8, 0, s, (int)n, w, wn);
            DWORD written;
            WriteConsoleW(h, w, (DWORD)wn, &written, NULL);
            free(w);
        }
        return;
    }
#endif
    fwrite(s, 1, n, f);
    fflush(f);
}

void con_printf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = xvformat(fmt, ap);
    va_end(ap);
    con_write(f, s, strlen(s));
    free(s);
}

/* ------------------------------------------------------------------ */
/* arguments                                                           */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
/* Parse the command line exactly like the Microsoft C runtime does. */
static char **win_parse_command_line(int *argc_out) {
    const wchar_t *p = GetCommandLineW();
    Vec args = {0};
    wchar_t *buf = xmalloc((wcslen(p) + 1) * sizeof(wchar_t));
    size_t n = 0;
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"') buf[n++] = *p++;
        if (*p) p++;
    } else {
        while (*p && *p != L' ' && *p != L'\t') buf[n++] = *p++;
    }
    buf[n] = 0;
    vec_push(&args, from_wide(buf));
    for (;;) {
        while (*p == L' ' || *p == L'\t') p++;
        if (!*p) break;
        n = 0;
        int quoted = 0;
        while (*p) {
            if (!quoted && (*p == L' ' || *p == L'\t')) break;
            if (*p == L'\\') {
                size_t bs = 0;
                while (*p == L'\\') {
                    bs++;
                    p++;
                }
                if (*p == L'"') {
                    for (size_t i = 0; i < bs / 2; i++) buf[n++] = L'\\';
                    if (bs % 2) {
                        buf[n++] = L'"';
                        p++;
                    }
                } else {
                    for (size_t i = 0; i < bs; i++) buf[n++] = L'\\';
                }
                continue;
            }
            if (*p == L'"') {
                if (quoted && p[1] == L'"') {
                    buf[n++] = L'"';
                    p += 2;
                    continue;
                }
                quoted = !quoted;
                p++;
                continue;
            }
            buf[n++] = *p++;
        }
        buf[n] = 0;
        vec_push(&args, from_wide(buf));
    }
    free(buf);
    *argc_out = args.len;
    vec_push(&args, NULL);
    return (char **)args.items;
}
#endif

char **utf8_args(int argc, char **argv, int *out_argc) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    return win_parse_command_line(out_argc);
#else
    *out_argc = argc;
    return argv;
#endif
}
