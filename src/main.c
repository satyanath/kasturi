/* main.c - the 'kasturi' compiler driver */
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "codegen.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "util.h"

#define KASTURI_VERSION "1.0.0"

#ifdef _WIN32
#define EXE_SUFFIX ".exe"
#else
#define EXE_SUFFIX ""
#endif

static const char *runtime_sources[] = {"kst_os.c", "kst_stdio.c", "kst_printf.c", "kst_scanf.c", "kst_core.c"};
#define N_RUNTIME_SOURCES (sizeof runtime_sources / sizeof runtime_sources[0])

typedef enum CcKind { CC_GCC, CC_CLANG, CC_MSVC } CcKind;

typedef struct Options {
    const char *input;
    const char *output;
    const char *cc;
    bool emit_c;
    bool keep_c;
    bool run;
    bool check_only;
    bool dump_tokens;
    bool debug;
    bool rt_from_source;
    bool verbose;
    const char *opt;
    Vec include_dirs;
    Vec cc_args;
    Vec run_args;
} Options;

static Options opts;
static char *g_home;
static Vec g_loaded;

/* ------------------------------------------------------------------ */
/* help                                                                */
/* ------------------------------------------------------------------ */

static void print_version(void) {
    con_printf(stdout, "ಕಸ್ತೂರಿ (Kasturi) ಸಂಕಲಕ ಆವೃತ್ತಿ %s\n", KASTURI_VERSION);
}

static void print_help(void) {
    print_version();
    con_printf(stdout,
               "\n"
               "ಬಳಕೆ (usage): kasturi [ಆಯ್ಕೆಗಳು] <ಕಡತ.kst> [-- ಪ್ರೋಗ್ರಾಂ-ವಾದಗಳು]\n"
               "\n"
               "ಆಯ್ಕೆಗಳು (options):\n"
               "  -o <ಕಡತ>        ಹೊರಫಲ ಕಡತದ ಹೆಸರು                 output file name\n"
               "  -c, --emit-c    C ಮೂಲ ಕಡತವನ್ನು ಮಾತ್ರ ರಚಿಸು          only generate C source\n"
               "  -r, --run       ಸಂಕಲಿಸಿ ತಕ್ಷಣ ಚಲಾಯಿಸು               compile and run\n"
               "      --check     ದೋಷಗಳನ್ನು ಮಾತ್ರ ಪರಿಶೀಲಿಸು            check only, no output\n"
               "  -I <ಕೋಶ>        'ಆಮದು' ಹುಡುಕುವ ಕೋಶ ಸೇರಿಸು          add an import directory\n"
               "      --cc <ಸಂಕಲಕ> C ಸಂಕಲಕ: gcc | clang | cl | ಮಾರ್ಗ    C compiler to use\n"
               "      --cc-arg <a> C ಸಂಕಲಕಕ್ಕೆ ಹೆಚ್ಚುವರಿ ವಾದ            extra C compiler argument\n"
               "      --keep-c    ರಚಿತ C ಕಡತವನ್ನು ಉಳಿಸು               keep the generated C file\n"
               "      --rt-src    ರನ್‌ಟೈಮ್ ಅನ್ನು ಮೂಲದಿಂದ ಸಂಕಲಿಸು       compile runtime from source\n"
               "  -g              ದೋಷನಿವಾರಣೆ ಮಾಹಿತಿ (debug info)\n"
               "  -O0 / -O1 / -O2 ಅನುಕೂಲೀಕರಣ ಮಟ್ಟ (ಪೂರ್ವನಿಯೋಜಿತ -O2)  optimisation level\n"
               "  -w              ಎಚ್ಚರಿಕೆಗಳನ್ನು ಮರೆಮಾಡು                hide warnings\n"
               "      --Werror    ಎಚ್ಚರಿಕೆಗಳನ್ನು ದೋಷಗಳಾಗಿ ಪರಿಗಣಿಸು       treat warnings as errors\n"
               "      --tokens    ಟೋಕನ್‌ಗಳನ್ನು ತೋರಿಸು                  dump tokens\n"
               "  -v, --verbose   C ಸಂಕಲಕ ಆದೇಶಗಳನ್ನು ತೋರಿಸು            show C compiler commands\n"
               "      --version   ಆವೃತ್ತಿ                               show version\n"
               "  -h, --help      ಈ ಸಹಾಯ                               this help\n"
               "\n"
               "ಪರಿಸರ ಚರಗಳು: KASTURI_HOME (ಸ್ಥಾಪನಾ ಕೋಶ), KASTURI_CC (C ಸಂಕಲಕ)\n");
}

/* ------------------------------------------------------------------ */
/* loading source files                                                */
/* ------------------------------------------------------------------ */

static char *resolve_import(const char *name, const char *from_dir) {
    char *file = str_ends_with(name, ".kst") ? xstrdup(name) : xformat("%s.kst", name);
    char *candidate = path_join(from_dir, file);
    if (file_exists(candidate)) {
        free(file);
        return candidate;
    }
    free(candidate);
    for (int i = 0; i < opts.include_dirs.len; i++) {
        candidate = path_join(opts.include_dirs.items[i], file);
        if (file_exists(candidate)) {
            free(file);
            return candidate;
        }
        free(candidate);
    }
    if (g_home) {
        char *lib = path_join(g_home, "lib");
        candidate = path_join(lib, file);
        free(lib);
        if (file_exists(candidate)) {
            free(file);
            return candidate;
        }
        free(candidate);
    }
    free(file);
    return NULL;
}

static bool load_file(const char *path, const char *display, Program *prog) {
    char *abs = path_absolute(path);
    for (int i = 0; i < g_loaded.len; i++)
        if (strcmp(g_loaded.items[i], abs) == 0) {
            free(abs);
            return true;
        }
    vec_push(&g_loaded, abs);

    SrcFile *sf = xcalloc(1, sizeof(SrcFile));
    sf->path = xstrdup(display);
    sf->text = read_file(abs, &sf->len);
    if (!sf->text) {
        con_printf(stderr, "kasturi: ದೋಷ: '%s' ಕಡತವನ್ನು ತೆರೆಯಲಾಗಲಿಲ್ಲ\n", display);
        return false;
    }
    int ntok;
    Token *toks = lex_file(sf, &ntok);
    if (opts.dump_tokens) {
        for (int i = 0; i < ntok; i++) {
            Token *t = &toks[i];
            con_printf(stdout, "%s:%d:%d\t%s", sf->path, t->loc.line, t->loc.col, tok_spelling(t->kind));
            if (t->kind == T_ID) con_printf(stdout, "\t%s", t->text);
            if (t->kind == T_INT || t->kind == T_CHAR) con_printf(stdout, "\t%lld", t->ival);
            if (t->kind == T_FLOAT) con_printf(stdout, "\t%s", t->text);
            if (t->kind == T_STR) {
                con_printf(stdout, "\t\"");
                con_write(stdout, t->text, t->len);
                con_printf(stdout, "\"");
            }
            con_printf(stdout, "\n");
        }
    }
    parser_register_structs(toks, ntok);

    Parser p;
    parser_init(&p, toks, ntok);
    Vec imports = {0};
    parse_imports(&p, &imports);
    char *dir = path_dirname(abs);
    for (int i = 0; i < imports.len; i++) {
        Token *t = imports.items[i];
        char *found = resolve_import(t->text, dir);
        if (!found) {
            diag_error(t->loc, "ಆಮದು ಘಟಕ '%s' ಕಂಡುಬಂದಿಲ್ಲ", t->text);
            continue;
        }
        char *found_abs = path_absolute(found);
        load_file(found_abs, found, prog);
        free(found_abs);
        free(found);
    }
    free(dir);
    parse_file_body(&p, prog);
    return true;
}

/* ------------------------------------------------------------------ */
/* C compiler selection and invocation                                 */
/* ------------------------------------------------------------------ */

static CcKind cc_kind(const char *cc) {
    char *base = path_basename(cc);
    for (char *p = base; *p; p++)
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    CcKind k = CC_GCC;
    if (strcmp(base, "cl") == 0 || strcmp(base, "cl.exe") == 0) k = CC_MSVC;
    else if (strstr(base, "clang")) k = CC_CLANG;
    free(base);
    return k;
}

#ifdef _WIN32
/* Locates vcvars64.bat of the newest Visual Studio with C++ tools. */
static char *find_vcvars(void) {
    char *pf86 = get_env("ProgramFiles(x86)");
    if (pf86) {
        char *vswhere = xformat("%s\\Microsoft Visual Studio\\Installer\\vswhere.exe", pf86);
        if (file_exists(vswhere)) {
            char *cmd = xformat("\"%s\" -latest -products * -requires "
                                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath",
                                vswhere);
            char *inst = capture_command(cmd);
            free(cmd);
            if (inst) {
                char *bat = xformat("%s\\VC\\Auxiliary\\Build\\vcvars64.bat", inst);
                free(inst);
                if (file_exists(bat)) {
                    free(vswhere);
                    free(pf86);
                    return bat;
                }
                free(bat);
            }
        }
        free(vswhere);
        free(pf86);
    }
    static const char *editions[] = {"Community", "Professional", "Enterprise", "BuildTools"};
    static const char *years[] = {"2022", "2019"};
    for (size_t y = 0; y < 2; y++)
        for (size_t e = 0; e < 4; e++) {
            const char *roots[] = {"C:\\Program Files\\Microsoft Visual Studio",
                                   "C:\\Program Files (x86)\\Microsoft Visual Studio"};
            for (size_t r = 0; r < 2; r++) {
                char *bat = xformat("%s\\%s\\%s\\VC\\Auxiliary\\Build\\vcvars64.bat", roots[r], years[y],
                                    editions[e]);
                if (file_exists(bat)) return bat;
                free(bat);
            }
        }
    return NULL;
}
#endif

typedef struct CcChoice {
    char *cc;      /* command */
    CcKind kind;
    char *prefix;  /* environment setup, e.g. 'call vcvars64.bat && ' */
} CcChoice;

static bool try_cc(const char *name, CcChoice *out) {
    CcKind k = cc_kind(name);
    char *found = find_in_path(name);
    if (found) {
        free(found);
        out->cc = xstrdup(name);
        out->kind = k;
        out->prefix = xstrdup("");
        return true;
    }
#ifdef _WIN32
    if (k == CC_MSVC) {
        char *bat = find_vcvars();
        if (bat) {
            out->cc = xstrdup("cl");
            out->kind = CC_MSVC;
            out->prefix = xformat("call \"%s\" >nul 2>&1 && ", bat);
            free(bat);
            return true;
        }
    }
#endif
    return false;
}

static bool choose_cc(CcChoice *out) {
    const char *requested = opts.cc;
    char *env = NULL;
    if (!requested) {
        env = get_env("KASTURI_CC");
        if (env && *env) requested = env;
    }
    if (requested) {
        if (try_cc(requested, out)) return true;
        /* a full path that exists */
        if (file_exists(requested)) {
            out->cc = xstrdup(requested);
            out->kind = cc_kind(requested);
            out->prefix = xstrdup("");
            return true;
        }
        con_printf(stderr, "kasturi: ದೋಷ: C ಸಂಕಲಕ '%s' ಕಂಡುಬಂದಿಲ್ಲ\n", requested);
        return false;
    }
#if defined(_MSC_VER)
    static const char *order[] = {"cl", "gcc", "clang", "cc"};
#elif defined(__clang__)
    static const char *order[] = {"clang", "gcc", "cc", "cl"};
#else
    static const char *order[] = {"gcc", "clang", "cc", "cl"};
#endif
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++)
        if (try_cc(order[i], out)) return true;
    con_printf(stderr,
               "kasturi: ದೋಷ: ಯಾವುದೇ C ಸಂಕಲಕ (gcc, clang, cl) ಕಂಡುಬಂದಿಲ್ಲ.\n"
               "  ಒಂದನ್ನು ಸ್ಥಾಪಿಸಿ ಅಥವಾ --cc / KASTURI_CC ಬಳಸಿ. (--emit-c ಮೂಲಕ C ಕಡತವನ್ನು ಮಾತ್ರ ರಚಿಸಬಹುದು)\n");
    return false;
}

static void q(StrBuf *sb, const char *s) {
    sb_putc(sb, '"');
    sb_puts(sb, s);
    sb_putc(sb, '"');
    sb_putc(sb, ' ');
}

static bool compile_c(const char *c_file, const char *exe_file, const char *work_dir) {
    CcChoice cc;
    if (!choose_cc(&cc)) return false;
    char *home = g_home ? ascii_safe_path(g_home) : xstrdup(".");
    char *rt_dir = path_join(home, "runtime");
    char *lib_dir = path_join(home, "lib");
    char *rt_lib = path_join(lib_dir, cc.kind == CC_MSVC ? "kasturi_rt.lib" : "libkasturi_rt.a");
    bool use_lib = !opts.rt_from_source && file_exists(rt_lib);
    char *log = path_join(work_dir, "cc.log");

    StrBuf cmd;
    sb_init(&cmd);
    sb_puts(&cmd, cc.prefix);
    if (cc.kind == CC_MSVC) {
        sb_puts(&cmd, "cl /nologo /utf-8 /std:c11 /w ");
        sb_puts(&cmd, opts.debug ? "/Zi /Od " : (strcmp(opts.opt, "0") == 0 ? "/Od " : "/O2 "));
        sb_printf(&cmd, "/I\"%s\" ", rt_dir);
        q(&cmd, c_file);
        if (use_lib) {
            q(&cmd, rt_lib);
        } else {
            for (size_t i = 0; i < N_RUNTIME_SOURCES; i++) {
                char *src = path_join(rt_dir, runtime_sources[i]);
                q(&cmd, src);
                free(src);
            }
        }
        for (int i = 0; i < opts.cc_args.len; i++) sb_printf(&cmd, "%s ", (char *)opts.cc_args.items[i]);
        sb_printf(&cmd, "/Fo\"%s\\\\\" /Fd\"%s\\\\\" /Fe\"%s\" /link /NOLOGO ", work_dir, work_dir, exe_file);
    } else {
        sb_printf(&cmd, "\"%s\" -std=c99 -w ", cc.cc);
        sb_puts(&cmd, opts.debug ? "-g -O0 " : "");
        if (!opts.debug) sb_printf(&cmd, "-O%s ", opts.opt);
        sb_printf(&cmd, "-I\"%s\" ", rt_dir);
        q(&cmd, c_file);
        if (use_lib) {
            q(&cmd, rt_lib);
        } else {
            for (size_t i = 0; i < N_RUNTIME_SOURCES; i++) {
                char *src = path_join(rt_dir, runtime_sources[i]);
                q(&cmd, src);
                free(src);
            }
        }
        /* after the program so that libraries given here resolve its symbols */
        for (int i = 0; i < opts.cc_args.len; i++) sb_printf(&cmd, "%s ", (char *)opts.cc_args.items[i]);
        sb_printf(&cmd, "-o \"%s\" -lm ", exe_file);
    }
    sb_printf(&cmd, "> \"%s\" 2>&1", log);

    if (opts.verbose) con_printf(stderr, "%s\n", cmd.data);
    int rc = run_command(cmd.data);
    bool ok = rc == 0 && file_exists(exe_file);
    if (!ok || opts.verbose) {
        size_t n;
        char *text = read_file(log, &n);
        if (!ok) con_printf(stderr, "kasturi: ದೋಷ: C ಸಂಕಲನ ವಿಫಲವಾಯಿತು (ನಿರ್ಗಮನ ಸಂಕೇತ %d)\n", rc);
        if (text && n) con_write(stderr, text, n);
        free(text);
    }
    remove_file(log);
    sb_free(&cmd);
    free(home);
    free(rt_dir);
    free(lib_dir);
    free(rt_lib);
    free(log);
    return ok;
}

static void cleanup_work_dir(const char *work_dir) {
    static const char *names[] = {"prog.c", "prog.obj", "prog.o", "prog" EXE_SUFFIX, "prog.pdb", "prog.ilk",
                                  "vc140.pdb", "cc.log", "kst_os.obj", "kst_stdio.obj", "kst_printf.obj",
                                  "kst_scanf.obj", "kst_core.obj"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char *p = path_join(work_dir, names[i]);
        if (file_exists(p)) remove_file(p);
        free(p);
    }
    remove_dir(work_dir);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static char *find_home(const char *argv0) {
    char *env = get_env("KASTURI_HOME");
    if (env && *env) return env;
    char *exe = exe_path(argv0);
    if (!exe) return NULL;
    char *dir = path_dirname(exe);
    free(exe);
    char *base = path_basename(dir);
    char *home;
    if (strcmp(base, "bin") == 0) {
        home = path_dirname(dir);
        free(dir);
    } else {
        home = dir;
    }
    free(base);
    return home;
}

static void parse_args(int argc, char **argv) {
    opts.opt = "2";
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) {
            for (int j = i + 1; j < argc; j++) vec_push(&opts.run_args, argv[j]);
            break;
        }
#define NEED_VALUE()                                                                   \
    do {                                                                               \
        if (i + 1 >= argc) fatal("'%s' ಆಯ್ಕೆಗೆ ಮೌಲ್ಯ ಬೇಕು", a);                         \
    } while (0)
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            print_help();
            exit(0);
        } else if (strcmp(a, "--version") == 0) {
            print_version();
            exit(0);
        } else if (strcmp(a, "-o") == 0) {
            NEED_VALUE();
            opts.output = argv[++i];
        } else if (strcmp(a, "-c") == 0 || strcmp(a, "--emit-c") == 0) {
            opts.emit_c = true;
        } else if (strcmp(a, "-r") == 0 || strcmp(a, "--run") == 0) {
            opts.run = true;
        } else if (strcmp(a, "--check") == 0) {
            opts.check_only = true;
        } else if (strcmp(a, "-I") == 0) {
            NEED_VALUE();
            vec_push(&opts.include_dirs, argv[++i]);
        } else if (strncmp(a, "-I", 2) == 0) {
            vec_push(&opts.include_dirs, (void *)(a + 2));
        } else if (strcmp(a, "--cc") == 0) {
            NEED_VALUE();
            opts.cc = argv[++i];
        } else if (strcmp(a, "--cc-arg") == 0) {
            NEED_VALUE();
            vec_push(&opts.cc_args, argv[++i]);
        } else if (strcmp(a, "--keep-c") == 0) {
            opts.keep_c = true;
        } else if (strcmp(a, "--rt-src") == 0) {
            opts.rt_from_source = true;
        } else if (strcmp(a, "-g") == 0) {
            opts.debug = true;
        } else if (strcmp(a, "-O0") == 0 || strcmp(a, "-O1") == 0 || strcmp(a, "-O2") == 0 ||
                   strcmp(a, "-O3") == 0) {
            opts.opt = a + 2;
        } else if (strcmp(a, "-w") == 0) {
            diag_set_warnings(0);
        } else if (strcmp(a, "--Werror") == 0) {
            diag_set_warnings_as_errors(1);
        } else if (strcmp(a, "--tokens") == 0) {
            opts.dump_tokens = true;
        } else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            opts.verbose = true;
        } else if (a[0] == '-' && a[1]) {
            fatal("ಅಜ್ಞಾತ ಆಯ್ಕೆ '%s' (ಸಹಾಯಕ್ಕೆ kasturi --help)", a);
        } else if (!opts.input) {
            opts.input = a;
        } else if (opts.run) {
            vec_push(&opts.run_args, (void *)a);
        } else {
            fatal("ಒಂದೇ ಮೂಲ ಕಡತವನ್ನು ನೀಡಿ ('%s' ಹೆಚ್ಚುವರಿ)", a);
        }
#undef NEED_VALUE
    }
}

static void q_arg(StrBuf *sb, const char *s) {
    sb_putc(sb, ' ');
#ifdef _WIN32
    sb_putc(sb, '"');
    for (const char *p = s; *p; p++) {
        if (*p == '"') sb_putc(sb, '\\');
        sb_putc(sb, *p);
    }
    sb_putc(sb, '"');
#else
    sb_putc(sb, '\'');
    for (const char *p = s; *p; p++) {
        if (*p == '\'') sb_puts(sb, "'\\''");
        else sb_putc(sb, *p);
    }
    sb_putc(sb, '\'');
#endif
}

int main(int argc, char **argv) {
    int uargc;
    char **uargv = utf8_args(argc, argv, &uargc);
    parse_args(uargc, uargv);
    if (!opts.input) {
        print_help();
        return 1;
    }
    g_home = find_home(uargv[0]);

    Program prog;
    memset(&prog, 0, sizeof prog);
    if (!load_file(opts.input, opts.input, &prog)) return 1;
    if (diag_error_count() > 0) {
        con_printf(stderr, "kasturi: %d ದೋಷ(ಗಳು) ಕಂಡುಬಂದಿವೆ.\n", diag_error_count());
        return 1;
    }
    bool need_main = !opts.check_only && !opts.emit_c;
    if (!sema_check(&prog, need_main)) {
        con_printf(stderr, "kasturi: %d ದೋಷ(ಗಳು) ಕಂಡುಬಂದಿವೆ.\n", diag_error_count());
        return 1;
    }
    if (opts.check_only) return 0;

    char *c_code = codegen_program(&prog, sema_main_function(), opts.input);
    char *stem = path_strip_ext(opts.input);

    if (opts.emit_c) {
        char *out = opts.output ? xstrdup(opts.output) : xformat("%s.c", stem);
        if (!write_file(out, c_code, strlen(c_code))) fatal("'%s' ಕಡತವನ್ನು ಬರೆಯಲಾಗಲಿಲ್ಲ", out);
        return 0;
    }

    char *exe = opts.output ? xstrdup(opts.output) : xformat("%s%s", stem, EXE_SUFFIX);
#ifdef _WIN32
    if (!str_ends_with(exe, ".exe") && !str_ends_with(exe, ".EXE")) {
        char *e2 = xformat("%s.exe", exe);
        free(exe);
        exe = e2;
    }
#endif
    if (opts.keep_c) {
        char *cpath = xformat("%s.c", stem);
        write_file(cpath, c_code, strlen(c_code));
        free(cpath);
    }

    char *tmp = temp_directory();
    char *work_name = xformat("kasturi_%d", current_pid());
    char *work = path_join(tmp, work_name);
    make_dir(work);
    char *c_file = path_join(work, "prog.c");
    char *tmp_exe = path_join(work, "prog" EXE_SUFFIX);
    if (!write_file(c_file, c_code, strlen(c_code))) fatal("ತಾತ್ಕಾಲಿಕ ಕಡತ '%s' ಬರೆಯಲಾಗಲಿಲ್ಲ", c_file);

    /* "-r" without "-o": build and run in the work directory, keep nothing */
    bool run_from_temp = opts.run && !opts.output;
    bool ok = compile_c(c_file, tmp_exe, work);
    if (ok && !run_from_temp && !move_file(tmp_exe, exe)) {
        con_printf(stderr, "kasturi: ದೋಷ: '%s' ಕಡತವನ್ನು ಬರೆಯಲಾಗಲಿಲ್ಲ\n", exe);
        ok = false;
    }
    if (!ok) {
        cleanup_work_dir(work);
        return 1;
    }

    if (opts.run) {
        StrBuf cmd;
        sb_init(&cmd);
        char *abs = path_absolute(run_from_temp ? tmp_exe : exe);
        sb_putc(&cmd, '"');
        sb_puts(&cmd, abs);
        sb_putc(&cmd, '"');
        for (int i = 0; i < opts.run_args.len; i++) q_arg(&cmd, opts.run_args.items[i]);
        int rc = run_command(cmd.data);
        cleanup_work_dir(work);
        return rc;
    }
    cleanup_work_dir(work);
    return 0;
}
