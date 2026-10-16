/* rt_selftest.c - compares the Kasturi runtime against the host C library.
 * Build (MSVC): cl /nologo /utf-8 /I runtime tests\rt_selftest.c runtime\*.c
 * Build (gcc):  gcc -I runtime tests/rt_selftest.c runtime/*.c -lm
 */
#define _CRT_SECURE_NO_WARNINGS
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "kasturi_rt.h"

static int failures, checks;

#define CHECK_FMT(fmt, val)                                                              \
    do {                                                                                 \
        char a[512], b[512];                                                             \
        int ra = kst_snprintf(a, sizeof a, fmt, val);                                    \
        int rb = snprintf(b, sizeof b, fmt, val);                                        \
        checks++;                                                                        \
        if (ra != rb || strcmp(a, b) != 0) {                                             \
            failures++;                                                                  \
            printf("FAIL %-10s kst=[%s](%d) libc=[%s](%d)\n", fmt, a, ra, b, rb);        \
        }                                                                                \
    } while (0)

#define CHECK_STR(got, want)                                                             \
    do {                                                                                 \
        checks++;                                                                        \
        if (strcmp((got), (want)) != 0) {                                                \
            failures++;                                                                  \
            printf("FAIL line %d: got [%s] want [%s]\n", __LINE__, (got), (want));       \
        }                                                                                \
    } while (0)

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        checks++;                                                                        \
        if (!(cond)) {                                                                   \
            failures++;                                                                  \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                               \
        }                                                                                \
    } while (0)

static void test_printf(void) {
    static const char *ifmts[] = {"%d", "%5d", "%-5d|", "%05d", "%+d", "% d", "%.3d", "%8.3d", "%-+8.3d|",
                                  "%x", "%#x", "%#X", "%08x", "%#010x", "%o", "%#o", "%.0d", "%#.0o", "%u"};
    static const int ivals[] = {0, 1, -1, 42, -42, 12345, -2147483647 - 1, 2147483647, 255};
    for (size_t i = 0; i < sizeof ifmts / sizeof *ifmts; i++)
        for (size_t j = 0; j < sizeof ivals / sizeof *ivals; j++) CHECK_FMT(ifmts[i], ivals[j]);

    CHECK_FMT("%lld", -9223372036854775807LL - 1);
    CHECK_FMT("%llu", 18446744073709551615ULL);
    CHECK_FMT("%llx", 0xDEADBEEFCAFEULL);
    CHECK_FMT("%hhd", 300);
    CHECK_FMT("%hd", 70000);

    static const char *ffmts[] = {"%f", "%.0f", "%.1f", "%.2f", "%.10f", "%#.0f", "%e", "%.0e", "%.3e", "%E",
                                  "%g", "%.0g", "%.1g", "%.3g", "%.10g", "%#g", "%G", "%12.4f", "%-12.4e|",
                                  "%+012.3f", "% f", "%.17g", "%.20f", "%.13a", "%.3a", "%.13A", "%.0a", "%010.2a"};
    static const double fvals[] = {0.0, -0.0, 1.0, -1.5, 0.5, 2.5, 3.5, 0.125, 1e-5, 123456.789, 1e100, -1e-300,
                                   3.141592653589793, 2.718281828459045, 1e21, 0.1, 0.3, 9.9999999, 999.9995,
                                   5e-324, DBL_MAX, DBL_MIN, 1.0 / 3.0, 100.0, 0.000123456};
    for (size_t i = 0; i < sizeof ffmts / sizeof *ffmts; i++)
        for (size_t j = 0; j < sizeof fvals / sizeof *fvals; j++) CHECK_FMT(ffmts[i], fvals[j]);

    char buf[128];
    kst_snprintf(buf, sizeof buf, "%a %a %a", 1.0, -1.5, 0.0);
    CHECK_STR(buf, "0x1p+0 -0x1.8p+0 0x0p+0");
    kst_snprintf(buf, sizeof buf, "%f %F %e", INFINITY, -INFINITY, NAN);
    CHECK(strstr(buf, "inf") && strstr(buf, "-INF") && strstr(buf, "nan"));

    CHECK_FMT("%s", "hello");
    CHECK_FMT("%10s|", "hi");
    CHECK_FMT("%-10s|", "hi");
    CHECK_FMT("%.3s", "abcdef");
    CHECK_FMT("%c", 'A');
    CHECK_FMT("%5c|", 'z');
    CHECK_FMT("%%%d%%", 5);

    kst_snprintf(buf, sizeof buf, "%*d|%-*d|%.*f", 6, 42, 4, 7, 2, 3.14159);
    CHECK_STR(buf, "    42|7   |3.14");

    /* Kannada digits */
    kst_snprintf(buf, sizeof buf, "%~d", 2024);
    CHECK_STR(buf, "೨೦೨೪");
    kst_snprintf(buf, sizeof buf, "%~.2f", 3.14159);
    CHECK_STR(buf, "೩.೧೪");
    kst_snprintf(buf, sizeof buf, "%~05d", -42);
    CHECK_STR(buf, "-೦೦೪೨");

    /* UTF-8 */
    kst_snprintf(buf, sizeof buf, "%lc", 0x0C95);
    CHECK_STR(buf, "ಕ");
    kst_snprintf(buf, sizeof buf, "[%.2s]", "ಕನ್ನಡ"); /* precision counts characters */
    CHECK_STR(buf, "[ಕನ]");
    kst_snprintf(buf, sizeof buf, "[%5s|%-4s]", "ಕನ್ನಡ", "ಅ"); /* so does the width */
    CHECK_STR(buf, "[ಕನ್ನಡ|ಅ   ]");
    kst_snprintf(buf, sizeof buf, "%s", (char *)NULL);
    CHECK_STR(buf, "(ಖಾಲಿ)");

    /* truncation and return value */
    int r = kst_snprintf(buf, 5, "%s", "abcdefgh");
    CHECK(r == 8 && strcmp(buf, "abcd") == 0);
    r = kst_snprintf(NULL, 0, "%d", 12345);
    CHECK(r == 5);

    int n = 0;
    kst_snprintf(buf, sizeof buf, "abc%n", &n);
    CHECK(n == 3);

    char *s = kst_aprintf("%s-%d-%.1f", "x", 7, 2.25);
    CHECK_STR(s, "x-7-2.2");
    kst_free(s);
}

static void test_scanf(void) {
    int a = 0, b = 0;
    long long ll = 0;
    double d = 0;
    float f = 0;
    char w[32], w2[32];

    CHECK(kst_sscanf("12 34", "%d %d", &a, &b) == 2 && a == 12 && b == 34);
    CHECK(kst_sscanf("೧೨೩ ೪೫", "%d%d", &a, &b) == 2 && a == 123 && b == 45);
    CHECK(kst_sscanf("-0x1F", "%i", &a) == 1 && a == -31);
    CHECK(kst_sscanf("017", "%i", &a) == 1 && a == 15);
    CHECK(kst_sscanf("ff", "%x", &a) == 1 && a == 255);
    CHECK(kst_sscanf("9223372036854775807", "%lld", &ll) == 1 && ll == 9223372036854775807LL);
    CHECK(kst_sscanf("3.25e2", "%lf", &d) == 1 && d == 325.0);
    CHECK(kst_sscanf("೩.೧೪", "%lf", &d) == 1 && d == 3.14);
    CHECK(kst_sscanf("1.5", "%f", &f) == 1 && f == 1.5f);
    CHECK(kst_sscanf("inf", "%lf", &d) == 1 && isinf(d));
    CHECK(kst_sscanf("0x1.8p1", "%lf", &d) == 1 && d == 3.0);
    CHECK(kst_sscanf("hello world", "%s %s", w, w2) == 2 && !strcmp(w, "hello") && !strcmp(w2, "world"));
    CHECK(kst_sscanf("ಕನ್ನಡ ಭಾಷೆ", "%s", w) == 1 && !strcmp(w, "ಕನ್ನಡ"));
    CHECK(kst_sscanf("abc,def", "%[^,],%s", w, w2) == 2 && !strcmp(w, "abc") && !strcmp(w2, "def"));
    CHECK(kst_sscanf("12345", "%3d%d", &a, &b) == 2 && a == 123 && b == 45);
    CHECK(kst_sscanf("", "%d", &a) == KST_EOF);
    CHECK(kst_sscanf("abc", "%d", &a) == 0);
    CHECK(kst_sscanf("10:20", "%d:%d", &a, &b) == 2 && a == 10 && b == 20);
    CHECK(kst_sscanf("x 5", "%*s %d", &a) == 1 && a == 5);
    CHECK(kst_sscanf("100%", "%d%%", &a) == 1 && a == 100);
    int cp = 0;
    CHECK(kst_sscanf("ಅ", "%lc", &cp) == 1 && cp == 0x0C85);
    int n = 0;
    CHECK(kst_sscanf("ab  cd", "%s %n", w, &n) == 1 && n == 4);
    /* width must not split a UTF-8 character */
    CHECK(kst_sscanf("ಕನ್ನಡ", "%4s", w) == 1 && !strcmp(w, "ಕ"));
}

static void test_files(void) {
    const char *path = "kst_selftest_ಕಡತ.txt";
    KFILE *f = kst_fopen(path, "w");
    CHECK(f != NULL);
    if (!f) return;
    kst_fprintf(f, "ಸಾಲು %d\n", 1);
    kst_fputs("second line\n", f);
    kst_fputc('X', f);
    CHECK(kst_fclose(f) == 0);

    f = kst_fopen(path, "r");
    CHECK(f != NULL);
    if (!f) return;
    char line[64];
    CHECK(kst_fgets(line, sizeof line, f) && !strcmp(line, "ಸಾಲು 1\n"));
    CHECK(kst_fgets(line, sizeof line, f) && !strcmp(line, "second line\n"));
    CHECK(kst_fgetc(f) == 'X');
    CHECK(kst_fgetc(f) == KST_EOF && kst_feof(f));
    kst_rewind(f);
    CHECK(!kst_feof(f));
    CHECK(kst_fgetcp(f) == 0x0CB8);
    long long pos = kst_ftell(f);
    CHECK(pos == 3);
    CHECK(kst_ungetc('Q', f) == 'Q' && kst_fgetc(f) == 'Q');
    kst_fseek(f, 0, KST_SEEK_END);
    long long size = kst_ftell(f);
    CHECK(size > 20);
    kst_fclose(f);

    f = kst_fopen(path, "a+");
    CHECK(f != NULL);
    if (f) {
        kst_fputs("Y", f);
        kst_fseek(f, 0, KST_SEEK_SET);
        char *l = kst_getline(f);
        CHECK(l && !strcmp(l, "ಸಾಲು 1"));
        kst_free(l);
        kst_fclose(f);
    }

    f = kst_fopen(path, "rb");
    if (f) {
        char all[128];
        long long got = kst_fread(all, 1, sizeof all, f);
        CHECK(got > 0 && all[got - 1] == 'Y');
        kst_fclose(f);
    }
    CHECK(kst_rename(path, "kst_selftest2.txt") == 0);
    CHECK(kst_file_exists("kst_selftest2.txt"));
    CHECK(kst_remove("kst_selftest2.txt") == 0);
    CHECK(!kst_file_exists("kst_selftest2.txt"));
    CHECK(kst_fopen("does/not/exist.txt", "r") == NULL && kst_errno == KST_ENOENT);

    KFILE *t = kst_tmpfile();
    CHECK(t != NULL);
    if (t) {
        kst_fprintf(t, "%d %s", 99, "tmp");
        kst_rewind(t);
        int v = 0;
        char word[16];
        CHECK(kst_fscanf(t, "%d %15s", &v, word) == 2 && v == 99 && !strcmp(word, "tmp"));
        kst_fclose(t);
    }
}

static void test_strings(void) {
    CHECK(kst_utf8_len("ಕನ್ನಡ") == 5);
    char *s = kst_substr("ಕನ್ನಡ ಭಾಷೆ", 0, 2);
    CHECK_STR(s, "ಕನ");
    kst_free(s);
    s = kst_str_replace("a-b-c", "-", "+");
    CHECK_STR(s, "a+b+c");
    kst_free(s);
    s = kst_str_trim("  hi  ");
    CHECK_STR(s, "hi");
    kst_free(s);
    CHECK(kst_atoi("೪೨") == 42);
    CHECK(kst_str_find("hello", "ll") == 2);
    s = kst_float_to_str(0.1);
    CHECK_STR(s, "0.1");
    kst_free(s);
}

int main(void) {
    kst_rt_init(0, NULL);
    test_printf();
    test_scanf();
    test_files();
    test_strings();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
