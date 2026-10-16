/* mangle.c - Kannada -> Latin transliteration used for generated C names */
#include "mangle.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

enum { CP_OTHER, CP_VOWEL, CP_CONSONANT, CP_SIGN, CP_VIRAMA };

typedef struct Translit {
    unsigned cp;
    const char *latin;
    int kind;
} Translit;

static const Translit table[] = {
    {0x0C80, "m", CP_OTHER},     {0x0C81, "m", CP_OTHER},     {0x0C82, "m", CP_OTHER},
    {0x0C83, "h", CP_OTHER},     {0x0C85, "a", CP_VOWEL},     {0x0C86, "aa", CP_VOWEL},
    {0x0C87, "i", CP_VOWEL},     {0x0C88, "ii", CP_VOWEL},    {0x0C89, "u", CP_VOWEL},
    {0x0C8A, "uu", CP_VOWEL},    {0x0C8B, "ru", CP_VOWEL},    {0x0C8C, "lu", CP_VOWEL},
    {0x0C8E, "e", CP_VOWEL},     {0x0C8F, "ee", CP_VOWEL},    {0x0C90, "ai", CP_VOWEL},
    {0x0C92, "o", CP_VOWEL},     {0x0C93, "oo", CP_VOWEL},    {0x0C94, "au", CP_VOWEL},
    {0x0C95, "k", CP_CONSONANT}, {0x0C96, "kh", CP_CONSONANT}, {0x0C97, "g", CP_CONSONANT},
    {0x0C98, "gh", CP_CONSONANT}, {0x0C99, "ng", CP_CONSONANT}, {0x0C9A, "ch", CP_CONSONANT},
    {0x0C9B, "chh", CP_CONSONANT}, {0x0C9C, "j", CP_CONSONANT}, {0x0C9D, "jh", CP_CONSONANT},
    {0x0C9E, "ny", CP_CONSONANT}, {0x0C9F, "T", CP_CONSONANT}, {0x0CA0, "Th", CP_CONSONANT},
    {0x0CA1, "D", CP_CONSONANT}, {0x0CA2, "Dh", CP_CONSONANT}, {0x0CA3, "N", CP_CONSONANT},
    {0x0CA4, "t", CP_CONSONANT}, {0x0CA5, "th", CP_CONSONANT}, {0x0CA6, "d", CP_CONSONANT},
    {0x0CA7, "dh", CP_CONSONANT}, {0x0CA8, "n", CP_CONSONANT}, {0x0CAA, "p", CP_CONSONANT},
    {0x0CAB, "ph", CP_CONSONANT}, {0x0CAC, "b", CP_CONSONANT}, {0x0CAD, "bh", CP_CONSONANT},
    {0x0CAE, "m", CP_CONSONANT}, {0x0CAF, "y", CP_CONSONANT}, {0x0CB0, "r", CP_CONSONANT},
    {0x0CB1, "rr", CP_CONSONANT}, {0x0CB2, "l", CP_CONSONANT}, {0x0CB3, "L", CP_CONSONANT},
    {0x0CB5, "v", CP_CONSONANT}, {0x0CB6, "sh", CP_CONSONANT}, {0x0CB7, "Sh", CP_CONSONANT},
    {0x0CB8, "s", CP_CONSONANT}, {0x0CB9, "h", CP_CONSONANT}, {0x0CDE, "zh", CP_CONSONANT},
    {0x0CBC, "", CP_OTHER},      {0x0CBD, "", CP_OTHER},
    {0x0CBE, "aa", CP_SIGN},     {0x0CBF, "i", CP_SIGN},      {0x0CC0, "ii", CP_SIGN},
    {0x0CC1, "u", CP_SIGN},      {0x0CC2, "uu", CP_SIGN},     {0x0CC3, "ru", CP_SIGN},
    {0x0CC4, "ruu", CP_SIGN},    {0x0CC6, "e", CP_SIGN},      {0x0CC7, "ee", CP_SIGN},
    {0x0CC8, "ai", CP_SIGN},     {0x0CCA, "o", CP_SIGN},      {0x0CCB, "oo", CP_SIGN},
    {0x0CCC, "au", CP_SIGN},     {0x0CCD, "", CP_VIRAMA},     {0x0CD5, "", CP_OTHER},
    {0x0CD6, "", CP_OTHER},      {0x0CDD, "n", CP_OTHER},     {0x0CE0, "ruu", CP_VOWEL},
    {0x0CE1, "luu", CP_VOWEL},   {0x0CE2, "lu", CP_SIGN},     {0x0CE3, "luu", CP_SIGN},
    {0x0CE6, "0", CP_OTHER},     {0x0CE7, "1", CP_OTHER},     {0x0CE8, "2", CP_OTHER},
    {0x0CE9, "3", CP_OTHER},     {0x0CEA, "4", CP_OTHER},     {0x0CEB, "5", CP_OTHER},
    {0x0CEC, "6", CP_OTHER},     {0x0CED, "7", CP_OTHER},     {0x0CEE, "8", CP_OTHER},
    {0x0CEF, "9", CP_OTHER},     {0x200C, "", CP_OTHER},      {0x200D, "", CP_OTHER},
};

static const Translit *find(unsigned cp) {
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (table[i].cp == cp) return &table[i];
    return NULL;
}

char *transliterate(const char *name) {
    StrBuf sb;
    sb_init(&sb);
    bool pending_a = false;
    size_t len = strlen(name);
    size_t i = 0;
    while (i < len) {
        unsigned cp;
        int n = utf8_decode(name + i, len - i, &cp);
        i += (size_t)n;
        const Translit *t = cp >= 0x80 ? find(cp) : NULL;
        int kind = t ? t->kind : CP_OTHER;
        if (kind == CP_SIGN || kind == CP_VIRAMA) {
            pending_a = false;
            sb_puts(&sb, t->latin);
            continue;
        }
        if (pending_a) {
            sb_putc(&sb, 'a');
            pending_a = false;
        }
        if (cp < 0x80) {
            sb_putc(&sb, (char)cp);
        } else if (t) {
            sb_puts(&sb, t->latin);
            if (kind == CP_CONSONANT) pending_a = true;
        } else {
            sb_printf(&sb, "u%04X", cp);
        }
    }
    if (pending_a) sb_putc(&sb, 'a');
    return sb.data;
}

static StrMap g_names; /* prefix+name -> C name */
static StrMap g_used;  /* C names already handed out */

const char *mangle(const char *prefix, const char *name) {
    char *key = xformat("%s\x01%s", prefix, name);
    const char *found = map_get(&g_names, key);
    if (found) {
        free(key);
        return found;
    }
    char *lat = transliterate(name);
    char *base = xformat("%s%s", prefix, lat);
    free(lat);
    char *cname = xstrdup(base);
    for (int n = 2; map_get(&g_used, cname); n++) {
        free(cname);
        cname = xformat("%s_%d", base, n);
    }
    free(base);
    map_put(&g_used, cname, cname);
    map_put(&g_names, key, cname);
    free(key);
    return cname;
}
