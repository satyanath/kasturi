# Kasturi internals

This note is for people working on the compiler or the runtime.

## Pipeline

```
.kst source ──► lexer ──► parser ──► AST ──► sema ──► codegen ──► C source ──► cc ──► executable
                                      ▲                                      │
                  ಆಮದು (imports) ─────┘                  runtime library ◄────┘
```

1. **Driver** (`src/main.c`) parses options and loads the main file. It
   resolves `ಆಮದು` imports recursively, loading each file once and keeping
   all of them in one `Program`. It then runs sema and codegen, writes `prog.c`
   into a temporary work directory, and calls the C compiler (`compile_c`).
   On Windows it locates Visual Studio through `vswhere` and loads `vcvars64.bat`.
   It passes short ASCII paths to compilers that cannot handle UTF-8 file names.
2. **Lexer** (`src/lexer.c`) decodes UTF-8. Kannada letters, vowel signs,
   virama, ZWJ and ZWNJ are identifier characters. Keywords are matched as
   whole identifiers. Kannada digits are converted to values in number literals.
   A character literal holding a non-ASCII character becomes its code point and
   is marked `wide`.
3. **Parser** (`src/parser.c`) is recursive descent and follows `grammar.txt`.
   A parenthesised expression at the start of a statement is parsed first and
   then classified by the keyword that follows (`ಆದರೆ` → if,
   `ಸಮಯವರೆಗೆ` → while), or treated as an expression statement. A name used
   as a type must have been declared with `ರಚನೆ` (any order, because struct
   names are collected first). After an error the parser skips to the next
   `;` or `}` and continues.
4. **Sema** (`src/sema.c`) runs in phases. First structs are registered, then
   global functions and variables (so order does not matter), then struct
   members (which may use global constants as array sizes), and finally
   function bodies. Sema resolves names, inserts implicit casts (`coerce`),
   folds constants (`const_eval`: array sizes, case labels, global
   initialisers), and marks string comparisons. It assigns C names through
   `src/mangle.c`.
5. **Codegen** (`src/codegen.c`) prints C99. It emits forward declarations
   of all structs, then struct definitions in dependency order, prototypes,
   globals and functions. `ಮುದ್ರಿಸು`/`ಓದು` become calls to the runtime's
   `kst_print_*` / `kst_read_*` helpers. `main` calls `kst_rt_init` and
   then the program's `ಮುಖ್ಯ`.

### Name mangling

Kannada identifiers are transliterated to readable ASCII (ISO-15919-like):
`ಮುಖ್ಯ` → `k_mukhya`. The prefix shows the kind of name: `k_` for functions
and variables, `ks_` for structs, `f_` for fields. If two names transliterate
to the same text, the second gets a numeric suffix. `ಬಾಹ್ಯ "cname"`
declarations use the given C name unchanged.

### Diagnostics

`src/diag.c` prints `file:line:col: ದೋಷ: message`, followed by the source
line and a caret. Columns are counted in code points. All messages are in
Kannada.

## Runtime

The runtime (`runtime/`) does not use the C library's `FILE`.

| File | Responsibility |
|------|----------------|
| `kst_os.c` | Thin OS layer: open/read/write/seek/close on handles, UTF-8 ↔ UTF-16 on Windows, console detection and `WriteConsoleW`/`ReadConsoleW`, temp files, environment, `system`, UTF-8 `argv` |
| `kst_stdio.c` | `KFILE` streams: buffering modes, ungetc stack, text-mode CRLF translation (including a CR at a buffer boundary), read/write switching, positioning, error/EOF flags, the list of open streams for `fflush(NULL)` and exit |
| `kst_printf.c` | Formatting engine with pluggable sinks (stream, fixed buffer, growing buffer). Exact binary-to-decimal conversion (big-integer arithmetic, round-half-even) for `%f %e %g`, and shortest `%a` |
| `kst_scanf.c` | Scanning engine over stream or string sources with a push-back buffer. Kannada digits are mapped to ASCII as they are read |
| `kst_core.c` | Start-up (`kst_rt_init`), helpers for `ಮುದ್ರಿಸು`/`ಓದು`, string comparison, Kannada `strerror`, and the wrappers behind `ಸಾಮಾನ್ಯ`, `ಪದಗಳು` and `ಗಣಿತ` |

The console is handled specially on Windows. Output to a console handle is
decoded from UTF-8 and written with `WriteConsoleW`, so Kannada text appears
correctly whatever the code page. Incomplete UTF-8 sequences are carried over
between writes. Console input is read with `ReadConsoleW` and converted to UTF-8.

## Tests

* `tests/cases/` – end-to-end programs with expected output.
* `tests/errors/` – programs that must fail, with the expected message.
* `tests/rt_selftest.c` – compares the runtime's `printf`/`scanf` engines with
  the host C library on several hundred cases, and checks file I/O edge
  cases.

Run `tests\run_tests.bat -Update` (Windows) to regenerate `.out` files after
an intended change in output. Review the diff before committing.

## Adding a library function

1. Implement it in C in the runtime (usually `kst_core.c`) and declare it in
   `kasturi_rt.h`.
2. Declare it in the matching `lib/*.kst` module:
   `ಬಾಹ್ಯ "kst_name" ಕ್ರಿಯೆ ಕನ್ನಡ_ಹೆಸರು(...) ಫಲಿತಾಂಶ ...;`
3. Rebuild (`build.bat`) so that the prebuilt runtime library includes it.
4. Document it in `docs/ಗ್ರಂಥಾಲಯ_ಉಲ್ಲೇಖ.md` and add a test case.

Functions that need no C code can be written in Kasturi directly in the
module, like `ಲಸಾಅ` in `lib/ಗಣಿತ.kst`.
