/* kst_internal.h - private definitions shared by the runtime sources */
#ifndef KST_INTERNAL_H
#define KST_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "kasturi_rt.h"

typedef intptr_t kst_handle;
#define KST_BAD_HANDLE ((kst_handle)-1)

/* stream flags */
enum {
    KF_READ = 1 << 0,
    KF_WRITE = 1 << 1,
    KF_APPEND = 1 << 2,
    KF_TEXT = 1 << 3,      /* translate "\n" <-> "\r\n" (Windows text mode) */
    KF_CONSOLE = 1 << 4,   /* interactive terminal */
    KF_EOF = 1 << 5,
    KF_ERR = 1 << 6,
    KF_OWNBUF = 1 << 7,    /* buffer allocated by the library */
    KF_LASTREAD = 1 << 8,
    KF_LASTWRITE = 1 << 9,
    KF_STATIC = 1 << 10,   /* stdin / stdout / stderr: never freed */
    KF_OPEN = 1 << 11,
    KF_USERBUF = 1 << 12   /* buffer given by setvbuf */
};

/* partial UTF-8 sequence carried between console writes */
typedef struct kst_u8carry {
    unsigned char bytes[4];
    int n;
} kst_u8carry;

struct KFILE {
    kst_handle h;
    int flags;
    int bufmode;
    unsigned char *buf;
    size_t bufsize;
    size_t rpos, rend; /* read buffer: unread bytes are buf[rpos..rend) */
    size_t wlen;       /* write buffer: pending bytes are buf[0..wlen) */
    unsigned char unget[KST_UNGET_MAX];
    int nunget;
    kst_u8carry carry;
    int std_index;     /* 0,1,2 for the standard streams, -1 otherwise */
    struct KFILE *prev, *next;
};

/* ---- operating system layer (kst_os.c) ---- */

enum {
    KO_READ = 1,
    KO_WRITE = 2,
    KO_CREATE = 4,
    KO_TRUNC = 8,
    KO_EXCL = 16,
    KO_APPEND = 32
};

void kst_os_init(void);
kst_handle kst_os_std_handle(int which);
int kst_os_open(const char *path, int flags, kst_handle *out);
long long kst_os_read(kst_handle h, void *buf, size_t n, int console);
long long kst_os_write(kst_handle h, const void *buf, size_t n, int console, kst_u8carry *carry);
long long kst_os_seek(kst_handle h, long long offset, int whence);
int kst_os_close(kst_handle h);
int kst_os_is_console(kst_handle h);
int kst_os_is_seekable(kst_handle h);
int kst_os_remove(const char *path);
int kst_os_rename(const char *from, const char *to);
int kst_os_tmpfile(kst_handle *out);
int kst_os_tmpname(char *buf, size_t size);
int kst_os_exists(const char *path);
char **kst_os_args(int argc, char **argv, int *out_argc);
int kst_os_system(const char *cmd);
char *kst_os_getenv(const char *name);

/* ---- stdio internals (kst_stdio.c) ---- */

void kst_stdio_init(void);
void kst_flush_all(void);
/* writes raw bytes through the stream's buffering (with text translation) */
int kst__write_bytes(KFILE *f, const char *p, size_t n);
/* finishes a high-level write call (flushes unbuffered streams) */
void kst__end_write(KFILE *f);
int kst__begin_write(KFILE *f);

/* ---- formatted output engine (kst_printf.c) ---- */

typedef struct kst_sink {
    void (*write)(struct kst_sink *s, const char *p, size_t n);
    KFILE *file;
    char *buf;     /* string sinks */
    size_t cap;    /* capacity of buf including the terminating NUL */
    size_t used;
    int grow;      /* malloc'd growing buffer */
    long long total;
    int failed;
} kst_sink;

int kst_format(kst_sink *out, const char *fmt, va_list ap);

/* ---- formatted input engine (kst_scanf.c) ---- */

typedef struct kst_source {
    int (*get)(struct kst_source *s);
    void (*unget)(struct kst_source *s, int c);
    KFILE *file;
    const unsigned char *str;
    size_t pos;
    long long count; /* characters consumed so far (%n) */
} kst_source;

int kst_scan(kst_source *in, const char *fmt, va_list ap);

/* converts Kannada digits (೦-೯) in s to ASCII in place, returns s */
char *kst__ascii_digits(char *s);

#endif
