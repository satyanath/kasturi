/* kst_scanf.c - formatted input engine (scanf family)
 *
 * Supports the complete C11 directive syntax:
 *   whitespace          skips any amount of input whitespace
 *   ordinary character  must match the input exactly
 *   %[*][width][length]conversion
 *     length      hh h l ll j z t L q
 *     conversion  d i u o x X b c s [set] p n % f F e E g G a A
 * Kannada digits (೦-೯) are accepted wherever a digit is expected.
 * %lc stores a whole UTF-8 encoded code point into an int.
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kst_internal.h"

enum { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T, LEN_BIGL };

#define NUMBUF 512

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }

static int rd_get(kst_source *in) {
    int c = in->get(in);
    if (c != KST_EOF) in->count++;
    return c;
}

static void rd_unget(kst_source *in, int c) {
    if (c == KST_EOF) return;
    in->unget(in, c);
    in->count--;
}

/* a "logical" character: a Kannada digit is returned as its ASCII digit */
typedef struct LChar {
    int c;
    unsigned char raw[3];
    int n;
} LChar;

static LChar lget(kst_source *in, long long *width) {
    LChar r;
    r.n = 0;
    r.c = KST_EOF;
    if (*width <= 0) return r;
    int c = rd_get(in);
    if (c == KST_EOF) return r;
    (*width)--;
    r.raw[r.n++] = (unsigned char)c;
    r.c = c;
    if (c == 0xE0) {
        int c2 = rd_get(in);
        if (c2 == 0xB3) {
            int c3 = rd_get(in);
            if (c3 >= 0xA6 && c3 <= 0xAF) {
                r.raw[1] = (unsigned char)c2;
                r.raw[2] = (unsigned char)c3;
                r.n = 3;
                r.c = '0' + (c3 - 0xA6);
                return r;
            }
            rd_unget(in, c3);
        }
        rd_unget(in, c2);
    }
    return r;
}

static void lunget(kst_source *in, const LChar *ch) {
    for (int i = ch->n - 1; i >= 0; i--) rd_unget(in, ch->raw[i]);
}

static void skip_space(kst_source *in) {
    int c;
    do c = rd_get(in);
    while (c != KST_EOF && is_space(c));
    rd_unget(in, c);
}

static int digit_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

/* Reads an integer in the given base (0 = auto) into buf as ASCII.
 * Returns the number of digits read (0 = matching failure). */
static int scan_integer(kst_source *in, long long width, int *base, char *buf) {
    int n = 0, digits = 0;
    LChar ch = lget(in, &width);
    if (ch.c == '+' || ch.c == '-') {
        buf[n++] = (char)ch.c;
        ch = lget(in, &width);
    }
    if (ch.c == '0' && (*base == 0 || *base == 16 || *base == 2)) {
        buf[n++] = '0';
        digits = 1;
        LChar x = lget(in, &width);
        int want16 = (x.c == 'x' || x.c == 'X') && (*base == 0 || *base == 16);
        int want2 = (x.c == 'b' || x.c == 'B') && (*base == 0 || *base == 2);
        if (want16 || want2) {
            ch = lget(in, &width);
            int nb = want16 ? 16 : 2;
            if (digit_value(ch.c) < nb) {
                *base = nb;
                n = n - 1; /* drop the 0: strtoull gets plain digits */
                digits = 0;
            } else {
                lunget(in, &ch);
                lunget(in, &x);
                buf[n] = '\0';
                if (*base == 0) *base = 10;
                return digits;
            }
        } else {
            ch = x;
            if (*base == 0) *base = 8;
        }
    } else if (*base == 0) {
        *base = 10;
    }
    while (ch.c != KST_EOF && digit_value(ch.c) < *base) {
        if (n < NUMBUF - 1) buf[n++] = (char)ch.c;
        digits++;
        ch = lget(in, &width);
    }
    lunget(in, &ch);
    buf[n] = '\0';
    return digits;
}

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* Reads a floating point number into buf as ASCII; returns 1 on success. */
static int scan_float(kst_source *in, long long width, char *buf) {
    int n = 0;
    LChar ch = lget(in, &width);
    if (ch.c == '+' || ch.c == '-') {
        buf[n++] = (char)ch.c;
        ch = lget(in, &width);
    }
    int lc = lower(ch.c);
    if (lc == 'i' || lc == 'n') {
        const char *word = lc == 'i' ? "infinity" : "nan";
        int matched = 0;
        LChar seen[8];
        while (word[matched] && lower(ch.c) == word[matched]) {
            seen[matched] = ch;
            buf[n++] = (char)ch.c;
            matched++;
            if (!word[matched]) break;
            ch = lget(in, &width);
        }
        if (word[matched]) {
            if (lc == 'i' && matched >= 3) {
                /* "inf" followed by a partial "inity": give back the extra */
                lunget(in, &ch);
                for (int i = matched - 1; i >= 3; i--) lunget(in, &seen[i]);
                n -= matched - 3;
            } else {
                lunget(in, &ch);
                for (int i = matched - 1; i >= 0; i--) lunget(in, &seen[i]);
                return 0;
            }
        } else if (lc == 'n') {
            /* optional nan(n-char-sequence) */
            LChar p = lget(in, &width);
            if (p.c == '(') {
                char tmp[64];
                int tn = 0;
                LChar q = lget(in, &width);
                while (q.c != KST_EOF && q.c != ')' &&
                       ((q.c >= '0' && q.c <= '9') || (lower(q.c) >= 'a' && lower(q.c) <= 'z') || q.c == '_') &&
                       tn < 60) {
                    tmp[tn++] = (char)q.c;
                    q = lget(in, &width);
                }
                if (q.c != ')') {
                    lunget(in, &q);
                } else {
                    buf[n++] = '(';
                    memcpy(buf + n, tmp, (size_t)tn);
                    n += tn;
                    buf[n++] = ')';
                }
            } else {
                lunget(in, &p);
            }
        }
        buf[n] = '\0';
        return 1;
    }
    int hex = 0, digits = 0;
    if (ch.c == '0') {
        buf[n++] = '0';
        digits = 1;
        ch = lget(in, &width);
        if (ch.c == 'x' || ch.c == 'X') {
            LChar x = ch;
            ch = lget(in, &width);
            if (digit_value(ch.c) < 16 || ch.c == '.') {
                buf[n++] = 'x';
                hex = 1;
                digits = 0;
            } else {
                lunget(in, &ch);
                ch = x;
            }
        }
    }
    int base = hex ? 16 : 10;
    while (ch.c != KST_EOF && digit_value(ch.c) < base) {
        if (n < NUMBUF - 8) buf[n++] = (char)ch.c;
        digits++;
        ch = lget(in, &width);
    }
    if (ch.c == '.') {
        if (n < NUMBUF - 8) buf[n++] = '.';
        ch = lget(in, &width);
        while (ch.c != KST_EOF && digit_value(ch.c) < base) {
            if (n < NUMBUF - 8) buf[n++] = (char)ch.c;
            digits++;
            ch = lget(in, &width);
        }
    }
    if (digits == 0) {
        lunget(in, &ch);
        return 0;
    }
    if ((!hex && lower(ch.c) == 'e') || (hex && lower(ch.c) == 'p')) {
        LChar e = ch, s;
        s.c = KST_EOF;
        s.n = 0;
        ch = lget(in, &width);
        if (ch.c == '+' || ch.c == '-') {
            s = ch;
            ch = lget(in, &width);
        }
        if (ch.c >= '0' && ch.c <= '9') {
            buf[n++] = (char)e.c;
            if (s.n) buf[n++] = (char)s.c;
            int en = 0;
            while (ch.c >= '0' && ch.c <= '9') {
                if (en < 6) buf[n++] = (char)ch.c;
                en++;
                ch = lget(in, &width);
            }
            if (en > 6) { /* absurd exponent: saturate */
                buf[n - 1] = '9';
            }
        } else {
            lunget(in, &ch);
            if (s.n) lunget(in, &s);
            ch = e;
        }
    }
    lunget(in, &ch);
    buf[n] = '\0';
    return 1;
}

static void store_signed(void *p, int len, long long v) {
    switch (len) {
    case LEN_HH: *(signed char *)p = (signed char)v; break;
    case LEN_H: *(short *)p = (short)v; break;
    case LEN_L: *(long *)p = (long)v; break;
    case LEN_LL: *(long long *)p = v; break;
    case LEN_J: *(intmax_t *)p = (intmax_t)v; break;
    case LEN_Z: *(size_t *)p = (size_t)v; break;
    case LEN_T: *(ptrdiff_t *)p = (ptrdiff_t)v; break;
    default: *(int *)p = (int)v; break;
    }
}

/* gives back a UTF-8 sequence cut in half by a field width */
static long long trim_partial_utf8(kst_source *in, char *dst, long long n, const unsigned char *tail, int tn) {
    int c = rd_get(in);
    rd_unget(in, c);
    if (c == KST_EOF || (c & 0xC0) != 0x80) return n;
    int k = 0;
    while (k < tn && (tail[tn - 1 - k] & 0xC0) == 0x80) k++;
    if (k < tn) k++;
    for (int i = tn - 1; i >= tn - k; i--) rd_unget(in, tail[i]);
    (void)dst;
    return n - k;
}

int kst_scan(kst_source *in, const char *fmt, va_list ap) {
    if (!fmt) {
        kst_errno = KST_EINVAL;
        return KST_EOF;
    }
    int assigned = 0, converted = 0;
    const unsigned char *p = (const unsigned char *)fmt;
    char num[NUMBUF];
    while (*p) {
        if (is_space(*p)) {
            while (is_space(*p)) p++;
            skip_space(in);
            continue;
        }
        if (*p != '%' || p[1] == '%') {
            if (*p == '%') {
                p++;
                skip_space(in);
            }
            int c = rd_get(in);
            if (c == KST_EOF) goto input_failure;
            if (c != *p) {
                rd_unget(in, c);
                return assigned;
            }
            p++;
            continue;
        }
        p++;
        int suppress = 0;
        if (*p == '*') {
            suppress = 1;
            p++;
        }
        long long width = 0;
        int has_width = 0;
        while (*p >= '0' && *p <= '9') {
            if (width < 100000000) width = width * 10 + (*p - '0');
            has_width = 1;
            p++;
        }
        int len = LEN_NONE;
        switch (*p) {
        case 'h':
            if (p[1] == 'h') { len = LEN_HH; p += 2; } else { len = LEN_H; p++; }
            break;
        case 'l':
            if (p[1] == 'l') { len = LEN_LL; p += 2; } else { len = LEN_L; p++; }
            break;
        case 'q': len = LEN_LL; p++; break;
        case 'j': len = LEN_J; p++; break;
        case 'z': len = LEN_Z; p++; break;
        case 't': len = LEN_T; p++; break;
        case 'L': len = LEN_BIGL; p++; break;
        default: break;
        }
        int conv = *p;
        if (!conv) break;
        p++;
        if (has_width && width == 0) has_width = 0;
        if (!has_width) width = LLONG_MAX;

        if (conv != 'c' && conv != '[' && conv != 'n') {
            skip_space(in);
        }

        switch (conv) {
        case 'n':
            if (!suppress) store_signed(va_arg(ap, void *), len, in->count);
            break;

        case 'd':
        case 'i':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
        case 'b':
        case 'B':
        case 'p': {
            int base = conv == 'd' || conv == 'u' ? 10
                     : conv == 'i'                ? 0
                     : conv == 'o'                ? 8
                     : (conv == 'b' || conv == 'B') ? 2
                                                    : 16;
            int c = rd_get(in);
            if (c == KST_EOF) goto input_failure;
            rd_unget(in, c);
            if (!scan_integer(in, width, &base, num)) return assigned;
            converted++;
            if (suppress) break;
            if (conv == 'p') {
                *va_arg(ap, void **) = (void *)(uintptr_t)strtoull(num, NULL, 16);
            } else if (conv == 'd' || conv == 'i') {
                store_signed(va_arg(ap, void *), len, strtoll(num, NULL, base));
            } else {
                unsigned long long u = strtoull(num, NULL, base);
                store_signed(va_arg(ap, void *), len, (long long)u);
            }
            assigned++;
            break;
        }

        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            int c = rd_get(in);
            if (c == KST_EOF) goto input_failure;
            rd_unget(in, c);
            if (!scan_float(in, width, num)) return assigned;
            converted++;
            if (suppress) break;
            double v = strtod(num, NULL);
            if (len == LEN_L) *va_arg(ap, double *) = v;
            else if (len == LEN_BIGL) *va_arg(ap, long double *) = (long double)v;
            else *va_arg(ap, float *) = (float)v;
            assigned++;
            break;
        }

        case 'c': {
            if (len == LEN_L) {
                /* one Unicode code point */
                int c = rd_get(in);
                if (c == KST_EOF) goto input_failure;
                int cp = c, extra = 0;
                if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
                else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
                else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
                for (int i = 0; i < extra; i++) {
                    int d = rd_get(in);
                    if (d == KST_EOF || (d & 0xC0) != 0x80) {
                        rd_unget(in, d);
                        cp = 0xFFFD;
                        break;
                    }
                    cp = (cp << 6) | (d & 0x3F);
                }
                converted++;
                if (!suppress) {
                    *va_arg(ap, int *) = cp;
                    assigned++;
                }
                break;
            }
            long long count = has_width ? width : 1;
            char *dst = suppress ? NULL : va_arg(ap, char *);
            long long got = 0;
            while (got < count) {
                int c = rd_get(in);
                if (c == KST_EOF) break;
                if (dst) dst[got] = (char)c;
                got++;
            }
            if (got == 0) goto input_failure;
            if (got < count) return assigned;
            converted++;
            if (!suppress) assigned++;
            break;
        }

        case 's':
        case '[': {
            unsigned char set[256];
            if (conv == 's') {
                for (int i = 0; i < 256; i++) set[i] = !is_space(i);
                set[0] = 1;
            } else {
                int negate = 0;
                if (*p == '^') {
                    negate = 1;
                    p++;
                }
                memset(set, 0, sizeof set);
                const unsigned char *q = p;
                if (*q == ']') {
                    set[']'] = 1;
                    q++;
                }
                while (*q && *q != ']') {
                    if (q[1] == '-' && q[2] && q[2] != ']' && q[2] >= q[0]) {
                        for (int ch = q[0]; ch <= q[2]; ch++) set[ch] = 1;
                        q += 3;
                    } else {
                        set[*q] = 1;
                        q++;
                    }
                }
                if (!*q) { /* malformed set */
                    return assigned;
                }
                p = q + 1;
                if (negate)
                    for (int i = 0; i < 256; i++) set[i] = !set[i];
            }
            char *dst = suppress ? NULL : va_arg(ap, char *);
            long long n = 0;
            unsigned char tail[4];
            int tn = 0;
            int c = KST_EOF;
            int stopped_by_width = 0;
            for (;;) {
                if (n >= width) {
                    stopped_by_width = 1;
                    break;
                }
                c = rd_get(in);
                if (c == KST_EOF) break;
                if (!set[c] || (conv == 's' && c == 0)) {
                    rd_unget(in, c);
                    break;
                }
                if (dst) dst[n] = (char)c;
                n++;
                if (tn == 4) {
                    memmove(tail, tail + 1, 3);
                    tn = 3;
                }
                tail[tn++] = (unsigned char)c;
            }
            if (stopped_by_width) n = trim_partial_utf8(in, dst, n, tail, tn);
            if (n == 0) {
                if (c == KST_EOF && !stopped_by_width) goto input_failure;
                return assigned;
            }
            if (dst) dst[n] = '\0';
            converted++;
            if (!suppress) assigned++;
            break;
        }

        default:
            return assigned; /* unknown conversion */
        }
    }
    return assigned;

input_failure:
    return (converted == 0 && assigned == 0) ? KST_EOF : assigned;
}

/* ------------------------------------------------------------------ */
/* sources                                                             */
/* ------------------------------------------------------------------ */

static int file_get(kst_source *s) { return kst_fgetc(s->file); }
static void file_unget(kst_source *s, int c) { kst_ungetc(c, s->file); }

static int str_get(kst_source *s) {
    unsigned char c = s->str[s->pos];
    if (!c) return KST_EOF;
    s->pos++;
    return c;
}
static void str_unget(kst_source *s, int c) {
    (void)c;
    if (s->pos > 0) s->pos--;
}

int kst_vfscanf(KFILE *f, const char *fmt, va_list ap) {
    if (!f) {
        kst_errno = KST_EBADF;
        return KST_EOF;
    }
    kst_source s;
    memset(&s, 0, sizeof s);
    s.get = file_get;
    s.unget = file_unget;
    s.file = f;
    return kst_scan(&s, fmt, ap);
}

int kst_vscanf(const char *fmt, va_list ap) { return kst_vfscanf(kst_stdin, fmt, ap); }

int kst_vsscanf(const char *str, const char *fmt, va_list ap) {
    if (!str) {
        kst_errno = KST_EINVAL;
        return KST_EOF;
    }
    kst_source s;
    memset(&s, 0, sizeof s);
    s.get = str_get;
    s.unget = str_unget;
    s.str = (const unsigned char *)str;
    return kst_scan(&s, fmt, ap);
}

int kst_fscanf(KFILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = kst_vfscanf(f, fmt, ap);
    va_end(ap);
    return r;
}

int kst_scanf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = kst_vfscanf(kst_stdin, fmt, ap);
    va_end(ap);
    return r;
}

int kst_sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = kst_vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

char *kst__ascii_digits(char *s) {
    if (!s) return s;
    unsigned char *r = (unsigned char *)s, *w = (unsigned char *)s;
    while (*r) {
        if (r[0] == 0xE0 && r[1] == 0xB3 && r[2] >= 0xA6 && r[2] <= 0xAF) {
            *w++ = (unsigned char)('0' + (r[2] - 0xA6));
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
    return s;
}
