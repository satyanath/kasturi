# Using the Kasturi compiler

```
kasturi [options] <file.kst> [-- program arguments]
```

`kasturi` reads one Kasturi source file plus everything it imports, checks
it, generates C, and calls a C compiler to build a native executable.
All diagnostics are in Kannada.

---

## 1. Quick start

Create `ನಮಸ್ಕಾರ.kst` (save it as UTF-8):

```
ಕ್ರಿಯೆ ಮುಖ್ಯ() ಫಲಿತಾಂಶ ಸಂಖ್ಯೆ {
    ಸಾಲುಮುದ್ರಿಸು("ನಮಸ್ಕಾರ, ಜಗತ್ತೇ!");
    ಮರಳಿ 0;
}
```

```bat
kasturi ನಮಸ್ಕಾರ.kst          rem → ನಮಸ್ಕಾರ.exe
ನಮಸ್ಕಾರ.exe
kasturi -r ನಮಸ್ಕಾರ.kst       rem compile and run at once
```

---

## 2. Options

| Option | Meaning |
|--------|---------|
| `-o <file>` | Name of the output file. Default: the source name with `.exe` (Windows) or no extension (Linux/macOS); with `-c` the default is `<name>.c`. On Windows `.exe` is added if missing. |
| `-c`, `--emit-c` | Only generate the C translation; do not call a C compiler. A `ಮುಖ್ಯ` function is not required. |
| `-r`, `--run` | Compile and run the program. Without `-o` the executable is built in a temporary directory and deleted afterwards. The exit code of `kasturi` is the program's exit code. |
| `--check` | Only parse and type-check (no output). A `ಮುಖ್ಯ` function is not required, so library modules can be checked too. |
| `-I <dir>` | Add a directory to search for `ಆಮದು` (import). May be repeated. |
| `--cc <compiler>` | C compiler to use: `gcc`, `clang`, `cl`, `cc`, or a full path. |
| `--cc-arg <arg>` | Extra argument passed to the C compiler (repeatable), e.g. `--cc-arg -static`. |
| `--keep-c` | Also write the generated C next to the source (`<name>.c`). |
| `--rt-src` | Compile the runtime from `runtime/*.c` instead of linking the prebuilt library. |
| `-g` | Include debug information. |
| `-O0` … `-O3` | C optimisation level (default `-O2`). |
| `-w` | Do not show warnings. |
| `--Werror` | Treat warnings as errors. |
| `--tokens` | Print the token stream (for debugging the lexer). |
| `-v`, `--verbose` | Show the C compiler commands. |
| `--version` | Show the version. |
| `-h`, `--help` | Show help (Kannada and English). |
| `--` | Everything after `--` is passed to the program (with `-r`). With `-r`, arguments after the source file are passed to the program as well. |

### Environment variables

| Variable | Meaning |
|----------|---------|
| `KASTURI_HOME` | Installation directory (contains `lib/` and `runtime/`). |
| `KASTURI_CC` | Default C compiler (same values as `--cc`). |

### Choosing the C compiler

If neither `--cc` nor `KASTURI_CC` is given, `kasturi` first tries the
compiler it was itself built with, then the others:

| `kasturi` built with | Order tried |
|----------------------|-------------|
| MSVC | `cl`, `gcc`, `clang`, `cc` |
| clang | `clang`, `gcc`, `cc`, `cl` |
| gcc | `gcc`, `clang`, `cc`, `cl` |

On Windows, if `cl` is not on `PATH`, Visual Studio is located through
`vswhere` and its environment is loaded automatically.

---

## 3. Examples

```bat
kasturi -r examples\ಗುಣಾಕಾರ_ಕೋಷ್ಟಕ.kst
kasturi -r examples\ಅವಿಭಾಜ್ಯ_ಜರಡಿ.kst -- 500
kasturi -o wc.exe examples\ಪದ_ಎಣಿಕೆ.kst
wc.exe docs\USAGE.md
kasturi -c -o program.c program.kst        rem inspect the generated C
kasturi --cc gcc -O3 program.kst
kasturi --check lib\ಐಒ.kst
```

---

## 4. Imports and modules

```
ಆಮದು "ಐಒ";        // standard I/O
ಆಮದು "ಗಣಿತ";      // mathematics
ಆಮದು "ನನ್ನ_ಘಟಕ";   // your own module: ನನ್ನ_ಘಟಕ.kst
```

An import names a file without the `.kst` extension. It is searched for in:

1. the directory of the importing file;
2. each `-I` directory, in order;
3. `KASTURI_HOME/lib` (the standard library).

Every file is loaded only once, even if it is imported many times or the
imports are circular. All imported declarations are visible in the importing
program, and declarations may appear in any order.

Standard modules:

| Module | Contents | C equivalent |
|--------|----------|--------------|
| `ಐಒ` | files, streams, `printf`/`scanf` families, character I/O, positioning, errors | `<stdio.h>` |
| `ಸಾಮಾನ್ಯ` | memory, conversions, random numbers, time, process, sorting/searching | `<stdlib.h>`, `<time.h>` |
| `ಪದಗಳು` | string functions, character classes, UTF-8 / Kannada helpers | `<string.h>`, `<ctype.h>` |
| `ಗಣಿತ` | mathematical functions and constants | `<math.h>` |

The full reference (in Kannada) is in
[ಗ್ರಂಥಾಲಯ_ಉಲ್ಲೇಖ.md](ಗ್ರಂಥಾಲಯ_ಉಲ್ಲೇಖ.md).

---

## 5. Diagnostics

Errors and warnings are reported in Kannada with the file, line and column,
the source line and a caret:

```
ಉದಾಹರಣೆ.kst:3:18: ದೋಷ: 'ಮೌಲ್ಯ' ಅನ್ನು ಘೋಷಿಸಲಾಗಿಲ್ಲ
    3 |     ಸಾಲುಮುದ್ರಿಸು(ಮೌಲ್ಯ);
      |                  ^
kasturi: 1 ದೋಷ(ಗಳು) ಕಂಡುಬಂದಿವೆ.
```

* `ದೋಷ` is an error, `ಎಚ್ಚರಿಕೆ` is a warning, and `ಸೂಚನೆ` is a note.
* The parser recovers after an error, so several errors are reported at once.
  It stops after 30 errors.
* The exit code is 0 on success and 1 on any error.
* If the C compiler fails (which should not happen for a valid program), its
  output is shown.

---

## 6. Interfacing with C

A Kasturi program can call any C function by declaring it with `ಬಾಹ್ಯ`
(extern) and giving the C symbol name:

```
ಬಾಹ್ಯ "puts" ಕ್ರಿಯೆ ಸಿ_ಸಾಲು(ಪದ ಪ) ಫಲಿತಾಂಶ ಸಂಖ್ಯೆ;
ಬಾಹ್ಯ "my_counter" ಸಂಖ್ಯೆ ಎಣಿಕೆದಾರ;      // a global variable defined in C
```

Link your own C code with `--cc-arg`:

```bat
kasturi --cc-arg mylib.c program.kst
kasturi --cc gcc --cc-arg -lm --cc-arg mylib.o program.kst
```

Kasturi types correspond to C types as follows:

| Kasturi | C |
|---------|---|
| `ಸಂಖ್ಯೆ` | `int` |
| `ದೀರ್ಘ` | `long long` |
| `ದಶಾಂಶ` | `double` |
| `ಅಕ್ಷರ` | `char` |
| `ಪದ` | `char *` (UTF-8) |
| `ತಾರ್ಕಿಕ` | `unsigned char` (0 / 1) |
| `ಶೂನ್ಯ` | `void` |
| `ಚರಪಟ್ಟಿ` | `va_list` |
| `ರಚನೆ X` | `struct` (same layout as C) |
| `T*` | `T *` |

Kasturi programs use their own stream implementation (`kst_stdout`, …),
not the C library's `FILE` objects. If C code you link in writes with
`printf`/`puts`, its output goes through a separate buffer and can appear out
of order with Kasturi output. Call `fflush(stdout)` in C, or `ಕಡತ_ಹರಿಸು(ಖಾಲಿ)`
in Kasturi, at the points where order matters. From C, you can also call the
runtime directly (`kst_printf`, `kst_fputs`, … declared in
`runtime/kasturi_rt.h`).

The generated C is plain ASCII. Kannada identifiers are transliterated
(`ಮುಖ್ಯ` → `k_mukhya`, struct `ಬಿಂದು` → `ks_bindu`, field `ಕ` → `f_ka`).
Non-ASCII characters inside strings become octal escapes. Use `-c` to see
the translation.

---

## 7. Runtime behaviour

* Standard output is line-buffered when it is a terminal and fully buffered
  otherwise. Standard error is unbuffered. Before the program reads from
  standard input, standard output is flushed, so prompts always appear.
* On Windows the console is used through the Unicode API. Kannada text is
  printed and read correctly in any console code page. Ctrl+Z (Windows) or
  Ctrl+D (Linux) ends input.
* Program arguments are UTF-8 on every platform.
* File names are UTF-8: `ಕಡತ_ತೆರೆ("ವರದಿ.txt", "w")` works on Windows too.
* In text mode on Windows, `"\n"` is written as CRLF and CRLF is read as `"\n"`.
  Use `"rb"`/`"wb"` for binary files.
* All output is flushed when the program ends: when `ಮುಖ್ಯ` returns or
  `ನಿರ್ಗಮಿಸು` (exit) is called.

---

## 8. Editor support

Kasturi files are UTF-8 text with the `.kst` extension. Any editor that
supports Unicode works. For a Kannada keyboard on Windows, add the
*Kannada* keyboard (Settings → Time & language → Language). For VS Code /
Cursor, associating `*.kst` with C syntax colouring gives readable
highlighting of strings, numbers and comments:

```json
"files.associations": { "*.kst": "c" }
```
