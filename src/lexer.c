/* lexer.c - converts UTF-8 source text into tokens */
#include "lexer.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

typedef struct Spelling {
    const char *text;
    TokKind kind;
} Spelling;

static const Spelling keywords[] = {
    {"ಸಂಖ್ಯೆ", K_INT},
    {"ದೀರ್ಘ", K_LONG},
    {"ದಶಾಂಶ", K_DOUBLE},
    {"ಅಕ್ಷರ", K_CHAR},
    {"ಪದ", K_STRING},
    {"ತಾರ್ಕಿಕ", K_BOOL},
    {"ಶೂನ್ಯ", K_VOID},
    {"ಚರಪಟ್ಟಿ", K_VALIST},
    {"ರಚನೆ", K_STRUCT},
    {"ಕ್ರಿಯೆ", K_FUNC},
    {"ಫಲಿತಾಂಶ", K_RETURNS},
    {"ಸ್ಥಿರ", K_CONST},
    {"ಬಾಹ್ಯ", K_EXTERN},
    {"ಆಮದು", K_IMPORT},
    {"ಆದರೆ", K_IF},
    {"ಇಲ್ಲವಾದರೆ", K_ELSE},
    {"ಸಮಯವರೆಗೆ", K_WHILE},
    {"ಮಾಡು", K_DO},
    {"ಎಣಿಕೆ", K_FOR},
    {"ನಿಲ್ಲಿಸು", K_BREAK},
    {"ಮುಂದುವರಿಸು", K_CONTINUE},
    {"ಮರಳಿ", K_RETURN},
    {"ಆಯ್ಕೆ", K_SWITCH},
    {"ಸಂದರ್ಭ", K_CASE},
    {"ಪೂರ್ವನಿಯೋಜಿತ", K_DEFAULT},
    {"ಮುದ್ರಿಸು", K_PRINT},
    {"ಸಾಲುಮುದ್ರಿಸು", K_PRINTLN},
    {"ಓದು", K_READ},
    {"ಗೆ", K_TO},
    {"ನಿಗದಿ", K_SET},
    {"ಅಥವಾ", K_OR},
    {"ಮತ್ತು", K_AND},
    {"ಅಲ್ಲ", K_NOT},
    {"ಸಮಾನ", K_EQ},
    {"ಸಮಾನವಲ್ಲ", K_NE},
    {"ಹೆಚ್ಚು", K_GT},
    {"ಕಡಿಮೆ", K_LT},
    {"ಹೆಚ್ಚಾಗಲಿ", K_GE},
    {"ಕಡಿಮೆಯಾಗಲಿ", K_LE},
    {"ಸೇರಿಸು", K_ADD},
    {"ಕಡಿತ", K_SUB},
    {"ಗುಣಿಸು", K_MUL},
    {"ಭಾಗಿಸು", K_DIV},
    {"ಶೇಷ", K_MOD},
    {"ಗಾತ್ರ", K_SIZEOF},
    {"ಸತ್ಯ", K_TRUE},
    {"ಅಸತ್ಯ", K_FALSE},
    {"ಖಾಲಿ", K_NULL},
    {"ಚರ_ಆರಂಭ", K_VA_START},
    {"ಚರ_ಮೌಲ್ಯ", K_VA_ARG},
    {"ಚರ_ಅಂತ್ಯ", K_VA_END},
    {"ಚರ_ನಕಲು", K_VA_COPY},
};

/* longest first */
static const Spelling puncts[] = {
    {"...", P_ELLIPSIS}, {"<<=", P_SHLEQ}, {">>=", P_SHREQ}, {"->", P_ARROW},
    {"++", P_INC},       {"--", P_DEC},   {"<<", P_SHL},    {">>", P_SHR},
    {"<=", P_LE},        {">=", P_GE},    {"==", P_EQ},     {"!=", P_NE},
    {"&&", P_ANDAND},    {"||", P_OROR},  {"+=", P_PLUSEQ}, {"-=", P_MINUSEQ},
    {"*=", P_STAREQ},    {"/=", P_SLASHEQ}, {"%=", P_PERCENTEQ}, {"&=", P_AMPEQ},
    {"|=", P_PIPEEQ},    {"^=", P_CARETEQ}, {"(", P_LPAREN}, {")", P_RPAREN},
    {"{", P_LBRACE},     {"}", P_RBRACE}, {"[", P_LBRACKET}, {"]", P_RBRACKET},
    {";", P_SEMI},       {",", P_COMMA},  {".", P_DOT},     {":", P_COLON},
    {"?", P_QUESTION},   {"+", P_PLUS},   {"-", P_MINUS},   {"*", P_STAR},
    {"/", P_SLASH},      {"%", P_PERCENT}, {"&", P_AMP},    {"|", P_PIPE},
    {"^", P_CARET},      {"~", P_TILDE},  {"!", P_BANG},    {"=", P_ASSIGN},
    {"<", P_LT},         {">", P_GT},
};

const char *tok_spelling(TokKind k) {
    switch (k) {
    case T_EOF: return "ಕಡತದ ಅಂತ್ಯ";
    case T_ID: return "ಹೆಸರು";
    case T_INT: return "ಪೂರ್ಣಾಂಕ";
    case T_FLOAT: return "ದಶಾಂಶ ಸಂಖ್ಯೆ";
    case T_STR: return "ಪದ ಅಕ್ಷರಶಃ";
    case T_CHAR: return "ಅಕ್ಷರ ಅಕ್ಷರಶಃ";
    default: break;
    }
    for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++)
        if (keywords[i].kind == k) return keywords[i].text;
    for (size_t i = 0; i < sizeof puncts / sizeof puncts[0]; i++)
        if (puncts[i].kind == k) return puncts[i].text;
    return "?";
}

typedef struct Lexer {
    SrcFile *file;
    const char *src;
    size_t pos, len;
    int line, col;
    Token *toks;
    int ntok, cap;
} Lexer;

static unsigned peek_cp(Lexer *L, size_t offset_bytes, int *nbytes) {
    unsigned cp = 0;
    size_t p = L->pos + offset_bytes;
    if (p >= L->len) {
        if (nbytes) *nbytes = 0;
        return 0;
    }
    int n = utf8_decode(L->src + p, L->len - p, &cp);
    if (nbytes) *nbytes = n;
    return cp;
}

static unsigned cur(Lexer *L) { return peek_cp(L, 0, NULL); }

static void advance(Lexer *L) {
    if (L->pos >= L->len) return;
    int n;
    unsigned cp = peek_cp(L, 0, &n);
    L->pos += (size_t)n;
    if (cp == '\n') {
        L->line++;
        L->col = 1;
    } else {
        L->col++;
    }
}

static unsigned peek_after(Lexer *L) {
    int n;
    peek_cp(L, 0, &n);
    if (n == 0) return 0;
    return peek_cp(L, (size_t)n, NULL);
}

static Loc here(Lexer *L) {
    Loc loc = {L->file, L->line, L->col};
    return loc;
}

static Token *push(Lexer *L, TokKind kind, Loc loc) {
    if (L->ntok == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 256;
        L->toks = xrealloc(L->toks, sizeof(Token) * (size_t)L->cap);
    }
    Token *t = &L->toks[L->ntok++];
    memset(t, 0, sizeof *t);
    t->kind = kind;
    t->loc = loc;
    return t;
}

static bool is_kn_digit(unsigned cp) { return cp >= 0x0CE6 && cp <= 0x0CEF; }
static bool is_ascii_digit(unsigned cp) { return cp >= '0' && cp <= '9'; }
static bool is_digit(unsigned cp) { return is_ascii_digit(cp) || is_kn_digit(cp); }
static int digit_value(unsigned cp) { return is_kn_digit(cp) ? (int)(cp - 0x0CE6) : (int)(cp - '0'); }

static bool is_id_start(unsigned cp) {
    if (cp < 128) return isalpha((int)cp) || cp == '_';
    return cp >= 0x0C80 && cp <= 0x0CFF && !is_kn_digit(cp);
}

static bool is_id_continue(unsigned cp) {
    return is_id_start(cp) || is_digit(cp) || cp == 0x200C || cp == 0x200D;
}

static bool is_space(unsigned cp) {
    return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == '\f' || cp == '\v' ||
           cp == 0x00A0 || cp == 0xFEFF;
}

static void skip_space_and_comments(Lexer *L) {
    for (;;) {
        unsigned c = cur(L);
        if (L->pos >= L->len) return;
        if (is_space(c)) {
            advance(L);
        } else if (c == '/' && peek_after(L) == '/') {
            while (L->pos < L->len && cur(L) != '\n') advance(L);
        } else if (c == '/' && peek_after(L) == '*') {
            Loc start = here(L);
            advance(L);
            advance(L);
            bool closed = false;
            while (L->pos < L->len) {
                if (cur(L) == '*' && peek_after(L) == '/') {
                    advance(L);
                    advance(L);
                    closed = true;
                    break;
                }
                advance(L);
            }
            if (!closed) diag_error(start, "ಮುಚ್ಚದ /* ಟಿಪ್ಪಣಿ */");
        } else if (c == '#' && L->pos == 0 && peek_after(L) == '!') {
            while (L->pos < L->len && cur(L) != '\n') advance(L); /* #! line */
        } else {
            return;
        }
    }
}

static void lex_identifier(Lexer *L) {
    Loc loc = here(L);
    size_t start = L->pos;
    while (L->pos < L->len && is_id_continue(cur(L))) advance(L);
    size_t n = L->pos - start;
    const char *s = L->src + start;
    for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
        if (strlen(keywords[i].text) == n && memcmp(keywords[i].text, s, n) == 0) {
            push(L, keywords[i].kind, loc);
            return;
        }
    }
    Token *t = push(L, T_ID, loc);
    t->text = xstrndup(s, n);
    t->len = n;
}

static int hex_value(unsigned c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (int)(c - 'A' + 10);
    return -1;
}

static void lex_number(Lexer *L) {
    Loc loc = here(L);
    StrBuf num;
    sb_init(&num);
    bool is_float = false;
    unsigned long long value = 0;
    bool overflow = false;
    int base = 10;

    unsigned c = cur(L);
    unsigned next = peek_after(L);
    if (c == '0' && (next == 'x' || next == 'X' || next == 'b' || next == 'B')) {
        base = (next == 'x' || next == 'X') ? 16 : 2;
        advance(L);
        advance(L);
        int digits = 0;
        for (;;) {
            int d = hex_value(cur(L));
            if (d < 0 || d >= base) break;
            if (value > (~0ULL - (unsigned)d) / (unsigned)base) overflow = true;
            value = value * (unsigned)base + (unsigned)d;
            digits++;
            advance(L);
        }
        if (digits == 0) diag_error(loc, "ಅಪೂರ್ಣ %s ಸಂಖ್ಯೆ", base == 16 ? "ಷೋಡಶ (hex)" : "ದ್ವಿಮಾನ (binary)");
    } else {
        while (is_digit(cur(L))) {
            sb_putc(&num, (char)('0' + digit_value(cur(L))));
            advance(L);
        }
        if (cur(L) == '.' && is_digit(peek_after(L))) {
            is_float = true;
            sb_putc(&num, '.');
            advance(L);
            while (is_digit(cur(L))) {
                sb_putc(&num, (char)('0' + digit_value(cur(L))));
                advance(L);
            }
        }
        if (cur(L) == 'e' || cur(L) == 'E') {
            /* exponent only if followed by digits */
            size_t save_pos = L->pos;
            int save_line = L->line, save_col = L->col;
            advance(L);
            char sign = 0;
            if (cur(L) == '+' || cur(L) == '-') {
                sign = (char)cur(L);
                advance(L);
            }
            if (is_digit(cur(L))) {
                is_float = true;
                sb_putc(&num, 'e');
                if (sign) sb_putc(&num, sign);
                while (is_digit(cur(L))) {
                    sb_putc(&num, (char)('0' + digit_value(cur(L))));
                    advance(L);
                }
            } else {
                L->pos = save_pos;
                L->line = save_line;
                L->col = save_col;
            }
        }
        if (!is_float) {
            for (size_t i = 0; i < num.len; i++) {
                unsigned d = (unsigned)(num.data[i] - '0');
                if (value > (~0ULL - d) / 10) overflow = true;
                value = value * 10 + d;
            }
        }
    }

    if (is_id_continue(cur(L))) {
        diag_error(here(L), "ಸಂಖ್ಯೆಯ ನಂತರ ಅಮಾನ್ಯ ಅಕ್ಷರ (ಸಂಖ್ಯೆ ಮತ್ತು ಹೆಸರಿನ ನಡುವೆ ಜಾಗ ಬಿಡಿ)");
        while (is_id_continue(cur(L))) advance(L);
    }

    if (is_float) {
        Token *t = push(L, T_FLOAT, loc);
        double d = strtod(num.data, NULL);
        if (isinf(d)) diag_error(loc, "ದಶಾಂಶ ಸಂಖ್ಯೆ ತುಂಬಾ ದೊಡ್ಡದು");
        t->text = xstrdup(num.data);
        t->len = num.len;
    } else {
        if (overflow || value > 9223372036854775807ULL) {
            diag_error(loc, "ಪೂರ್ಣಾಂಕ ತುಂಬಾ ದೊಡ್ಡದು (ಗರಿಷ್ಠ 9223372036854775807)");
            value = 0;
        }
        Token *t = push(L, T_INT, loc);
        t->ival = (long long)value;
    }
    sb_free(&num);
}

/* Reads one character or escape sequence of a string / char literal.
 * Returns false on end of line / file. *is_byte is set when the value is a
 * raw byte (\x, octal) rather than a Unicode code point. */
static bool read_char_or_escape(Lexer *L, unsigned *value, bool *is_byte) {
    *is_byte = false;
    unsigned c = cur(L);
    if (L->pos >= L->len || c == '\n' || c == '\r') return false;
    if (c != '\\') {
        *value = c;
        advance(L);
        return true;
    }
    Loc loc = here(L);
    advance(L);
    c = cur(L);
    if (L->pos >= L->len) return false;
    advance(L);
    switch (c) {
    case 'n': *value = '\n'; return true;
    case 't': *value = '\t'; return true;
    case 'r': *value = '\r'; return true;
    case 'a': *value = 7; return true;
    case 'b': *value = 8; return true;
    case 'f': *value = 12; return true;
    case 'v': *value = 11; return true;
    case 'e': *value = 27; return true;
    case '\\': *value = '\\'; return true;
    case '\'': *value = '\''; return true;
    case '"': *value = '"'; return true;
    case '?': *value = '?'; return true;
    case 'x': {
        unsigned v = 0;
        int n = 0;
        while (n < 2 && hex_value(cur(L)) >= 0) {
            v = v * 16 + (unsigned)hex_value(cur(L));
            advance(L);
            n++;
        }
        if (n == 0) diag_error(loc, "\\x ನಂತರ ಷೋಡಶ ಅಂಕಿ ಬೇಕು");
        *value = v;
        *is_byte = true;
        return true;
    }
    case 'u':
    case 'U': {
        int want = c == 'u' ? 4 : 8;
        unsigned v = 0;
        int n = 0;
        while (n < want && hex_value(cur(L)) >= 0) {
            v = v * 16 + (unsigned)hex_value(cur(L));
            advance(L);
            n++;
        }
        if (n != want || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) {
            diag_error(loc, "ಅಮಾನ್ಯ ಯುನಿಕೋಡ್ ಎಸ್ಕೇಪ್");
            v = 0xFFFD;
        }
        *value = v;
        return true;
    }
    default:
        if (c >= '0' && c <= '7') {
            unsigned v = c - '0';
            int n = 1;
            while (n < 3 && cur(L) >= '0' && cur(L) <= '7') {
                v = v * 8 + (cur(L) - '0');
                advance(L);
                n++;
            }
            if (v > 255) diag_error(loc, "ಅಷ್ಟಮಾನ ಎಸ್ಕೇಪ್ ತುಂಬಾ ದೊಡ್ಡದು");
            *value = v & 0xFF;
            *is_byte = true;
            return true;
        }
        diag_error(loc, "ಅಜ್ಞಾತ ಎಸ್ಕೇಪ್ ಅನುಕ್ರಮ");
        *value = c;
        return true;
    }
}

static void lex_string(Lexer *L) {
    Loc loc = here(L);
    advance(L); /* opening quote */
    StrBuf sb;
    sb_init(&sb);
    for (;;) {
        if (cur(L) == '"') {
            advance(L);
            break;
        }
        unsigned v;
        bool is_byte;
        if (!read_char_or_escape(L, &v, &is_byte)) {
            diag_error(loc, "ಮುಚ್ಚದ ಪದ ಅಕ್ಷರಶಃ (\" ಕಾಣೆಯಾಗಿದೆ)");
            break;
        }
        if (is_byte) {
            sb_putc(&sb, (char)v);
        } else {
            char buf[4];
            int n = utf8_encode(v, buf);
            sb_putn(&sb, buf, (size_t)n);
        }
    }
    Token *t = push(L, T_STR, loc);
    t->len = sb.len;
    t->text = sb.data;
}

static void lex_char(Lexer *L) {
    Loc loc = here(L);
    advance(L);
    if (cur(L) == '\'') {
        diag_error(loc, "ಖಾಲಿ ಅಕ್ಷರ ಅಕ್ಷರಶಃ");
        advance(L);
        push(L, T_CHAR, loc);
        return;
    }
    unsigned v = 0;
    bool is_byte = false;
    if (!read_char_or_escape(L, &v, &is_byte)) {
        diag_error(loc, "ಮುಚ್ಚದ ಅಕ್ಷರ ಅಕ್ಷರಶಃ");
        push(L, T_CHAR, loc);
        return;
    }
    if (cur(L) == '\'') {
        advance(L);
    } else {
        diag_error(loc, "ಅಕ್ಷರ ಅಕ್ಷರಶಃದಲ್ಲಿ ಒಂದೇ ಅಕ್ಷರ ಇರಬೇಕು (ಹೆಚ್ಚು ಅಕ್ಷರಗಳಿಗೆ \"...\" ಬಳಸಿ)");
        while (L->pos < L->len && cur(L) != '\'' && cur(L) != '\n') advance(L);
        if (cur(L) == '\'') advance(L);
    }
    Token *t = push(L, T_CHAR, loc);
    t->ival = (long long)v;
    t->wide = !is_byte && v >= 128;
}

static bool lex_punct(Lexer *L) {
    const char *s = L->src + L->pos;
    size_t rem = L->len - L->pos;
    for (size_t i = 0; i < sizeof puncts / sizeof puncts[0]; i++) {
        size_t n = strlen(puncts[i].text);
        if (n <= rem && memcmp(s, puncts[i].text, n) == 0) {
            push(L, puncts[i].kind, here(L));
            for (size_t k = 0; k < n; k++) advance(L);
            return true;
        }
    }
    return false;
}

Token *lex_file(SrcFile *file, int *count) {
    Lexer L;
    memset(&L, 0, sizeof L);
    L.file = file;
    L.src = file->text;
    L.len = file->len;
    L.line = 1;
    L.col = 1;
    for (;;) {
        skip_space_and_comments(&L);
        if (L.pos >= L.len) break;
        unsigned c = cur(&L);
        if (is_id_start(c))
            lex_identifier(&L);
        else if (is_digit(c))
            lex_number(&L);
        else if (c == '"')
            lex_string(&L);
        else if (c == '\'')
            lex_char(&L);
        else if (!lex_punct(&L)) {
            if (c >= 32 && c < 127)
                diag_error(here(&L), "ಅನಿರೀಕ್ಷಿತ ಅಕ್ಷರ '%c'", (int)c);
            else
                diag_error(here(&L), "ಅನಿರೀಕ್ಷಿತ ಅಕ್ಷರ U+%04X", c);
            advance(&L);
        }
    }
    push(&L, T_EOF, here(&L));
    *count = L.ntok;
    return L.toks;
}
