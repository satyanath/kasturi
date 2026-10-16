/* kst_os.c - operating system layer of the Kasturi runtime (Win32 / POSIX) */
#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#define _CRT_SECURE_NO_WARNINGS

#ifdef _WIN32
#include <stdio.h>
#include <wchar.h>
#endif
#include <stdlib.h>
#include <string.h>

#include "kst_internal.h"

int kst_errno;

int kst_errno_get(void) { return kst_errno; }
void kst_errno_set(int e) { kst_errno = e; }

/* Decodes one UTF-8 sequence. Returns bytes consumed, or 0 when the
 * sequence is valid so far but incomplete. Invalid input gives U+FFFD. */
static int u8_next(const unsigned char *s, size_t len, unsigned *cp) {
    unsigned c = s[0];
    int need;
    unsigned v, min;
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF) {
        need = 2; v = c & 0x1F; min = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
        need = 3; v = c & 0x0F; min = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
        need = 4; v = c & 0x07; min = 0x10000;
    } else {
        *cp = 0xFFFD;
        return 1;
    }
    for (int k = 1; k < need; k++) {
        if ((size_t)k >= len) return 0;
        if ((s[k] & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return k;
        }
        v = (v << 6) | (s[k] & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) v = 0xFFFD;
    *cp = v;
    return need;
}

static int u8_put(unsigned cp, unsigned char *out) {
    if (cp < 0x80) {
        out[0] = (unsigned char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (unsigned char)(0xF0 | (cp >> 18));
    out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

#ifdef _WIN32
/* ================================================================== */
/* Windows                                                             */
/* ================================================================== */
#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <windows.h>

static wchar_t *to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) {
        kst_errno = KST_EILSEQ;
        return NULL;
    }
    wchar_t *w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)n);
    if (!w) {
        kst_errno = KST_ENOMEM;
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static char *from_wide(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = (char *)malloc((size_t)n);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

static void set_errno_win(DWORD e) {
    switch (e) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
    case ERROR_BAD_PATHNAME: kst_errno = KST_ENOENT; break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_CANNOT_MAKE: kst_errno = KST_EACCES; break;
    case ERROR_WRITE_PROTECT: kst_errno = KST_EROFS; break;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS: kst_errno = KST_EEXIST; break;
    case ERROR_INVALID_HANDLE: kst_errno = KST_EBADF; break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY: kst_errno = KST_ENOMEM; break;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL: kst_errno = KST_ENOSPC; break;
    case ERROR_TOO_MANY_OPEN_FILES: kst_errno = KST_EMFILE; break;
    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_NAME:
    case ERROR_NEGATIVE_SEEK: kst_errno = KST_EINVAL; break;
    case ERROR_BROKEN_PIPE:
    case ERROR_NO_DATA: kst_errno = KST_EPIPE; break;
    case ERROR_DIRECTORY: kst_errno = KST_ENOTDIR; break;
    case ERROR_DIR_NOT_EMPTY: kst_errno = KST_ENOTEMPTY; break;
    case ERROR_FILENAME_EXCED_RANGE: kst_errno = KST_ENAMETOOLONG; break;
    case ERROR_NOT_SAME_DEVICE: kst_errno = KST_EXDEV; break;
    case ERROR_BUSY: kst_errno = KST_EBUSY; break;
    default: kst_errno = KST_EIO; break;
    }
}

void kst_os_init(void) {}

kst_handle kst_os_std_handle(int which) {
    DWORD id = which == 0 ? STD_INPUT_HANDLE : which == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE;
    HANDLE h = GetStdHandle(id);
    if (h == NULL || h == INVALID_HANDLE_VALUE) return KST_BAD_HANDLE;
    return (kst_handle)h;
}

int kst_os_is_console(kst_handle h) {
    DWORD mode;
    if (h == KST_BAD_HANDLE) return 0;
    return GetConsoleMode((HANDLE)h, &mode) != 0;
}

int kst_os_is_seekable(kst_handle h) {
    if (h == KST_BAD_HANDLE) return 0;
    return GetFileType((HANDLE)h) == FILE_TYPE_DISK;
}

int kst_os_open(const char *path, int flags, kst_handle *out) {
    wchar_t *w = to_wide(path);
    if (!w) return -1;
    DWORD attrs = GetFileAttributesW(w);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        free(w);
        kst_errno = KST_EISDIR;
        return -1;
    }
    DWORD access = 0;
    if (flags & KO_READ) access |= GENERIC_READ;
    if (flags & KO_WRITE) access |= GENERIC_WRITE;
    DWORD disp;
    if (flags & KO_CREATE) {
        if (flags & KO_EXCL) disp = CREATE_NEW;
        else if (flags & KO_TRUNC) disp = CREATE_ALWAYS;
        else disp = OPEN_ALWAYS;
    } else {
        disp = (flags & KO_TRUNC) ? TRUNCATE_EXISTING : OPEN_EXISTING;
    }
    HANDLE h = CreateFileW(w, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, disp,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    free(w);
    if (h == INVALID_HANDLE_VALUE) {
        set_errno_win(GetLastError());
        return -1;
    }
    *out = (kst_handle)h;
    return 0;
}

/* console input is read as UTF-16 lines and handed out as UTF-8 */
static unsigned char con_pending[4 * 4096 + 8];
static size_t con_pos, con_len;
static int con_eof_pending;

static long long console_read(HANDLE h, unsigned char *buf, size_t n) {
    while (con_pos >= con_len) {
        con_pos = con_len = 0;
        if (con_eof_pending) {
            con_eof_pending = 0;
            return 0;
        }
        wchar_t wbuf[4096];
        DWORD got = 0;
        if (!ReadConsoleW(h, wbuf, 4096, &got, NULL)) {
            set_errno_win(GetLastError());
            return -1;
        }
        if (got == 0) return 0;
        for (DWORD i = 0; i < got; i++) {
            unsigned cp = wbuf[i];
            if (cp == 0x1A) { /* Ctrl+Z = end of input */
                con_eof_pending = 1;
                break;
            }
            if (cp == '\r') continue;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (i + 1 < got && wbuf[i + 1] >= 0xDC00 && wbuf[i + 1] <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (wbuf[i + 1] - 0xDC00);
                    i++;
                } else {
                    cp = 0xFFFD;
                }
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                cp = 0xFFFD;
            }
            con_len += (size_t)u8_put(cp, con_pending + con_len);
        }
    }
    size_t avail = con_len - con_pos;
    if (avail > n) avail = n;
    memcpy(buf, con_pending + con_pos, avail);
    con_pos += avail;
    return (long long)avail;
}

long long kst_os_read(kst_handle h, void *buf, size_t n, int console) {
    if (h == KST_BAD_HANDLE) {
        kst_errno = KST_EBADF;
        return -1;
    }
    if (console) return console_read((HANDLE)h, (unsigned char *)buf, n);
    DWORD got = 0;
    DWORD want = n > 0x40000000u ? 0x40000000u : (DWORD)n;
    if (!ReadFile((HANDLE)h, buf, want, &got, NULL)) {
        DWORD e = GetLastError();
        if (e == ERROR_BROKEN_PIPE || e == ERROR_HANDLE_EOF) return 0;
        set_errno_win(e);
        return -1;
    }
    return (long long)got;
}

static int console_flush_w(HANDLE h, const wchar_t *w, DWORD n) {
    while (n > 0) {
        DWORD done = 0;
        if (!WriteConsoleW(h, w, n, &done, NULL) || done == 0) {
            set_errno_win(GetLastError());
            return -1;
        }
        w += done;
        n -= done;
    }
    return 0;
}

static long long console_write(HANDLE h, const unsigned char *data, size_t n, kst_u8carry *carry) {
    wchar_t wbuf[2048];
    DWORD wn = 0;
    unsigned char joined[8];
    size_t i = 0;

    /* finish a sequence that was split by the previous write */
    if (carry && carry->n > 0) {
        size_t k = 0;
        memcpy(joined, carry->bytes, (size_t)carry->n);
        size_t jn = (size_t)carry->n;
        while (jn < 4 && k < n) joined[jn++] = data[k++];
        unsigned cp;
        int used = u8_next(joined, jn, &cp);
        if (used == 0) {
            if (k >= n) { /* still incomplete */
                memcpy(carry->bytes, joined, jn);
                carry->n = (int)jn;
                return (long long)n;
            }
            used = (int)jn;
            cp = 0xFFFD;
        }
        /* bytes of 'data' consumed by this sequence */
        i = (size_t)used > (size_t)carry->n ? (size_t)used - (size_t)carry->n : 0;
        carry->n = 0;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            wbuf[wn++] = (wchar_t)(0xD800 + (cp >> 10));
            wbuf[wn++] = (wchar_t)(0xDC00 + (cp & 0x3FF));
        } else {
            wbuf[wn++] = (wchar_t)cp;
        }
    }
    while (i < n) {
        unsigned cp;
        int used = u8_next(data + i, n - i, &cp);
        if (used == 0) {
            if (carry) {
                carry->n = (int)(n - i);
                memcpy(carry->bytes, data + i, n - i);
                break;
            }
            cp = 0xFFFD;
            used = (int)(n - i);
        }
        i += (size_t)used;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            wbuf[wn++] = (wchar_t)(0xD800 + (cp >> 10));
            wbuf[wn++] = (wchar_t)(0xDC00 + (cp & 0x3FF));
        } else {
            wbuf[wn++] = (wchar_t)cp;
        }
        if (wn >= 2040) {
            if (console_flush_w(h, wbuf, wn) != 0) return -1;
            wn = 0;
        }
    }
    if (wn > 0 && console_flush_w(h, wbuf, wn) != 0) return -1;
    return (long long)n;
}

long long kst_os_write(kst_handle h, const void *buf, size_t n, int console, kst_u8carry *carry) {
    if (h == KST_BAD_HANDLE) {
        kst_errno = KST_EBADF;
        return -1;
    }
    if (console) return console_write((HANDLE)h, (const unsigned char *)buf, n, carry);
    const char *p = (const char *)buf;
    size_t total = 0;
    while (total < n) {
        DWORD done = 0;
        size_t rem = n - total;
        DWORD chunk = rem > 0x40000000u ? 0x40000000u : (DWORD)rem;
        if (!WriteFile((HANDLE)h, p + total, chunk, &done, NULL)) {
            set_errno_win(GetLastError());
            return total ? (long long)total : -1;
        }
        if (done == 0) break;
        total += done;
    }
    return (long long)total;
}

long long kst_os_seek(kst_handle h, long long offset, int whence) {
    if (!kst_os_is_seekable(h)) {
        kst_errno = KST_ESPIPE;
        return -1;
    }
    LARGE_INTEGER dist, result;
    dist.QuadPart = offset;
    DWORD method = whence == KST_SEEK_SET ? FILE_BEGIN : whence == KST_SEEK_CUR ? FILE_CURRENT : FILE_END;
    if (!SetFilePointerEx((HANDLE)h, dist, &result, method)) {
        set_errno_win(GetLastError());
        return -1;
    }
    return result.QuadPart;
}

int kst_os_close(kst_handle h) {
    if (h == KST_BAD_HANDLE) {
        kst_errno = KST_EBADF;
        return -1;
    }
    if (!CloseHandle((HANDLE)h)) {
        set_errno_win(GetLastError());
        return -1;
    }
    return 0;
}

int kst_os_remove(const char *path) {
    wchar_t *w = to_wide(path);
    if (!w) return -1;
    int r = 0;
    DWORD attrs = GetFileAttributesW(w);
    BOOL ok = (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) ? RemoveDirectoryW(w)
                                                                                         : DeleteFileW(w);
    if (!ok) {
        set_errno_win(GetLastError());
        r = -1;
    }
    free(w);
    return r;
}

int kst_os_rename(const char *from, const char *to) {
    wchar_t *a = to_wide(from);
    wchar_t *b = to_wide(to);
    int r = -1;
    if (a && b) {
        if (MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
            r = 0;
        else
            set_errno_win(GetLastError());
    }
    free(a);
    free(b);
    return r;
}

int kst_os_tmpfile(kst_handle *out) {
    wchar_t dir[MAX_PATH + 1], name[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, dir);
    if (n == 0 || n > MAX_PATH) {
        set_errno_win(GetLastError());
        return -1;
    }
    if (!GetTempFileNameW(dir, L"kst", 0, name)) {
        set_errno_win(GetLastError());
        return -1;
    }
    HANDLE h = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        set_errno_win(GetLastError());
        DeleteFileW(name);
        return -1;
    }
    *out = (kst_handle)h;
    return 0;
}

int kst_os_tmpname(char *buf, size_t size) {
    static unsigned counter;
    wchar_t dir[MAX_PATH + 1], name[MAX_PATH + 64];
    DWORD n = GetTempPathW(MAX_PATH + 1, dir);
    if (n == 0 || n > MAX_PATH) {
        set_errno_win(GetLastError());
        return -1;
    }
    for (int attempt = 0; attempt < 100000; attempt++) {
        _snwprintf(name, MAX_PATH + 63, L"%lskst%lu_%u.tmp", dir, (unsigned long)GetCurrentProcessId(), ++counter);
        name[MAX_PATH + 63] = 0;
        if (GetFileAttributesW(name) == INVALID_FILE_ATTRIBUTES) {
            char *u = from_wide(name);
            if (!u) return -1;
            if (strlen(u) + 1 > size) {
                free(u);
                kst_errno = KST_ENAMETOOLONG;
                return -1;
            }
            strcpy(buf, u);
            free(u);
            return 0;
        }
    }
    kst_errno = KST_EEXIST;
    return -1;
}

int kst_os_exists(const char *path) {
    wchar_t *w = to_wide(path);
    if (!w) return 0;
    DWORD a = GetFileAttributesW(w);
    free(w);
    return a != INVALID_FILE_ATTRIBUTES;
}

char **kst_os_args(int argc, char **argv, int *out_argc) {
    (void)argc;
    (void)argv;
    const wchar_t *p = GetCommandLineW();
    size_t cap = 16, count = 0;
    char **args = (char **)malloc(sizeof(char *) * cap);
    wchar_t *tmp = (wchar_t *)malloc((wcslen(p) + 1) * sizeof(wchar_t));
    if (!args || !tmp) {
        *out_argc = argc;
        return argv;
    }
    size_t n = 0;
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"') tmp[n++] = *p++;
        if (*p) p++;
    } else {
        while (*p && *p != L' ' && *p != L'\t') tmp[n++] = *p++;
    }
    tmp[n] = 0;
    args[count++] = from_wide(tmp);
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
                    for (size_t i = 0; i < bs / 2; i++) tmp[n++] = L'\\';
                    if (bs % 2) {
                        tmp[n++] = L'"';
                        p++;
                    }
                } else {
                    for (size_t i = 0; i < bs; i++) tmp[n++] = L'\\';
                }
                continue;
            }
            if (*p == L'"') {
                if (quoted && p[1] == L'"') {
                    tmp[n++] = L'"';
                    p += 2;
                    continue;
                }
                quoted = !quoted;
                p++;
                continue;
            }
            tmp[n++] = *p++;
        }
        tmp[n] = 0;
        if (count + 2 > cap) {
            cap *= 2;
            char **grown = (char **)realloc(args, sizeof(char *) * cap);
            if (!grown) break;
            args = grown;
        }
        args[count++] = from_wide(tmp);
    }
    free(tmp);
    args[count] = NULL;
    *out_argc = (int)count;
    return args;
}

int kst_os_system(const char *cmd) {
    if (!cmd) return 1;
    size_t n = strlen(cmd);
    char *wrapped = (char *)malloc(n + 3);
    if (!wrapped) {
        kst_errno = KST_ENOMEM;
        return -1;
    }
    wrapped[0] = '"';
    memcpy(wrapped + 1, cmd, n);
    wrapped[n + 1] = '"';
    wrapped[n + 2] = 0;
    wchar_t *w = to_wide(wrapped);
    free(wrapped);
    if (!w) return -1;
    int r = _wsystem(w);
    free(w);
    return r;
}

char *kst_os_getenv(const char *name) {
    wchar_t *w = to_wide(name);
    if (!w) return NULL;
    DWORD n = GetEnvironmentVariableW(w, NULL, 0);
    if (n == 0) {
        free(w);
        return NULL;
    }
    wchar_t *val = (wchar_t *)malloc(sizeof(wchar_t) * n);
    if (!val) {
        free(w);
        return NULL;
    }
    GetEnvironmentVariableW(w, val, n);
    char *r = from_wide(val);
    free(w);
    free(val);
    return r;
}

#else
/* ================================================================== */
/* POSIX                                                               */
/* ================================================================== */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void set_errno_posix(int e) {
    switch (e) {
    case ENOENT: kst_errno = KST_ENOENT; break;
    case EPERM: kst_errno = KST_EPERM; break;
    case EACCES: kst_errno = KST_EACCES; break;
    case EEXIST: kst_errno = KST_EEXIST; break;
    case EBADF: kst_errno = KST_EBADF; break;
    case ENOMEM: kst_errno = KST_ENOMEM; break;
    case ENOSPC: kst_errno = KST_ENOSPC; break;
    case EMFILE:
    case ENFILE: kst_errno = KST_EMFILE; break;
    case EINVAL: kst_errno = KST_EINVAL; break;
    case EISDIR: kst_errno = KST_EISDIR; break;
    case ENOTDIR: kst_errno = KST_ENOTDIR; break;
    case ESPIPE: kst_errno = KST_ESPIPE; break;
    case EROFS: kst_errno = KST_EROFS; break;
    case EPIPE: kst_errno = KST_EPIPE; break;
    case EBUSY: kst_errno = KST_EBUSY; break;
    case EXDEV: kst_errno = KST_EXDEV; break;
    case ENOTEMPTY: kst_errno = KST_ENOTEMPTY; break;
    case ENAMETOOLONG: kst_errno = KST_ENAMETOOLONG; break;
    case EFBIG: kst_errno = KST_EFBIG; break;
    case EAGAIN: kst_errno = KST_EAGAIN; break;
    default: kst_errno = KST_EIO; break;
    }
}

void kst_os_init(void) {}

kst_handle kst_os_std_handle(int which) { return (kst_handle)which; }

int kst_os_is_console(kst_handle h) { return h >= 0 && isatty((int)h); }

int kst_os_is_seekable(kst_handle h) { return h >= 0 && lseek((int)h, 0, SEEK_CUR) != (off_t)-1; }

int kst_os_open(const char *path, int flags, kst_handle *out) {
    int of = 0;
    if ((flags & KO_READ) && (flags & KO_WRITE)) of = O_RDWR;
    else if (flags & KO_WRITE) of = O_WRONLY;
    else of = O_RDONLY;
    if (flags & KO_CREATE) of |= O_CREAT;
    if (flags & KO_TRUNC) of |= O_TRUNC;
    if (flags & KO_EXCL) of |= O_EXCL;
    if (flags & KO_APPEND) of |= O_APPEND;
#ifdef O_CLOEXEC
    of |= O_CLOEXEC;
#endif
    int fd;
    do {
        fd = open(path, of, 0666);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        set_errno_posix(errno);
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
        close(fd);
        kst_errno = KST_EISDIR;
        return -1;
    }
    *out = (kst_handle)fd;
    return 0;
}

long long kst_os_read(kst_handle h, void *buf, size_t n, int console) {
    (void)console;
    ssize_t r;
    do {
        r = read((int)h, buf, n);
    } while (r < 0 && errno == EINTR);
    if (r < 0) {
        set_errno_posix(errno);
        return -1;
    }
    return (long long)r;
}

long long kst_os_write(kst_handle h, const void *buf, size_t n, int console, kst_u8carry *carry) {
    (void)console;
    (void)carry;
    const char *p = (const char *)buf;
    size_t total = 0;
    while (total < n) {
        ssize_t r = write((int)h, p + total, n - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            set_errno_posix(errno);
            return total ? (long long)total : -1;
        }
        if (r == 0) break;
        total += (size_t)r;
    }
    return (long long)total;
}

long long kst_os_seek(kst_handle h, long long offset, int whence) {
    int w = whence == KST_SEEK_SET ? SEEK_SET : whence == KST_SEEK_CUR ? SEEK_CUR : SEEK_END;
    off_t r = lseek((int)h, (off_t)offset, w);
    if (r == (off_t)-1) {
        set_errno_posix(errno);
        return -1;
    }
    return (long long)r;
}

int kst_os_close(kst_handle h) {
    if (close((int)h) != 0) {
        set_errno_posix(errno);
        return -1;
    }
    return 0;
}

int kst_os_remove(const char *path) {
    if (remove(path) != 0) {
        set_errno_posix(errno);
        return -1;
    }
    return 0;
}

int kst_os_rename(const char *from, const char *to) {
    if (rename(from, to) != 0) {
        set_errno_posix(errno);
        return -1;
    }
    return 0;
}

static const char *tmp_dir(void) {
    const char *t = getenv("TMPDIR");
    return (t && *t) ? t : "/tmp";
}

int kst_os_tmpfile(kst_handle *out) {
    char path[KST_L_TMPNAM];
    snprintf(path, sizeof path, "%s/kstXXXXXX", tmp_dir());
    int fd = mkstemp(path);
    if (fd < 0) {
        set_errno_posix(errno);
        return -1;
    }
    unlink(path);
    *out = (kst_handle)fd;
    return 0;
}

int kst_os_tmpname(char *buf, size_t size) {
    static unsigned counter;
    for (int attempt = 0; attempt < 100000; attempt++) {
        int n = snprintf(buf, size, "%s/kst%ld_%u.tmp", tmp_dir(), (long)getpid(), ++counter);
        if (n < 0 || (size_t)n >= size) {
            kst_errno = KST_ENAMETOOLONG;
            return -1;
        }
        if (access(buf, F_OK) != 0) return 0;
    }
    kst_errno = KST_EEXIST;
    return -1;
}

int kst_os_exists(const char *path) { return access(path, F_OK) == 0; }

char **kst_os_args(int argc, char **argv, int *out_argc) {
    *out_argc = argc;
    return argv;
}

int kst_os_system(const char *cmd) {
    int r = system(cmd);
    if (!cmd) return r;
    if (r == -1) {
        set_errno_posix(errno);
        return -1;
    }
    if (WIFEXITED(r)) return WEXITSTATUS(r);
    return 128 + (WIFSIGNALED(r) ? WTERMSIG(r) : 0);
}

char *kst_os_getenv(const char *name) {
    const char *v = getenv(name);
    if (!v) return NULL;
    size_t n = strlen(v) + 1;
    char *r = (char *)malloc(n);
    if (r) memcpy(r, v, n);
    return r;
}
#endif
