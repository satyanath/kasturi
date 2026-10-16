/* kst_printf.c - formatted output engine (printf family)
 *
 * Supports the complete C11 conversion syntax:
 *   %[flags][width][.precision][length]conversion
 *   flags      - + space # 0, and the Kasturi extension '~' which prints the
 *              digits as Kannada numerals (೦೧೨೩೪೫೬೭೮೯)
 *   width      number or *
 *   precision  .number or .*
 *   length     hh h l ll j z t L q
 *   conversion d i u o x X b B c s p n % f F e E g G a A
 * For %s and %c the width and precision count characters (UTF-8 code
 * points) rather than bytes; for ASCII text this is identical to C.
 * Floating point numbers are converted exactly (big-number arithmetic) and
 * rounded half-to-even, so the output matches the true binary value.
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kst_internal.h"

typedef struct Spec {
    int left, plus, space, alt, zero, kn;
    int width;
    int prec;
    int len;
    char conv;
} Spec;

enum { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T, LEN_BIGL };

/* ------------------------------------------------------------------ */
/* output helpers                                                      */
/* ------------------------------------------------------------------ */

static void put(kst_sink *o, const char *p, size_t n) {
    if (n == 0) return;
    o->write(o, p, n);
    o->total += (long long)n;
}

static void pad(kst_sink *o, char ch, int count) {
    char buf[32];
    memset(buf, ch, sizeof buf);
    while (count > 0) {
        int n = count > 32 ? 32 : count;
        put(o, buf, (size_t)n);
        count -= n;
    }
}

/* digits, optionally as Kannada numerals */
static void put_digits(kst_sink *o, const char *s, size_t n, int kn) {
    if (!kn) {
        put(o, s, n);
        return;
    }
    char buf[96];
    size_t bn = 0;
    for (size_t i = 0; i < n; i++) {
        if (bn + 3 > sizeof buf) {
            put(o, buf, bn);
            bn = 0;
        }
        char c = s[i];
        if (c >= '0' && c <= '9') {
            buf[bn++] = '\xE0';
            buf[bn++] = '\xB3';
            buf[bn++] = (char)(0xA6 + (c - '0'));
        } else {
            buf[bn++] = c;
        }
    }
    put(o, buf, bn);
}

static void pad_zeros(kst_sink *o, int count, int kn) {
    static const char zeros[] = "0000000000000000";
    while (count > 0) {
        int n = count > 16 ? 16 : count;
        put_digits(o, zeros, (size_t)n, kn);
        count -= n;
    }
}

/* ------------------------------------------------------------------ */
/* integers                                                            */
/* ------------------------------------------------------------------ */

static void fmt_integer(kst_sink *o, const Spec *s, unsigned long long mag, int neg, int is_signed) {
    char digits[72];
    int nd = 0;
    int base = 10, upper = 0;
    switch (s->conv) {
    case 'o': base = 8; break;
    case 'x': base = 16; break;
    case 'X': base = 16; upper = 1; break;
    case 'b':
    case 'B': base = 2; upper = s->conv == 'B'; break;
    default: break;
    }
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    unsigned long long orig = mag;
    if (!(mag == 0 && s->prec == 0)) {
        do {
            digits[nd++] = dig[mag % (unsigned)base];
            mag /= (unsigned)base;
        } while (mag);
    }
    for (int i = 0; i < nd / 2; i++) {
        char t = digits[i];
        digits[i] = digits[nd - 1 - i];
        digits[nd - 1 - i] = t;
    }
    char prefix[4];
    int np = 0;
    if (is_signed) {
        if (neg) prefix[np++] = '-';
        else if (s->plus) prefix[np++] = '+';
        else if (s->space) prefix[np++] = ' ';
    }
    int zeros = s->prec > nd ? s->prec - nd : 0;
    if (s->alt) {
        if (base == 8 && zeros == 0 && (nd == 0 || digits[0] != '0')) zeros = 1;
        if (base == 16 && orig != 0) {
            prefix[np++] = '0';
            prefix[np++] = upper ? 'X' : 'x';
        }
        if (base == 2 && orig != 0) {
            prefix[np++] = '0';
            prefix[np++] = upper ? 'B' : 'b';
        }
    }
    int len = np + zeros + nd;
    if (s->zero && !s->left && s->prec < 0 && s->width > len) {
        zeros += s->width - len;
        len = s->width;
    }
    if (!s->left && s->width > len) pad(o, ' ', s->width - len);
    put(o, prefix, (size_t)np);
    pad_zeros(o, zeros, s->kn);
    put_digits(o, digits, (size_t)nd, s->kn && base == 10);
    if (s->left && s->width > len) pad(o, ' ', s->width - len);
}

/* ------------------------------------------------------------------ */
/* exact decimal conversion of doubles                                 */
/* ------------------------------------------------------------------ */

#define DEC_MAX 1100

typedef struct Dec {
    char d[DEC_MAX]; /* significant digits, no leading zeros */
    int nd;          /* number of digits (0 = value is zero) */
    int dp;          /* value = 0.d[0]d[1]... * 10^dp */
} Dec;

static void big_mul_small(uint32_t *limbs, int *n, uint32_t m) {
    uint64_t carry = 0;
    for (int i = 0; i < *n; i++) {
        uint64_t cur = (uint64_t)limbs[i] * m + carry;
        limbs[i] = (uint32_t)(cur % 1000000000u);
        carry = cur / 1000000000u;
    }
    while (carry) {
        limbs[(*n)++] = (uint32_t)(carry % 1000000000u);
        carry /= 1000000000u;
    }
}

/* v must be finite and > 0 */
static void dec_from_double(double v, Dec *out) {
    static const uint32_t pow5[14] = {1u, 5u, 25u, 125u, 625u, 3125u, 15625u, 78125u, 390625u,
                                      1953125u, 9765625u, 48828125u, 244140625u, 1220703125u};
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    int bexp = (int)((bits >> 52) & 0x7FF);
    uint64_t mant = bits & ((1ULL << 52) - 1);
    int e;
    if (bexp == 0) {
        e = -1074;
    } else {
        mant |= 1ULL << 52;
        e = bexp - 1075;
    }
    while (mant && !(mant & 1)) {
        mant >>= 1;
        e++;
    }
    uint32_t limbs[130];
    int n = 0;
    uint64_t m = mant;
    while (m) {
        limbs[n++] = (uint32_t)(m % 1000000000u);
        m /= 1000000000u;
    }
    int e10 = 0;
    if (e > 0) {
        while (e > 0) {
            int sh = e > 29 ? 29 : e;
            big_mul_small(limbs, &n, 1u << sh);
            e -= sh;
        }
    } else if (e < 0) {
        int k = -e;
        e10 = -k; /* m / 2^k == m * 5^k / 10^k */
        while (k > 0) {
            int t = k > 13 ? 13 : k;
            big_mul_small(limbs, &n, pow5[t]);
            k -= t;
        }
    }
    /* to decimal text */
    char tmp[DEC_MAX];
    int len = 0;
    char first[16];
    int fl = 0;
    uint32_t top = limbs[n - 1];
    do {
        first[fl++] = (char)('0' + top % 10);
        top /= 10;
    } while (top);
    while (fl > 0) tmp[len++] = first[--fl];
    for (int i = n - 2; i >= 0; i--) {
        uint32_t x = limbs[i];
        for (int k = 8; k >= 0; k--) {
            tmp[len + k] = (char)('0' + x % 10);
            x /= 10;
        }
        len += 9;
    }
    int nd = len;
    while (nd > 0 && tmp[nd - 1] == '0') nd--;
    memcpy(out->d, tmp, (size_t)nd);
    out->nd = nd;
    out->dp = len + e10;
}

static char dec_digit(const Dec *d, int i) { return (i >= 0 && i < d->nd) ? d->d[i] : '0'; }

/* rounds to 'keep' significant digits, half to even */
static void dec_round(Dec *d, int keep) {
    if (keep >= d->nd) return;
    if (keep < 0) {
        d->nd = 0;
        return;
    }
    char r = d->d[keep];
    int up;
    if (r > '5') up = 1;
    else if (r < '5') up = 0;
    else {
        int rest = 0;
        for (int i = keep + 1; i < d->nd; i++)
            if (d->d[i] != '0') {
                rest = 1;
                break;
            }
        up = rest ? 1 : (keep > 0 ? ((d->d[keep - 1] - '0') & 1) : 0);
    }
    d->nd = keep;
    if (up) {
        int i = keep - 1;
        while (i >= 0 && d->d[i] == '9') i--;
        if (i < 0) {
            d->d[0] = '1';
            d->nd = 1;
            d->dp += 1;
        } else {
            d->d[i]++;
            d->nd = i + 1;
        }
    }
    while (d->nd > 0 && d->d[d->nd - 1] == '0') d->nd--;
}

static char *fixed_text(double a, int prec, int alt) {
    Dec d;
    if (a == 0) {
        d.nd = 0;
        d.dp = 1;
    } else {
        dec_from_double(a, &d);
        dec_round(&d, d.dp + prec);
        if (d.nd == 0) d.dp = 1;
    }
    int intlen = d.dp > 0 ? d.dp : 1;
    char *s = (char *)malloc((size_t)intlen + (size_t)prec + 4);
    if (!s) return NULL;
    size_t n = 0;
    if (d.dp > 0)
        for (int i = 0; i < d.dp; i++) s[n++] = dec_digit(&d, i);
    else
        s[n++] = '0';
    if (prec > 0 || alt) s[n++] = '.';
    for (int i = 0; i < prec; i++) s[n++] = dec_digit(&d, d.dp + i);
    s[n] = '\0';
    return s;
}

static char *exp_text(double a, int prec, int alt, int upper) {
    Dec d;
    int x = 0;
    if (a == 0) {
        d.nd = 0;
        d.dp = 1;
    } else {
        dec_from_double(a, &d);
        dec_round(&d, prec + 1);
        x = d.dp - 1;
    }
    char *s = (char *)malloc((size_t)prec + 16);
    if (!s) return NULL;
    size_t n = 0;
    s[n++] = dec_digit(&d, 0);
    if (prec > 0 || alt) s[n++] = '.';
    for (int i = 1; i <= prec; i++) s[n++] = dec_digit(&d, i);
    s[n++] = upper ? 'E' : 'e';
    s[n++] = x < 0 ? '-' : '+';
    int ax = x < 0 ? -x : x;
    char eb[8];
    int en = 0;
    do {
        eb[en++] = (char)('0' + ax % 10);
        ax /= 10;
    } while (ax);
    if (en < 2) eb[en++] = '0';
    while (en > 0) s[n++] = eb[--en];
    s[n] = '\0';
    return s;
}

static char *hex_text(double a, int prec, int alt, int upper) {
    uint64_t bits;
    memcpy(&bits, &a, sizeof bits);
    int bexp = (int)((bits >> 52) & 0x7FF);
    uint64_t frac = bits & ((1ULL << 52) - 1);
    int lead, e;
    if (bexp == 0) {
        lead = 0;
        e = frac ? -1022 : 0;
    } else {
        lead = 1;
        e = bexp - 1023;
    }
    int hd[13];
    for (int i = 0; i < 13; i++) hd[i] = (int)((frac >> (48 - 4 * i)) & 0xF);
    int nd = 13;
    if (prec < 0) {
        while (nd > 0 && hd[nd - 1] == 0) nd--;
    } else if (prec < 13) {
        int shift = (13 - prec) * 4;
        uint64_t keep = frac >> shift;
        uint64_t rem = frac & ((1ULL << shift) - 1);
        uint64_t half = 1ULL << (shift - 1);
        int odd = prec == 0 ? (lead & 1) : (int)(keep & 1);
        if (rem > half || (rem == half && odd)) {
            keep++;
            if (prec == 0 || (keep >> (prec * 4)) != 0) {
                keep = 0;
                lead++; /* may become 2, as in glibc: 0x2.00p+0 */
            }
        }
        for (int i = 0; i < prec; i++) hd[i] = (int)((keep >> (4 * (prec - 1 - i))) & 0xF);
        nd = prec;
    }
    int frac_digits = prec < 0 ? nd : prec;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char *s = (char *)malloc((size_t)frac_digits + 32);
    if (!s) return NULL;
    size_t n = 0;
    s[n++] = '0';
    s[n++] = upper ? 'X' : 'x';
    s[n++] = (char)('0' + lead);
    if (frac_digits > 0 || alt) s[n++] = '.';
    for (int i = 0; i < frac_digits; i++) s[n++] = i < nd ? dig[hd[i]] : '0';
    s[n++] = upper ? 'P' : 'p';
    s[n++] = e < 0 ? '-' : '+';
    int ae = e < 0 ? -e : e;
    char eb[8];
    int en = 0;
    do {
        eb[en++] = (char)('0' + ae % 10);
        ae /= 10;
    } while (ae);
    while (en > 0) s[n++] = eb[--en];
    s[n] = '\0';
    return s;
}

static void strip_trailing_zeros(char *s) {
    char *dot = strchr(s, '.');
    if (!dot) return;
    char *e = dot;
    while (*e && *e != 'e' && *e != 'E') e++;
    char *end = e;
    while (end > dot + 1 && end[-1] == '0') end--;
    if (end == dot + 1) end = dot;
    memmove(end, e, strlen(e) + 1);
}

static void fmt_float(kst_sink *o, const Spec *s, double v) {
    char conv = s->conv;
    int upper = conv == 'F' || conv == 'E' || conv == 'G' || conv == 'A';
    char lc = (char)(upper ? conv - 'A' + 'a' : conv);
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    int neg = (int)(bits >> 63);
    double a = neg ? -v : v;
    char sign = neg ? '-' : s->plus ? '+' : s->space ? ' ' : 0;
    int exp_bits = (int)((bits >> 52) & 0x7FF);
    char *body = NULL;
    int special = 0;
    if (exp_bits == 0x7FF) {
        special = 1;
        int is_nan = (bits & ((1ULL << 52) - 1)) != 0;
        const char *t = is_nan ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        body = (char *)malloc(4);
        if (body) memcpy(body, t, 4);
    } else {
        int prec = s->prec < 0 ? 6 : s->prec;
        switch (lc) {
        case 'f':
            body = fixed_text(a, prec, s->alt);
            break;
        case 'e':
            body = exp_text(a, prec, s->alt, upper);
            break;
        case 'g': {
            int P = s->prec < 0 ? 6 : (s->prec == 0 ? 1 : s->prec);
            int x = 0;
            if (a != 0) {
                Dec d;
                dec_from_double(a, &d);
                dec_round(&d, P);
                x = d.dp - 1;
            }
            if (P > x && x >= -4)
                body = fixed_text(a, P - 1 - x, s->alt);
            else
                body = exp_text(a, P - 1, s->alt, upper);
            if (body && !s->alt) strip_trailing_zeros(body);
            break;
        }
        default: /* 'a' */
            body = hex_text(a, s->prec, s->alt, upper);
            break;
        }
    }
    if (!body) {
        o->failed = 1;
        return;
    }
    int blen = (int)strlen(body);
    int len = blen + (sign ? 1 : 0);
    int zeros = 0;
    if (s->zero && !s->left && !special && s->width > len) {
        zeros = s->width - len;
        len = s->width;
    }
    if (!s->left && s->width > len) pad(o, ' ', s->width - len);
    if (sign) put(o, &sign, 1);
    const char *b = body;
    if (lc == 'a' && !special) {
        put(o, b, 2); /* 0x before zero padding */
        b += 2;
    }
    pad_zeros(o, zeros, s->kn && lc != 'a');
    put_digits(o, b, strlen(b), s->kn && lc != 'a');
    if (s->left && s->width > len) pad(o, ' ', s->width - len);
    free(body);
}

/* ------------------------------------------------------------------ */
/* strings and characters                                              */
/* ------------------------------------------------------------------ */

/* number of UTF-8 encoded characters (code points) in p[0..n) */
static size_t u8_count(const char *p, size_t n) {
    size_t c = 0;
    for (size_t i = 0; i < n; i++)
        if (((unsigned char)p[i] & 0xC0) != 0x80) c++;
    return c;
}

/* field width counts characters, not bytes, so Kannada text lines up */
static void fmt_bytes(kst_sink *o, const Spec *s, const char *p, size_t n) {
    int chars = (int)u8_count(p, n);
    int w = s->width > chars ? s->width - chars : 0;
    if (!s->left) pad(o, ' ', w);
    put(o, p, n);
    if (s->left) pad(o, ' ', w);
}

static int utf8_encode(unsigned cp, char *out) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* ------------------------------------------------------------------ */
/* the engine                                                          */
/* ------------------------------------------------------------------ */

int kst_format(kst_sink *o, const char *fmt, va_list ap) {
    if (!fmt) {
        kst_errno = KST_EINVAL;
        return -1;
    }
    const char *p = fmt;
    while (*p) {
        if (*p != '%') {
            const char *q = p;
            while (*q && *q != '%') q++;
            put(o, p, (size_t)(q - p));
            p = q;
            continue;
        }
        const char *start = p++;
        Spec s;
        memset(&s, 0, sizeof s);
        s.prec = -1;
        for (;;) {
            char c = *p;
            if (c == '-') s.left = 1;
            else if (c == '+') s.plus = 1;
            else if (c == ' ') s.space = 1;
            else if (c == '#') s.alt = 1;
            else if (c == '0') s.zero = 1;
            else if (c == '~') s.kn = 1;
            else if (c == '\'') { /* digit grouping: accepted, ignored */ }
            else break;
            p++;
        }
        if (*p == '*') {
            int w = va_arg(ap, int);
            if (w < 0) {
                s.left = 1;
                w = w == INT_MIN ? INT_MAX : -w;
            }
            s.width = w;
            p++;
        } else {
            while (*p >= '0' && *p <= '9') {
                if (s.width < 100000000) s.width = s.width * 10 + (*p - '0');
                p++;
            }
        }
        if (*p == '.') {
            p++;
            s.prec = 0;
            if (*p == '*') {
                int pr = va_arg(ap, int);
                s.prec = pr < 0 ? -1 : pr;
                p++;
            } else {
                while (*p >= '0' && *p <= '9') {
                    if (s.prec < 100000000) s.prec = s.prec * 10 + (*p - '0');
                    p++;
                }
            }
        }
        switch (*p) {
        case 'h':
            if (p[1] == 'h') {
                s.len = LEN_HH;
                p += 2;
            } else {
                s.len = LEN_H;
                p++;
            }
            break;
        case 'l':
            if (p[1] == 'l') {
                s.len = LEN_LL;
                p += 2;
            } else {
                s.len = LEN_L;
                p++;
            }
            break;
        case 'q': s.len = LEN_LL; p++; break;
        case 'j': s.len = LEN_J; p++; break;
        case 'z': s.len = LEN_Z; p++; break;
        case 't': s.len = LEN_T; p++; break;
        case 'L': s.len = LEN_BIGL; p++; break;
        default: break;
        }
        char c = *p;
        if (!c) {
            put(o, start, (size_t)(p - start));
            break;
        }
        p++;
        s.conv = c;
        switch (c) {
        case 'd':
        case 'i': {
            long long v;
            switch (s.len) {
            case LEN_HH: v = (signed char)va_arg(ap, int); break;
            case LEN_H: v = (short)va_arg(ap, int); break;
            case LEN_L: v = va_arg(ap, long); break;
            case LEN_LL: v = va_arg(ap, long long); break;
            case LEN_J: v = (long long)va_arg(ap, intmax_t); break;
            case LEN_Z:
            case LEN_T: v = (long long)va_arg(ap, ptrdiff_t); break;
            default: v = va_arg(ap, int); break;
            }
            unsigned long long mag = v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v;
            fmt_integer(o, &s, mag, v < 0, 1);
            break;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X':
        case 'b':
        case 'B': {
            unsigned long long v;
            switch (s.len) {
            case LEN_HH: v = (unsigned char)va_arg(ap, unsigned int); break;
            case LEN_H: v = (unsigned short)va_arg(ap, unsigned int); break;
            case LEN_L: v = va_arg(ap, unsigned long); break;
            case LEN_LL: v = va_arg(ap, unsigned long long); break;
            case LEN_J: v = (unsigned long long)va_arg(ap, uintmax_t); break;
            case LEN_Z: v = (unsigned long long)va_arg(ap, size_t); break;
            case LEN_T: v = (unsigned long long)va_arg(ap, ptrdiff_t); break;
            default: v = va_arg(ap, unsigned int); break;
            }
            fmt_integer(o, &s, v, 0, 0);
            break;
        }
        case 'c': {
            char buf[4];
            int n;
            if (s.len == LEN_L) {
                n = utf8_encode((unsigned)va_arg(ap, int), buf);
            } else {
                buf[0] = (char)(unsigned char)va_arg(ap, int);
                n = 1;
            }
            fmt_bytes(o, &s, buf, (size_t)n);
            break;
        }
        case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str) str = "(ಖಾಲಿ)";
            size_t n;
            if (s.prec >= 0) {
                /* precision counts characters; a UTF-8 sequence is never cut */
                int chars = 0;
                n = 0;
                while (str[n]) {
                    if (((unsigned char)str[n] & 0xC0) != 0x80) {
                        if (chars == s.prec) break;
                        chars++;
                    }
                    n++;
                }
            } else {
                n = strlen(str);
            }
            fmt_bytes(o, &s, str, n);
            break;
        }
        case 'p': {
            void *ptr = va_arg(ap, void *);
            uintptr_t u = (uintptr_t)ptr;
            Spec t = s;
            t.conv = 'x';
            t.alt = 1;
            t.kn = 0;
            if (u == 0) {
                fmt_bytes(o, &s, "0x0", 3);
            } else {
                fmt_integer(o, &t, (unsigned long long)u, 0, 0);
            }
            break;
        }
        case 'n': {
            long long count = o->total;
            switch (s.len) {
            case LEN_HH: *va_arg(ap, signed char *) = (signed char)count; break;
            case LEN_H: *va_arg(ap, short *) = (short)count; break;
            case LEN_L: *va_arg(ap, long *) = (long)count; break;
            case LEN_LL: *va_arg(ap, long long *) = count; break;
            case LEN_J: *va_arg(ap, intmax_t *) = (intmax_t)count; break;
            case LEN_Z:
            case LEN_T: *va_arg(ap, ptrdiff_t *) = (ptrdiff_t)count; break;
            default: *va_arg(ap, int *) = (int)count; break;
            }
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
            double v = s.len == LEN_BIGL ? (double)va_arg(ap, long double) : va_arg(ap, double);
            fmt_float(o, &s, v);
            break;
        }
        case '%':
            put(o, "%", 1);
            break;
        default:
            put(o, start, (size_t)(p - start)); /* unknown conversion: copy as is */
            break;
        }
    }
    if (o->failed) return -1;
    if (o->total > INT_MAX) {
        kst_errno = KST_EOVERFLOW;
        return -1;
    }
    return (int)o->total;
}

/* ------------------------------------------------------------------ */
/* sinks                                                               */
/* ------------------------------------------------------------------ */

static void file_write(kst_sink *s, const char *p, size_t n) {
    if (s->failed) return;
    if (kst__write_bytes(s->file, p, n) != 0) s->failed = 1;
}

static void string_write(kst_sink *s, const char *p, size_t n) {
    if (s->grow) {
        if (s->failed) return;
        if (s->used + n + 1 > s->cap) {
            size_t cap = s->cap ? s->cap : 64;
            while (cap < s->used + n + 1) cap *= 2;
            char *b = (char *)realloc(s->buf, cap);
            if (!b) {
                s->failed = 1;
                kst_errno = KST_ENOMEM;
                return;
            }
            s->buf = b;
            s->cap = cap;
        }
        memcpy(s->buf + s->used, p, n);
        s->used += n;
        return;
    }
    if (s->cap == 0) return;
    size_t room = s->cap - 1 - s->used;
    size_t take = n < room ? n : room;
    memcpy(s->buf + s->used, p, take);
    s->used += take;
}

static void init_sink(kst_sink *s) { memset(s, 0, sizeof *s); }

int kst_vfprintf(KFILE *f, const char *fmt, va_list ap) {
    if (kst__begin_write(f) != 0) return -1;
    kst_sink s;
    init_sink(&s);
    s.write = file_write;
    s.file = f;
    int r = kst_format(&s, fmt, ap);
    kst__end_write(f);
    if (s.failed || kst_ferror(f)) return -1;
    return r;
}

int kst_vprintf(const char *fmt, va_list ap) { return kst_vfprintf(kst_stdout, fmt, ap); }

int kst_vsnprintf(char *buf, long long size, const char *fmt, va_list ap) {
    if (size < 0 || (size > 0 && !buf)) {
        kst_errno = KST_EINVAL;
        return -1;
    }
    kst_sink s;
    init_sink(&s);
    s.write = string_write;
    s.buf = buf;
    s.cap = (size_t)size;
    int r = kst_format(&s, fmt, ap);
    if (size > 0) buf[s.used] = '\0';
    return r;
}

int kst_vsprintf(char *buf, const char *fmt, va_list ap) {
    if (!buf) {
        kst_errno = KST_EINVAL;
        return -1;
    }
    kst_sink s;
    init_sink(&s);
    s.write = string_write;
    s.buf = buf;
    s.cap = ((size_t)-1) >> 1;
    int r = kst_format(&s, fmt, ap);
    buf[s.used] = '\0';
    return r;
}

char *kst_vaprintf(const char *fmt, va_list ap) {
    kst_sink s;
    init_sink(&s);
    s.write = string_write;
    s.grow = 1;
    int r = kst_format(&s, fmt, ap);
    if (r < 0 || s.failed) {
        free(s.buf);
        return NULL;
    }
    if (!s.buf) {
        s.buf = (char *)malloc(1);
        if (!s.buf) return NULL;
    }
    s.buf[s.used] = '\0';
    return s.buf;
}

int kst_fprintf(KFILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = kst_vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int kst_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = kst_vfprintf(kst_stdout, fmt, ap);
    va_end(ap);
    return r;
}

int kst_snprintf(char *buf, long long size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = kst_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int kst_sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = kst_vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

char *kst_aprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *r = kst_vaprintf(fmt, ap);
    va_end(ap);
    return r;
}
