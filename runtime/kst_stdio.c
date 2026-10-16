/* kst_stdio.c - buffered streams: the heart of the Kasturi standard I/O library */
#include <stdlib.h>
#include <string.h>

#include "kst_internal.h"

static KFILE s_stdin, s_stdout, s_stderr;
KFILE *kst_stdin = &s_stdin;
KFILE *kst_stdout = &s_stdout;
KFILE *kst_stderr = &s_stderr;

static KFILE *open_list; /* streams returned by fopen / tmpfile */
static int stdio_ready;

/* ------------------------------------------------------------------ */
/* initialisation                                                      */
/* ------------------------------------------------------------------ */

static void init_std_stream(KFILE *f, int which) {
    memset(f, 0, sizeof *f);
    f->std_index = which;
    f->h = kst_os_std_handle(which);
    f->flags = KF_STATIC | KF_OPEN | (which == 0 ? KF_READ : KF_WRITE);
    if (kst_os_is_console(f->h)) f->flags |= KF_CONSOLE;
#ifdef _WIN32
    if (!(f->flags & KF_CONSOLE)) f->flags |= KF_TEXT;
#endif
    if (which == 2)
        f->bufmode = KST_IONBF;
    else if (f->flags & KF_CONSOLE)
        f->bufmode = KST_IOLBF;
    else
        f->bufmode = KST_IOFBF;
}

void kst_stdio_init(void) {
    if (stdio_ready) return;
    stdio_ready = 1;
    kst_os_init();
    init_std_stream(&s_stdin, 0);
    init_std_stream(&s_stdout, 1);
    init_std_stream(&s_stderr, 2);
}

#define ENSURE_READY()                       \
    do {                                     \
        if (!stdio_ready) kst_stdio_init();  \
    } while (0)

static int valid_stream(KFILE *f) {
    ENSURE_READY();
    if (!f || !(f->flags & KF_OPEN)) {
        kst_errno = KST_EBADF;
        return 0;
    }
    return 1;
}

static int ensure_buffer(KFILE *f) {
    if (f->buf) return 0;
    size_t size = f->bufmode == KST_IONBF ? 64 : KST_BUFSIZ;
    f->buf = (unsigned char *)malloc(size);
    if (!f->buf) {
        kst_errno = KST_ENOMEM;
        f->flags |= KF_ERR;
        return -1;
    }
    f->bufsize = size;
    f->flags |= KF_OWNBUF;
    return 0;
}

/* ------------------------------------------------------------------ */
/* writing                                                             */
/* ------------------------------------------------------------------ */

static int flush_write_buffer(KFILE *f) {
    if (f->wlen == 0) return 0;
    if (f->flags & KF_APPEND) kst_os_seek(f->h, 0, KST_SEEK_END);
    size_t off = 0;
    while (off < f->wlen) {
        long long w = kst_os_write(f->h, f->buf + off, f->wlen - off, (f->flags & KF_CONSOLE) != 0, &f->carry);
        if (w <= 0) {
            f->flags |= KF_ERR;
            /* keep unwritten data out of the way so the stream stays usable */
            f->wlen = 0;
            return KST_EOF;
        }
        off += (size_t)w;
    }
    f->wlen = 0;
    return 0;
}

/* discards read-ahead so that the OS position matches the logical one */
static void drop_read_buffer(KFILE *f) {
    long long ahead = (long long)(f->rend - f->rpos) + f->nunget;
    if (ahead > 0 && kst_os_is_seekable(f->h)) kst_os_seek(f->h, -ahead, KST_SEEK_CUR);
    f->rpos = f->rend = 0;
    f->nunget = 0;
}

int kst__begin_write(KFILE *f) {
    if (!valid_stream(f)) return -1;
    if (!(f->flags & KF_WRITE)) {
        kst_errno = KST_EBADF;
        f->flags |= KF_ERR;
        return -1;
    }
    if (f->flags & KF_LASTREAD) {
        drop_read_buffer(f);
        f->flags &= ~KF_LASTREAD;
    }
    f->flags |= KF_LASTWRITE;
    return ensure_buffer(f);
}

void kst__end_write(KFILE *f) {
    if (f->bufmode == KST_IONBF) flush_write_buffer(f);
}

static int put_raw(KFILE *f, unsigned char c) {
    if (f->wlen >= f->bufsize && flush_write_buffer(f) != 0) return KST_EOF;
    f->buf[f->wlen++] = c;
    if ((f->bufmode == KST_IOLBF && c == '\n') || f->wlen >= f->bufsize) {
        if (flush_write_buffer(f) != 0) return KST_EOF;
    }
    return c;
}

int kst__write_bytes(KFILE *f, const char *p, size_t n) {
    const unsigned char *s = (const unsigned char *)p;
    if (!(f->flags & KF_TEXT) && f->bufmode == KST_IOFBF && n >= f->bufsize) {
        /* large binary write: bypass the buffer */
        if (flush_write_buffer(f) != 0) return -1;
        if (f->flags & KF_APPEND) kst_os_seek(f->h, 0, KST_SEEK_END);
        long long w = kst_os_write(f->h, s, n, (f->flags & KF_CONSOLE) != 0, &f->carry);
        if (w < 0 || (size_t)w != n) {
            f->flags |= KF_ERR;
            return -1;
        }
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        if ((f->flags & KF_TEXT) && s[i] == '\n' && put_raw(f, '\r') == KST_EOF) return -1;
        if (put_raw(f, s[i]) == KST_EOF) return -1;
    }
    return 0;
}

int kst_fputc(int c, KFILE *f) {
    if (kst__begin_write(f) != 0) return KST_EOF;
    char ch = (char)c;
    if (kst__write_bytes(f, &ch, 1) != 0) return KST_EOF;
    kst__end_write(f);
    return (unsigned char)c;
}

int kst_putc(int c, KFILE *f) { return kst_fputc(c, f); }

int kst_putchar(int c) { return kst_fputc(c, kst_stdout); }

int kst_fputs(const char *s, KFILE *f) {
    if (kst__begin_write(f) != 0) return KST_EOF;
    if (!s) s = "(ಖಾಲಿ)";
    if (kst__write_bytes(f, s, strlen(s)) != 0) return KST_EOF;
    kst__end_write(f);
    return 0;
}

int kst_puts(const char *s) {
    KFILE *f = kst_stdout;
    if (kst__begin_write(f) != 0) return KST_EOF;
    if (!s) s = "(ಖಾಲಿ)";
    if (kst__write_bytes(f, s, strlen(s)) != 0 || kst__write_bytes(f, "\n", 1) != 0) return KST_EOF;
    kst__end_write(f);
    return 0;
}

int kst_fputcp(int cp, KFILE *f) {
    unsigned char b[4];
    unsigned u = (unsigned)cp;
    int n;
    if (u > 0x10FFFF || (u >= 0xD800 && u <= 0xDFFF)) u = 0xFFFD;
    if (u < 0x80) {
        b[0] = (unsigned char)u;
        n = 1;
    } else if (u < 0x800) {
        b[0] = (unsigned char)(0xC0 | (u >> 6));
        b[1] = (unsigned char)(0x80 | (u & 0x3F));
        n = 2;
    } else if (u < 0x10000) {
        b[0] = (unsigned char)(0xE0 | (u >> 12));
        b[1] = (unsigned char)(0x80 | ((u >> 6) & 0x3F));
        b[2] = (unsigned char)(0x80 | (u & 0x3F));
        n = 3;
    } else {
        b[0] = (unsigned char)(0xF0 | (u >> 18));
        b[1] = (unsigned char)(0x80 | ((u >> 12) & 0x3F));
        b[2] = (unsigned char)(0x80 | ((u >> 6) & 0x3F));
        b[3] = (unsigned char)(0x80 | (u & 0x3F));
        n = 4;
    }
    if (kst__begin_write(f) != 0) return KST_EOF;
    if (kst__write_bytes(f, (const char *)b, (size_t)n) != 0) return KST_EOF;
    kst__end_write(f);
    return (int)u;
}

long long kst_fwrite(const void *ptr, long long size, long long count, KFILE *f) {
    if (size <= 0 || count <= 0) return 0;
    if (kst__begin_write(f) != 0) return 0;
    const char *p = (const char *)ptr;
    if (!(f->flags & KF_TEXT)) {
        if (kst__write_bytes(f, p, (size_t)(size * count)) != 0) {
            kst__end_write(f);
            return 0;
        }
        kst__end_write(f);
        return count;
    }
    long long done = 0;
    for (; done < count; done++) {
        if (kst__write_bytes(f, p + done * size, (size_t)size) != 0) break;
    }
    kst__end_write(f);
    return done;
}

/* ------------------------------------------------------------------ */
/* reading                                                             */
/* ------------------------------------------------------------------ */

static void flush_line_buffered(void) {
    if ((s_stdout.flags & KF_OPEN) && (s_stdout.flags & KF_LASTWRITE) && s_stdout.wlen) flush_write_buffer(&s_stdout);
    for (KFILE *g = open_list; g; g = g->next)
        if (g->bufmode == KST_IOLBF && (g->flags & KF_LASTWRITE) && g->wlen) flush_write_buffer(g);
}

static int begin_read(KFILE *f) {
    if (!valid_stream(f)) return -1;
    if (!(f->flags & KF_READ)) {
        kst_errno = KST_EBADF;
        f->flags |= KF_ERR;
        return -1;
    }
    if (f->flags & KF_LASTWRITE) {
        if (flush_write_buffer(f) != 0) return -1;
        f->flags &= ~KF_LASTWRITE;
    }
    f->flags |= KF_LASTREAD;
    return ensure_buffer(f);
}

/* refills the read buffer keeping unread bytes; 1 = data, 0 = EOF, -1 = error */
static int fill(KFILE *f) {
    if ((f->flags & KF_CONSOLE) || f == &s_stdin) flush_line_buffered();
    size_t keep = f->rend - f->rpos;
    if (keep && f->rpos) memmove(f->buf, f->buf + f->rpos, keep);
    f->rpos = 0;
    f->rend = keep;
    size_t room = f->bufsize - keep;
    if (f->bufmode == KST_IONBF && room > 1 && !(f->flags & KF_CONSOLE)) room = 1;
    long long r = kst_os_read(f->h, f->buf + keep, room, (f->flags & KF_CONSOLE) != 0);
    if (r < 0) {
        f->flags |= KF_ERR;
        return -1;
    }
    if (r == 0) {
        f->flags |= KF_EOF;
        return 0;
    }
    f->rend += (size_t)r;
    return 1;
}

static int get_raw(KFILE *f) {
    if (f->rpos < f->rend) return f->buf[f->rpos++];
    if (fill(f) <= 0) return KST_EOF;
    return f->buf[f->rpos++];
}

/* one byte with text-mode translation; stream must be in read mode */
static int get_byte(KFILE *f) {
    if (f->nunget > 0) return f->unget[--f->nunget];
    if (f->flags & KF_EOF) return KST_EOF;
    int c = get_raw(f);
    if (c == '\r' && (f->flags & KF_TEXT)) {
        if (f->rpos >= f->rend) fill(f);
        if (f->rpos < f->rend && f->buf[f->rpos] == '\n') {
            f->rpos++;
            c = '\n';
        }
    }
    return c;
}

int kst_fgetc(KFILE *f) {
    if (begin_read(f) != 0) return KST_EOF;
    return get_byte(f);
}

int kst_getc(KFILE *f) { return kst_fgetc(f); }

int kst_getchar(void) { return kst_fgetc(kst_stdin); }

int kst_ungetc(int c, KFILE *f) {
    if (c == KST_EOF) return KST_EOF;
    if (begin_read(f) != 0) return KST_EOF;
    if (f->nunget >= KST_UNGET_MAX) return KST_EOF;
    f->unget[f->nunget++] = (unsigned char)c;
    f->flags &= ~KF_EOF;
    return (unsigned char)c;
}

char *kst_fgets(char *buf, int n, KFILE *f) {
    if (!buf || n <= 0) {
        kst_errno = KST_EINVAL;
        return NULL;
    }
    if (begin_read(f) != 0) return NULL;
    int i = 0;
    while (i < n - 1) {
        int c = get_byte(f);
        if (c == KST_EOF) break;
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0 && n > 1) return NULL;
    if ((f->flags & KF_ERR) && i == 0) return NULL;
    buf[i] = '\0';
    return buf;
}

char *kst_gets_s(char *buf, long long n) {
    KFILE *f = kst_stdin;
    if (!buf || n <= 0) {
        kst_errno = KST_EINVAL;
        return NULL;
    }
    if (begin_read(f) != 0) return NULL;
    long long i = 0;
    int c, any = 0;
    while ((c = get_byte(f)) != KST_EOF) {
        any = 1;
        if (c == '\n') break;
        if (i < n - 1) buf[i++] = (char)c;
    }
    if (!any) return NULL;
    buf[i] = '\0';
    return buf;
}

char *kst_getline(KFILE *f) {
    if (begin_read(f) != 0) return NULL;
    size_t cap = 128, len = 0;
    char *s = (char *)malloc(cap);
    if (!s) {
        kst_errno = KST_ENOMEM;
        return NULL;
    }
    int c, any = 0;
    while ((c = get_byte(f)) != KST_EOF) {
        any = 1;
        if (c == '\n') break;
        if (len + 1 >= cap) {
            cap *= 2;
            char *g = (char *)realloc(s, cap);
            if (!g) {
                free(s);
                kst_errno = KST_ENOMEM;
                return NULL;
            }
            s = g;
        }
        s[len++] = (char)c;
    }
    if (!any) {
        free(s);
        return NULL;
    }
    s[len] = '\0';
    return s;
}

int kst_fgetcp(KFILE *f) {
    int c = kst_fgetc(f);
    if (c == KST_EOF || c < 0x80) return c;
    int need;
    unsigned v, min;
    if (c >= 0xC2 && c <= 0xDF) {
        need = 1; v = (unsigned)c & 0x1F; min = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
        need = 2; v = (unsigned)c & 0x0F; min = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
        need = 3; v = (unsigned)c & 0x07; min = 0x10000;
    } else {
        return 0xFFFD;
    }
    for (int i = 0; i < need; i++) {
        int d = kst_fgetc(f);
        if (d == KST_EOF) return 0xFFFD;
        if ((d & 0xC0) != 0x80) {
            kst_ungetc(d, f);
            return 0xFFFD;
        }
        v = (v << 6) | ((unsigned)d & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 0xFFFD;
    return (int)v;
}

long long kst_fread(void *ptr, long long size, long long count, KFILE *f) {
    if (size <= 0 || count <= 0) return 0;
    if (begin_read(f) != 0) return 0;
    unsigned char *dst = (unsigned char *)ptr;
    long long total = size * count;
    long long got = 0;
    if (f->flags & KF_TEXT) {
        while (got < total) {
            int c = get_byte(f);
            if (c == KST_EOF) break;
            dst[got++] = (unsigned char)c;
        }
        return got / size;
    }
    while (got < total && f->nunget > 0) dst[got++] = f->unget[--f->nunget];
    while (got < total) {
        size_t avail = f->rend - f->rpos;
        if (avail > 0) {
            size_t take = (size_t)(total - got) < avail ? (size_t)(total - got) : avail;
            memcpy(dst + got, f->buf + f->rpos, take);
            f->rpos += take;
            got += (long long)take;
            continue;
        }
        if (f->flags & KF_EOF) break;
        if ((size_t)(total - got) >= f->bufsize && !(f->flags & KF_CONSOLE)) {
            long long r = kst_os_read(f->h, dst + got, (size_t)(total - got), 0);
            if (r < 0) {
                f->flags |= KF_ERR;
                break;
            }
            if (r == 0) {
                f->flags |= KF_EOF;
                break;
            }
            got += r;
            continue;
        }
        if (fill(f) <= 0) break;
    }
    return got / size;
}

/* ------------------------------------------------------------------ */
/* buffering control                                                   */
/* ------------------------------------------------------------------ */

int kst_fflush(KFILE *f) {
    ENSURE_READY();
    if (!f) {
        kst_flush_all();
        return 0;
    }
    if (!valid_stream(f)) return KST_EOF;
    if (f->flags & KF_LASTWRITE) return flush_write_buffer(f) == 0 ? 0 : KST_EOF;
    if ((f->flags & KF_LASTREAD) && kst_os_is_seekable(f->h)) drop_read_buffer(f);
    return 0;
}

void kst_flush_all(void) {
    if (!stdio_ready) return;
    if ((s_stdout.flags & KF_OPEN) && (s_stdout.flags & KF_LASTWRITE)) flush_write_buffer(&s_stdout);
    if ((s_stderr.flags & KF_OPEN) && (s_stderr.flags & KF_LASTWRITE)) flush_write_buffer(&s_stderr);
    for (KFILE *g = open_list; g; g = g->next)
        if (g->flags & KF_LASTWRITE) flush_write_buffer(g);
}

int kst_setvbuf(KFILE *f, char *buf, int mode, long long size) {
    if (!valid_stream(f)) return -1;
    if (mode != KST_IOFBF && mode != KST_IOLBF && mode != KST_IONBF) {
        kst_errno = KST_EINVAL;
        return -1;
    }
    if ((f->flags & (KF_LASTREAD | KF_LASTWRITE)) || f->wlen || f->rend) {
        kst_errno = KST_EINVAL; /* only allowed before any I/O */
        return -1;
    }
    if (f->flags & KF_OWNBUF) free(f->buf);
    f->buf = NULL;
    f->bufsize = 0;
    f->flags &= ~(KF_OWNBUF | KF_USERBUF);
    f->bufmode = mode;
    if (buf && size >= 2 && mode != KST_IONBF) {
        f->buf = (unsigned char *)buf;
        f->bufsize = (size_t)size;
        f->flags |= KF_USERBUF;
    } else if (size >= 2 && mode != KST_IONBF) {
        f->buf = (unsigned char *)malloc((size_t)size);
        if (!f->buf) {
            kst_errno = KST_ENOMEM;
            return -1;
        }
        f->bufsize = (size_t)size;
        f->flags |= KF_OWNBUF;
    }
    return 0;
}

void kst_setbuf(KFILE *f, char *buf) {
    kst_setvbuf(f, buf, buf ? KST_IOFBF : KST_IONBF, KST_BUFSIZ);
}

/* ------------------------------------------------------------------ */
/* opening and closing                                                 */
/* ------------------------------------------------------------------ */

/* parses an fopen mode; returns OS flags and stream flags */
static int parse_mode(const char *mode, int *os_flags, int *st_flags) {
    int of = 0, sf = 0;
    if (!mode) return -1;
    switch (mode[0]) {
    case 'r': of = KO_READ; sf = KF_READ; break;
    case 'w': of = KO_WRITE | KO_CREATE | KO_TRUNC; sf = KF_WRITE; break;
    case 'a': of = KO_WRITE | KO_CREATE | KO_APPEND; sf = KF_WRITE | KF_APPEND; break;
    default: return -1;
    }
    int binary = 0;
    for (const char *p = mode + 1; *p; p++) {
        switch (*p) {
        case '+': of |= KO_READ | KO_WRITE; sf |= KF_READ | KF_WRITE; break;
        case 'b': binary = 1; break;
        case 't': binary = 0; break;
        case 'x':
            if (mode[0] != 'w') return -1;
            of |= KO_EXCL;
            break;
        case ',': /* ignore options such as ",ccs=UTF-8" */
            p += strlen(p) - 1;
            break;
        default: return -1;
        }
    }
#ifdef _WIN32
    if (!binary) sf |= KF_TEXT;
#else
    (void)binary;
#endif
    *os_flags = of;
    *st_flags = sf;
    return 0;
}

static void link_stream(KFILE *f) {
    f->prev = NULL;
    f->next = open_list;
    if (open_list) open_list->prev = f;
    open_list = f;
}

static void unlink_stream(KFILE *f) {
    if (f->flags & KF_STATIC) return;
    if (f->prev) f->prev->next = f->next;
    else if (open_list == f) open_list = f->next;
    if (f->next) f->next->prev = f->prev;
    f->prev = f->next = NULL;
}

static KFILE *new_stream(kst_handle h, int st_flags) {
    KFILE *f = (KFILE *)calloc(1, sizeof(KFILE));
    if (!f) {
        kst_errno = KST_ENOMEM;
        return NULL;
    }
    f->h = h;
    f->std_index = -1;
    f->flags = st_flags | KF_OPEN;
    if (kst_os_is_console(h)) {
        f->flags |= KF_CONSOLE;
        f->flags &= ~KF_TEXT;
        f->bufmode = KST_IOLBF;
    } else {
        f->bufmode = KST_IOFBF;
    }
    link_stream(f);
    return f;
}

KFILE *kst_fopen(const char *path, const char *mode) {
    ENSURE_READY();
    int of, sf;
    if (!path || !*path) {
        kst_errno = KST_ENOENT;
        return NULL;
    }
    if (parse_mode(mode, &of, &sf) != 0) {
        kst_errno = KST_EINVAL;
        return NULL;
    }
    kst_handle h;
    if (kst_os_open(path, of, &h) != 0) return NULL;
    KFILE *f = new_stream(h, sf);
    if (!f) kst_os_close(h);
    return f;
}

KFILE *kst_tmpfile(void) {
    ENSURE_READY();
    kst_handle h;
    if (kst_os_tmpfile(&h) != 0) return NULL;
    KFILE *f = new_stream(h, KF_READ | KF_WRITE);
    if (!f) kst_os_close(h);
    return f;
}

static int close_stream_handle(KFILE *f) {
    int r = 0;
    if ((f->flags & KF_LASTWRITE) && flush_write_buffer(f) != 0) r = KST_EOF;
    if (f->h != KST_BAD_HANDLE && kst_os_close(f->h) != 0) r = KST_EOF;
    f->h = KST_BAD_HANDLE;
    if (f->flags & KF_OWNBUF) free(f->buf);
    f->buf = NULL;
    f->bufsize = 0;
    f->rpos = f->rend = f->wlen = 0;
    f->nunget = 0;
    f->carry.n = 0;
    return r;
}

int kst_fclose(KFILE *f) {
    if (!valid_stream(f)) return KST_EOF;
    int r = close_stream_handle(f);
    unlink_stream(f);
    if (f->flags & KF_STATIC) {
        f->flags = KF_STATIC;
    } else {
        f->flags = 0;
        free(f);
    }
    return r;
}

KFILE *kst_freopen(const char *path, const char *mode, KFILE *f) {
    ENSURE_READY();
    if (!f) {
        kst_errno = KST_EBADF;
        return NULL;
    }
    int of, sf;
    if (parse_mode(mode, &of, &sf) != 0) {
        kst_errno = KST_EINVAL;
        return NULL;
    }
    if (!path) {
        /* change the mode of the open stream (binary <-> text) */
        if (!(f->flags & KF_OPEN)) {
            kst_errno = KST_EBADF;
            return NULL;
        }
        kst_fflush(f);
        f->flags = (f->flags & ~KF_TEXT) | (sf & KF_TEXT);
        if (f->flags & KF_CONSOLE) f->flags &= ~KF_TEXT;
        return f;
    }
    if (f->flags & KF_OPEN) close_stream_handle(f);
    kst_handle h;
    if (kst_os_open(path, of, &h) != 0) {
        unlink_stream(f);
        if (f->flags & KF_STATIC) f->flags = KF_STATIC;
        else {
            f->flags = 0;
            free(f);
        }
        return NULL;
    }
    int keep_static = f->flags & KF_STATIC;
    f->h = h;
    f->flags = sf | KF_OPEN | keep_static;
    f->bufmode = KST_IOFBF;
    if (kst_os_is_console(h)) {
        f->flags |= KF_CONSOLE;
        f->flags &= ~KF_TEXT;
        f->bufmode = KST_IOLBF;
    }
    return f;
}

/* ------------------------------------------------------------------ */
/* positioning                                                         */
/* ------------------------------------------------------------------ */

long long kst_ftell(KFILE *f) {
    if (!valid_stream(f)) return -1;
    if ((f->flags & KF_LASTWRITE) && (f->flags & KF_APPEND) && flush_write_buffer(f) != 0) return -1;
    long long pos = kst_os_seek(f->h, 0, KST_SEEK_CUR);
    if (pos < 0) return -1;
    if (f->flags & KF_LASTWRITE) pos += (long long)f->wlen;
    else if (f->flags & KF_LASTREAD) pos -= (long long)(f->rend - f->rpos) + f->nunget;
    return pos < 0 ? 0 : pos;
}

int kst_fseek(KFILE *f, long long offset, int whence) {
    if (!valid_stream(f)) return -1;
    if (whence != KST_SEEK_SET && whence != KST_SEEK_CUR && whence != KST_SEEK_END) {
        kst_errno = KST_EINVAL;
        return -1;
    }
    if (!kst_os_is_seekable(f->h)) {
        kst_errno = KST_ESPIPE;
        return -1;
    }
    if ((f->flags & KF_LASTWRITE) && flush_write_buffer(f) != 0) return -1;
    if (whence == KST_SEEK_CUR && (f->flags & KF_LASTREAD))
        offset -= (long long)(f->rend - f->rpos) + f->nunget;
    if (kst_os_seek(f->h, offset, whence) < 0) return -1;
    f->rpos = f->rend = 0;
    f->nunget = 0;
    f->flags &= ~(KF_EOF | KF_LASTREAD | KF_LASTWRITE);
    return 0;
}

int kst_fgetpos(KFILE *f, kst_fpos *pos) {
    if (!pos) {
        kst_errno = KST_EINVAL;
        return -1;
    }
    long long p = kst_ftell(f);
    if (p < 0) return -1;
    pos->pos = p;
    return 0;
}

int kst_fsetpos(KFILE *f, const kst_fpos *pos) {
    if (!pos) {
        kst_errno = KST_EINVAL;
        return -1;
    }
    return kst_fseek(f, pos->pos, KST_SEEK_SET);
}

void kst_rewind(KFILE *f) {
    if (kst_fseek(f, 0, KST_SEEK_SET) == 0 || valid_stream(f)) kst_clearerr(f);
}

/* ------------------------------------------------------------------ */
/* errors                                                              */
/* ------------------------------------------------------------------ */

void kst_clearerr(KFILE *f) {
    if (f) f->flags &= ~(KF_EOF | KF_ERR);
}

int kst_feof(KFILE *f) { return f && (f->flags & KF_EOF) && f->nunget == 0 ? 1 : 0; }

int kst_ferror(KFILE *f) { return f && (f->flags & KF_ERR) ? 1 : 0; }

void kst_perror(const char *s) {
    int e = kst_errno;
    KFILE *f = kst_stderr;
    if (kst__begin_write(f) != 0) return;
    if (s && *s) {
        kst__write_bytes(f, s, strlen(s));
        kst__write_bytes(f, ": ", 2);
    }
    const char *msg = kst_strerror(e);
    kst__write_bytes(f, msg, strlen(msg));
    kst__write_bytes(f, "\n", 1);
    flush_write_buffer(f);
}

/* ------------------------------------------------------------------ */
/* operations on files                                                 */
/* ------------------------------------------------------------------ */

int kst_remove(const char *path) {
    if (!path) {
        kst_errno = KST_EINVAL;
        return -1;
    }
    return kst_os_remove(path);
}

int kst_rename(const char *from, const char *to) {
    if (!from || !to) {
        kst_errno = KST_EINVAL;
        return -1;
    }
    return kst_os_rename(from, to);
}

char *kst_tmpnam(char *buf) {
    static char internal[KST_L_TMPNAM];
    char *target = buf ? buf : internal;
    if (kst_os_tmpname(target, KST_L_TMPNAM) != 0) return NULL;
    return target;
}

int kst_file_exists(const char *path) { return path && kst_os_exists(path); }

int kst_fileno_is_console(KFILE *f) { return valid_stream(f) && (f->flags & KF_CONSOLE) ? 1 : 0; }
