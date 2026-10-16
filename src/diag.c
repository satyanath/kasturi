/* diag.c - error reporting with source excerpts */
#include "diag.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define MAX_ERRORS 30

static int g_errors;
static int g_warnings_enabled = 1;
static int g_werror;

void diag_set_warnings(int enabled) { g_warnings_enabled = enabled; }
void diag_set_warnings_as_errors(int enabled) { g_werror = enabled; }
int diag_error_count(void) { return g_errors; }

/* Kannada vowel signs, virama etc. render on top of the previous letter. */
static int zero_width(unsigned cp) {
    return (cp >= 0x0CBC && cp <= 0x0CCD && cp != 0x0CBD) || cp == 0x0CD5 || cp == 0x0CD6 ||
           cp == 0x0CE2 || cp == 0x0CE3 || cp == 0x200C || cp == 0x200D;
}

static void show_source_line(Loc loc) {
    if (!loc.file || !loc.file->text || loc.line <= 0) return;
    const char *p = loc.file->text;
    const char *end = p + loc.file->len;
    int line = 1;
    while (p < end && line < loc.line) {
        if (*p == '\n') line++;
        p++;
    }
    const char *start = p;
    while (p < end && *p != '\n' && *p != '\r') p++;

    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "%5d | ", loc.line);
    sb_putn(&sb, start, (size_t)(p - start));
    sb_puts(&sb, "\n      | ");
    const char *q = start;
    int col = 1;
    while (q < p && col < loc.col) {
        unsigned cp;
        int n = utf8_decode(q, (size_t)(p - q), &cp);
        if (*q == '\t')
            sb_putc(&sb, '\t');
        else if (!zero_width(cp))
            sb_putc(&sb, ' ');
        q += n;
        col++;
    }
    sb_puts(&sb, "^\n");
    con_write(stderr, sb.data, sb.len);
    sb_free(&sb);
}

static void report(const char *kind, Loc loc, const char *fmt, va_list ap) {
    char *msg = xvformat(fmt, ap);
    if (loc.file)
        con_printf(stderr, "%s:%d:%d: %s: %s\n", loc.file->path, loc.line, loc.col, kind, msg);
    else
        con_printf(stderr, "kasturi: %s: %s\n", kind, msg);
    free(msg);
    show_source_line(loc);
}

void diag_error(Loc loc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    report("ದೋಷ", loc, fmt, ap);
    va_end(ap);
    if (++g_errors >= MAX_ERRORS) {
        con_printf(stderr, "kasturi: ಅತಿ ಹೆಚ್ಚು ದೋಷಗಳು; ಸಂಕಲನವನ್ನು ನಿಲ್ಲಿಸಲಾಗಿದೆ.\n");
        exit(1);
    }
}

void diag_warning(Loc loc, const char *fmt, ...) {
    if (!g_warnings_enabled) return;
    va_list ap;
    va_start(ap, fmt);
    report(g_werror ? "ದೋಷ (ಎಚ್ಚರಿಕೆ)" : "ಎಚ್ಚರಿಕೆ", loc, fmt, ap);
    va_end(ap);
    if (g_werror) g_errors++;
}

void diag_note(Loc loc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    report("ಸೂಚನೆ", loc, fmt, ap);
    va_end(ap);
}

void fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *msg = xvformat(fmt, ap);
    va_end(ap);
    con_printf(stderr, "kasturi: ದೋಷ: %s\n", msg);
    free(msg);
    exit(1);
}
