/* kst_core.c - program start-up, helpers used by generated code, error
 * messages, and the general / string / math library wrappers bound by
 * lib/ಸಾಮಾನ್ಯ.kst, lib/ಪದಗಳು.kst and lib/ಗಣಿತ.kst.
 */
#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kst_internal.h"

/* ------------------------------------------------------------------ */
/* error messages                                                      */
/* ------------------------------------------------------------------ */

const char *kst_strerror(int e) {
    switch (e) {
    case KST_EOK: return "ದೋಷವಿಲ್ಲ";
    case KST_EPERM: return "ಅನುಮತಿ ಇಲ್ಲ";
    case KST_ENOENT: return "ಕಡತ ಅಥವಾ ಕೋಶ ಇಲ್ಲ";
    case KST_EIO: return "ಒಳಹರಿವು/ಹೊರಹರಿವು ದೋಷ";
    case KST_EBADF: return "ಅಮಾನ್ಯ ಕಡತ ಹರಿವು";
    case KST_EAGAIN: return "ಸಂಪನ್ಮೂಲ ತಾತ್ಕಾಲಿಕವಾಗಿ ಲಭ್ಯವಿಲ್ಲ";
    case KST_ENOMEM: return "ಸ್ಮರಣೆ ಸಾಕಾಗುತ್ತಿಲ್ಲ";
    case KST_EACCES: return "ಪ್ರವೇಶ ನಿರಾಕರಿಸಲಾಗಿದೆ";
    case KST_EBUSY: return "ಸಂಪನ್ಮೂಲ ಕಾರ್ಯನಿರತವಾಗಿದೆ";
    case KST_EEXIST: return "ಕಡತ ಈಗಾಗಲೇ ಇದೆ";
    case KST_EXDEV: return "ಬೇರೆ ಸಾಧನಕ್ಕೆ ಸರಿಸಲು ಸಾಧ್ಯವಿಲ್ಲ";
    case KST_ENOTDIR: return "ಕೋಶವಲ್ಲ";
    case KST_EISDIR: return "ಇದು ಒಂದು ಕೋಶ";
    case KST_EINVAL: return "ಅಮಾನ್ಯ ಚರಾಂಶ";
    case KST_EMFILE: return "ಹಲವಾರು ಕಡತಗಳು ತೆರೆದಿವೆ";
    case KST_EFBIG: return "ಕಡತ ತುಂಬಾ ದೊಡ್ಡದು";
    case KST_ENOSPC: return "ಸಾಧನದಲ್ಲಿ ಸ್ಥಳವಿಲ್ಲ";
    case KST_ESPIPE: return "ಈ ಹರಿವಿನಲ್ಲಿ ಸ್ಥಾನ ಬದಲಿಸಲಾಗದು";
    case KST_EROFS: return "ಓದಲು-ಮಾತ್ರ ಕಡತ ವ್ಯವಸ್ಥೆ";
    case KST_EPIPE: return "ಮುರಿದ ಕೊಳವೆ";
    case KST_EDOM: return "ಗಣಿತ ವ್ಯಾಪ್ತಿಯ ಹೊರಗಿನ ಚರಾಂಶ";
    case KST_ERANGE: return "ಫಲಿತಾಂಶ ವ್ಯಾಪ್ತಿಯ ಹೊರಗಿದೆ";
    case KST_ENAMETOOLONG: return "ಕಡತದ ಹೆಸರು ತುಂಬಾ ಉದ್ದವಾಗಿದೆ";
    case KST_ENOTEMPTY: return "ಕೋಶ ಖಾಲಿಯಿಲ್ಲ";
    case KST_EOVERFLOW: return "ಮೌಲ್ಯ ತುಂಬಾ ದೊಡ್ಡದು";
    case KST_EILSEQ: return "ಅಮಾನ್ಯ ಅಕ್ಷರ ಅನುಕ್ರಮ";
    default: return "ಅಜ್ಞಾತ ದೋಷ";
    }
}

/* ------------------------------------------------------------------ */
/* program start-up                                                    */
/* ------------------------------------------------------------------ */

static int g_argc;
static char **g_argv;

void kst_rt_init(int argc, char **argv) {
    kst_stdio_init();
    int n = argc;
    char **v = kst_os_args(argc, argv, &n);
    g_argc = v ? n : argc;
    g_argv = v ? v : argv;
    atexit(kst_flush_all);
}

void kst_rt_finish(void) { kst_flush_all(); }

int kst_rt_argc(void) { return g_argc; }

char **kst_rt_argv(void) { return g_argv; }

/* ------------------------------------------------------------------ */
/* ಮುದ್ರಿಸು helpers                                                    */
/* ------------------------------------------------------------------ */

void kst_print_i64(long long v) { kst_fprintf(kst_stdout, "%lld", v); }

/* Shortest text that reads back as the same double. Always shows a
 * decimal point ("5.0"); exponent form only for very large/small values. */
static void format_double(double v, char *out, size_t cap) {
    if (isnan(v)) {
        kst_snprintf(out, (long long)cap, "nan");
        return;
    }
    if (isinf(v)) {
        kst_snprintf(out, (long long)cap, v > 0 ? "inf" : "-inf");
        return;
    }
    char buf[48];
    int prec;
    for (prec = 1; prec <= 17; prec++) {
        kst_snprintf(buf, sizeof buf, "%.*e", prec - 1, v);
        if (strtod(buf, NULL) == v) break;
    }
    if (prec > 17) prec = 17;
    const char *e = strchr(buf, 'e');
    int exp10 = e ? atoi(e + 1) : 0;
    if (exp10 >= -5 && exp10 < 16) {
        int decimals = prec - 1 - exp10;
        if (decimals < 1) decimals = 1;
        kst_snprintf(out, (long long)cap, "%.*f", decimals, v);
    } else {
        kst_snprintf(out, (long long)cap, "%s", buf);
    }
}

void kst_print_f64(double v) {
    char buf[64];
    format_double(v, buf, sizeof buf);
    kst_fputs(buf, kst_stdout);
}

void kst_print_str(const char *s) { kst_fputs(s ? s : "(ಖಾಲಿ)", kst_stdout); }

void kst_print_char(int c) { kst_fputc((unsigned char)c, kst_stdout); }

void kst_print_bool(int b) { kst_fputs(b ? "ಸತ್ಯ" : "ಅಸತ್ಯ", kst_stdout); }

void kst_print_ptr(const void *p) { kst_fprintf(kst_stdout, "%p", p); }

void kst_print_newline(void) { kst_fputc('\n', kst_stdout); }

/* ------------------------------------------------------------------ */
/* ಓದು helpers - each returns 1 on success, 0 on bad input, -1 at EOF */
/* ------------------------------------------------------------------ */

static int scan_result(int r) { return r == 1 ? 1 : (r == KST_EOF ? -1 : 0); }

/* after a failed conversion, drop the offending token so the program
 * does not loop forever on the same input */
static void skip_bad_token(void) {
    int c;
    while ((c = kst_fgetc(kst_stdin)) != KST_EOF && c != ' ' && c != '\t' && c != '\n' && c != '\r')
        ;
}

int kst_read_i32(int *p) {
    int r = scan_result(kst_fscanf(kst_stdin, "%d", p));
    if (r == 0) skip_bad_token();
    return r;
}

int kst_read_i64(long long *p) {
    int r = scan_result(kst_fscanf(kst_stdin, "%lld", p));
    if (r == 0) skip_bad_token();
    return r;
}

int kst_read_f64(double *p) {
    int r = scan_result(kst_fscanf(kst_stdin, "%lf", p));
    if (r == 0) skip_bad_token();
    return r;
}

int kst_read_char(char *p) { return scan_result(kst_fscanf(kst_stdin, " %c", p)); }

int kst_read_bool(kst_bool *p) {
    char word[64];
    int r = kst_fscanf(kst_stdin, "%63s", word);
    if (r != 1) return scan_result(r);
    if (strcmp(word, "ಸತ್ಯ") == 0 || strcmp(word, "1") == 0 || strcmp(word, "೧") == 0 ||
        strcmp(word, "true") == 0 || strcmp(word, "ಹೌದು") == 0) {
        *p = 1;
        return 1;
    }
    if (strcmp(word, "ಅಸತ್ಯ") == 0 || strcmp(word, "0") == 0 || strcmp(word, "೦") == 0 ||
        strcmp(word, "false") == 0 || strcmp(word, "ಇಲ್ಲ") == 0) {
        *p = 0;
        return 1;
    }
    return 0;
}

int kst_read_word(char *buf, long long size) {
    if (!buf || size <= 1) return 0;
    char fmt[32];
    kst_snprintf(fmt, sizeof fmt, "%%%llds", size - 1);
    return scan_result(kst_fscanf(kst_stdin, fmt, buf));
}

char *kst_read_word_alloc(void) {
    int c;
    do c = kst_fgetc(kst_stdin);
    while (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f');
    if (c == KST_EOF) return NULL;
    size_t cap = 32, n = 0;
    char *s = (char *)malloc(cap);
    if (!s) return NULL;
    while (c != KST_EOF && c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\v' && c != '\f') {
        if (n + 1 >= cap) {
            cap *= 2;
            char *t = (char *)realloc(s, cap);
            if (!t) {
                free(s);
                return NULL;
            }
            s = t;
        }
        s[n++] = (char)c;
        c = kst_fgetc(kst_stdin);
    }
    if (c != KST_EOF) kst_ungetc(c, kst_stdin);
    s[n] = '\0';
    return s;
}

int kst_str_cmp(const char *a, const char *b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    int r = strcmp(a, b);
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

/* ------------------------------------------------------------------ */
/* ಸಾಮಾನ್ಯ (general utilities)                                         */
/* ------------------------------------------------------------------ */

void *kst_malloc(long long n) { return n < 0 ? NULL : malloc((size_t)(n ? n : 1)); }
void *kst_calloc(long long count, long long size) {
    if (count < 0 || size < 0) return NULL;
    return calloc((size_t)(count ? count : 1), (size_t)(size ? size : 1));
}
void *kst_realloc(void *p, long long n) { return n < 0 ? NULL : realloc(p, (size_t)(n ? n : 1)); }
void kst_free(void *p) { free(p); }
void *kst_memcpy(void *d, const void *s, long long n) { return n > 0 ? memcpy(d, s, (size_t)n) : d; }
void *kst_memmove(void *d, const void *s, long long n) { return n > 0 ? memmove(d, s, (size_t)n) : d; }
void *kst_memset(void *d, int c, long long n) { return n > 0 ? memset(d, c, (size_t)n) : d; }
int kst_memcmp(const void *a, const void *b, long long n) {
    if (n <= 0) return 0;
    int r = memcmp(a, b, (size_t)n);
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

void kst_exit(int code) {
    kst_flush_all();
    exit(code);
}
void kst_abort(void) {
    kst_flush_all();
    abort();
}
int kst_system(const char *cmd) {
    kst_flush_all();
    return kst_os_system(cmd);
}
char *kst_getenv(const char *name) { return name ? kst_os_getenv(name) : NULL; }

static char *digits_copy(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *c = (char *)malloc(n);
    if (!c) return NULL;
    memcpy(c, s, n);
    return kst__ascii_digits(c);
}

long long kst_strtoll(const char *s, char **end, int base) {
    char *c = digits_copy(s);
    if (!c) {
        if (end) *end = (char *)s;
        return 0;
    }
    char *e;
    long long v = strtoll(c, &e, base);
    if (end) {
        /* map the end position back onto the original string */
        const char *o = s;
        const char *q = c;
        while (q < e) {
            if ((unsigned char)o[0] == 0xE0 && (unsigned char)o[1] == 0xB3 && (unsigned char)o[2] >= 0xA6 &&
                (unsigned char)o[2] <= 0xAF)
                o += 3;
            else
                o++;
            q++;
        }
        *end = (char *)o;
    }
    free(c);
    return v;
}

double kst_strtod(const char *s, char **end) {
    char *c = digits_copy(s);
    if (!c) {
        if (end) *end = (char *)s;
        return 0;
    }
    char *e;
    double v = strtod(c, &e);
    if (end) {
        const char *o = s;
        const char *q = c;
        while (q < e) {
            if ((unsigned char)o[0] == 0xE0 && (unsigned char)o[1] == 0xB3 && (unsigned char)o[2] >= 0xA6 &&
                (unsigned char)o[2] <= 0xAF)
                o += 3;
            else
                o++;
            q++;
        }
        *end = (char *)o;
    }
    free(c);
    return v;
}

int kst_atoi(const char *s) { return (int)kst_strtoll(s, NULL, 10); }
long long kst_atoll(const char *s) { return kst_strtoll(s, NULL, 10); }
double kst_atof(const char *s) { return kst_strtod(s, NULL); }

char *kst_int_to_str(long long v) { return kst_aprintf("%lld", v); }
char *kst_int_to_str_kn(long long v) { return kst_aprintf("%~lld", v); }
char *kst_float_to_str(double v) {
    char buf[64];
    format_double(v, buf, sizeof buf);
    return kst_strdup(buf);
}

int kst_abs(int v) { return v < 0 ? -v : v; }
long long kst_llabs(long long v) { return v < 0 ? -v : v; }
int kst_min(int a, int b) { return a < b ? a : b; }
int kst_max(int a, int b) { return a > b ? a : b; }
long long kst_llmin(long long a, long long b) { return a < b ? a : b; }
long long kst_llmax(long long a, long long b) { return a > b ? a : b; }

/* xorshift64* - better than most C library rand() implementations */
static unsigned long long g_rng = 0x9E3779B97F4A7C15ULL;
static unsigned long long rng_next(void) {
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * 0x2545F4914F6CDD1DULL;
}
int kst_rand(void) { return (int)(rng_next() >> 33); }
void kst_srand(long long seed) {
    g_rng = (unsigned long long)seed * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL;
    if (!g_rng) g_rng = 1;
}
int kst_rand_range(int lo, int hi) {
    if (hi <= lo) return lo;
    unsigned long long span = (unsigned long long)((long long)hi - lo + 1);
    return (int)(lo + (long long)(rng_next() % span));
}
double kst_rand_unit(void) { return (double)(rng_next() >> 11) * (1.0 / 9007199254740992.0); }

long long kst_time(void) { return (long long)time(NULL); }
double kst_clock(void) { return (double)clock() / CLOCKS_PER_SEC; }

/* ------------------------------------------------------------------ */
/* ಪದಗಳು (strings) - all lengths are in bytes unless named utf8       */
/* ------------------------------------------------------------------ */

long long kst_strlen(const char *s) { return s ? (long long)strlen(s) : 0; }
char *kst_strcpy(char *d, const char *s) { return strcpy(d, s); }
char *kst_strncpy(char *d, const char *s, long long n) { return n > 0 ? strncpy(d, s, (size_t)n) : d; }
char *kst_strcat(char *d, const char *s) { return strcat(d, s); }
char *kst_strncat(char *d, const char *s, long long n) { return n > 0 ? strncat(d, s, (size_t)n) : d; }
int kst_strcmp(const char *a, const char *b) { return kst_str_cmp(a, b); }
int kst_strncmp(const char *a, const char *b, long long n) {
    if (n <= 0) return 0;
    if (!a || !b) return kst_str_cmp(a, b);
    int r = strncmp(a, b, (size_t)n);
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
}
char *kst_strchr(const char *s, int c) { return s ? (char *)strchr(s, c) : NULL; }
char *kst_strrchr(const char *s, int c) { return s ? (char *)strrchr(s, c) : NULL; }
char *kst_strstr(const char *h, const char *n) { return (h && n) ? (char *)strstr(h, n) : NULL; }
char *kst_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

long long kst_str_find(const char *h, const char *n) {
    const char *p = kst_strstr(h, n);
    return p ? (long long)(p - h) : -1;
}

kst_bool kst_str_equal(const char *a, const char *b) { return kst_str_cmp(a, b) == 0; }

kst_bool kst_str_starts(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

kst_bool kst_str_ends(const char *s, const char *suffix) {
    if (!s || !suffix) return 0;
    size_t a = strlen(s), b = strlen(suffix);
    return b <= a && memcmp(s + a - b, suffix, b) == 0;
}

char *kst_concat(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t x = strlen(a), y = strlen(b);
    char *r = (char *)malloc(x + y + 1);
    if (!r) return NULL;
    memcpy(r, a, x);
    memcpy(r + x, b, y + 1);
    return r;
}

/* byte length of the UTF-8 sequence starting with c */
static size_t u8_len(unsigned char c) {
    if (c < 0x80) return 1;
    if (c >= 0xF0) return 4;
    if (c >= 0xE0) return 3;
    if (c >= 0xC0) return 2;
    return 1;
}

static const char *u8_advance(const char *s, long long count) {
    while (count > 0 && *s) {
        size_t n = u8_len((unsigned char)*s);
        while (n > 0 && *s) {
            s++;
            n--;
        }
        count--;
    }
    return s;
}

long long kst_utf8_len(const char *s) {
    if (!s) return 0;
    long long n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if ((*p & 0xC0) != 0x80) n++;
    return n;
}

/* substring by character (code point) positions, malloc'd */
char *kst_substr(const char *s, long long start, long long count) {
    if (!s) return NULL;
    if (start < 0) start = 0;
    const char *a = u8_advance(s, start);
    const char *b = count < 0 ? a + strlen(a) : u8_advance(a, count);
    size_t n = (size_t)(b - a);
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, a, n);
    r[n] = '\0';
    return r;
}

/* code point at character position i, or -1 */
int kst_utf8_at(const char *s, long long i) {
    if (!s || i < 0) return -1;
    const unsigned char *p = (const unsigned char *)u8_advance(s, i);
    if (!*p) return -1;
    size_t n = u8_len(*p);
    int cp = n == 1 ? p[0] : n == 2 ? (p[0] & 0x1F) : n == 3 ? (p[0] & 0x0F) : (p[0] & 0x07);
    for (size_t k = 1; k < n; k++) {
        if ((p[k] & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (p[k] & 0x3F);
    }
    return cp;
}

char *kst_str_reverse(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    size_t w = n;
    const char *p = s;
    while (*p) {
        size_t k = u8_len((unsigned char)*p);
        if (k > w) k = w;
        w -= k;
        memcpy(r + w, p, k);
        p += k;
    }
    r[n] = '\0';
    return r;
}

char *kst_str_upper(const char *s) {
    char *r = kst_strdup(s);
    if (r)
        for (char *p = r; *p; p++)
            if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    return r;
}

char *kst_str_lower(const char *s) {
    char *r = kst_strdup(s);
    if (r)
        for (char *p = r; *p; p++)
            if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
    return r;
}

static int space_byte(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

char *kst_str_trim(const char *s) {
    if (!s) return NULL;
    while (space_byte((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && space_byte((unsigned char)s[n - 1])) n--;
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

char *kst_str_repeat(const char *s, int times) {
    if (!s || times < 0) times = 0;
    size_t n = s ? strlen(s) : 0;
    char *r = (char *)malloc(n * (size_t)times + 1);
    if (!r) return NULL;
    for (int i = 0; i < times; i++) memcpy(r + n * (size_t)i, s, n);
    r[n * (size_t)times] = '\0';
    return r;
}

char *kst_str_replace(const char *s, const char *from, const char *to) {
    if (!s) return NULL;
    if (!from || !*from) return kst_strdup(s);
    if (!to) to = "";
    size_t fl = strlen(from), tl = strlen(to), count = 0;
    for (const char *p = strstr(s, from); p; p = strstr(p + fl, from)) count++;
    size_t n = strlen(s) + count * tl - count * fl;
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    char *w = r;
    const char *p = s;
    const char *q;
    while ((q = strstr(p, from)) != NULL) {
        memcpy(w, p, (size_t)(q - p));
        w += q - p;
        memcpy(w, to, tl);
        w += tl;
        p = q + fl;
    }
    strcpy(w, p);
    return r;
}

/* character classes (ASCII; Kannada helpers work on code points) */
kst_bool kst_isdigit(int c) { return c >= '0' && c <= '9'; }
kst_bool kst_isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
kst_bool kst_isalnum(int c) { return kst_isdigit(c) || kst_isalpha(c); }
kst_bool kst_isspace(int c) { return space_byte((unsigned char)c) && c >= 0 && c < 256; }
kst_bool kst_isupper(int c) { return c >= 'A' && c <= 'Z'; }
kst_bool kst_islower(int c) { return c >= 'a' && c <= 'z'; }
kst_bool kst_isxdigit(int c) { return kst_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
kst_bool kst_ispunct(int c) { return c > 32 && c < 127 && !kst_isalnum(c); }
kst_bool kst_iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
kst_bool kst_isprint(int c) { return c >= 32 && c < 127; }
int kst_toupper(int c) { return kst_islower(c) ? c - 32 : c; }
int kst_tolower(int c) { return kst_isupper(c) ? c + 32 : c; }
kst_bool kst_is_kannada(int cp) { return cp >= 0x0C80 && cp <= 0x0CFF; }
kst_bool kst_is_kannada_digit(int cp) { return cp >= 0x0CE6 && cp <= 0x0CEF; }
int kst_digit_value(int cp) {
    if (cp >= '0' && cp <= '9') return cp - '0';
    if (cp >= 0x0CE6 && cp <= 0x0CEF) return cp - 0x0CE6;
    return -1;
}

/* ------------------------------------------------------------------ */
/* ಗಣಿತ (mathematics)                                                  */
/* ------------------------------------------------------------------ */

double kst_sqrt(double x) { return sqrt(x); }
double kst_cbrt(double x) { return cbrt(x); }
double kst_pow(double x, double y) { return pow(x, y); }
double kst_exp(double x) { return exp(x); }
double kst_log(double x) { return log(x); }
double kst_log10(double x) { return log10(x); }
double kst_log2(double x) { return log2(x); }
double kst_sin(double x) { return sin(x); }
double kst_cos(double x) { return cos(x); }
double kst_tan(double x) { return tan(x); }
double kst_asin(double x) { return asin(x); }
double kst_acos(double x) { return acos(x); }
double kst_atan(double x) { return atan(x); }
double kst_atan2(double y, double x) { return atan2(y, x); }
double kst_sinh(double x) { return sinh(x); }
double kst_cosh(double x) { return cosh(x); }
double kst_tanh(double x) { return tanh(x); }
double kst_fabs(double x) { return fabs(x); }
double kst_floor(double x) { return floor(x); }
double kst_ceil(double x) { return ceil(x); }
double kst_round(double x) { return round(x); }
double kst_trunc(double x) { return trunc(x); }
double kst_fmod(double x, double y) { return fmod(x, y); }
double kst_hypot(double x, double y) { return hypot(x, y); }
double kst_fmin(double a, double b) { return fmin(a, b); }
double kst_fmax(double a, double b) { return fmax(a, b); }
kst_bool kst_isnan(double x) { return isnan(x) != 0; }
kst_bool kst_isinf(double x) { return isinf(x) != 0; }
long long kst_ipow(long long b, int e) {
    long long r = 1;
    if (e < 0) return b == 1 ? 1 : (b == -1 ? ((e & 1) ? -1 : 1) : 0);
    while (e) {
        if (e & 1) r *= b;
        b *= b;
        e >>= 1;
    }
    return r;
}
long long kst_gcd(long long a, long long b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) {
        long long t = a % b;
        a = b;
        b = t;
    }
    return a;
}
