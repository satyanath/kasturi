# Building the Kasturi compiler

Kasturi (ಕಸ್ತೂರಿ) is a C-like programming language whose keywords are in
Kannada. The compiler (`kasturi`) translates Kasturi source (`.kst`) to
portable C and then calls a C compiler to produce a native executable.
Programs are linked with the Kasturi runtime library, a complete,
self-contained implementation of the C standard I/O library with full
UTF-8 / Kannada support.

This document explains how to build the compiler and the runtime from
source on Windows, Linux and macOS.

---

## 1. Requirements

| What | Windows | Linux / macOS |
|------|---------|---------------|
| C compiler (to build `kasturi`) | Visual Studio 2019/2022 with the *Desktop development with C++* workload, **or** MinGW-w64 gcc, **or** clang | gcc or clang |
| C compiler (used by `kasturi` at compile time) | the same | the same |
| Build tool | none (`build.bat`), or CMake ≥ 3.15 | `make`, or CMake ≥ 3.15 |

Kasturi itself is written in standard C99 and has no other dependencies.

The C compiler must be available **both** when building Kasturi and when
compiling Kasturi programs, because `kasturi` generates C code. On Windows
the compiler finds Visual Studio automatically (via `vswhere`), so you do
not have to open a "Developer Command Prompt".

---

## 2. Source layout

```
Kasturi/
├── grammar.txt            language grammar (ANTLR-style notation)
├── grammar.original.txt   the original grammar, before revision
├── build.bat              Windows build script
├── Makefile               build with make (gcc / clang)
├── CMakeLists.txt         build with CMake (any platform)
├── src/                   the compiler
│   ├── main.c             command-line driver, imports, C compiler invocation
│   ├── lexer.c/.h         tokenizer (UTF-8, Kannada keywords and digits)
│   ├── parser.c/.h        recursive-descent parser → AST
│   ├── ast.c/.h           AST and type representation
│   ├── sema.c/.h          semantic analysis, type checking, constant folding
│   ├── codegen.c/.h       C code generation
│   ├── mangle.c/.h        Kannada → ASCII identifier transliteration
│   ├── diag.c/.h          error messages (in Kannada) with source excerpts
│   └── util.c/.h          strings, vectors, maps, UTF-8 file system helpers
├── runtime/               runtime library linked into every program
│   ├── kasturi_rt.h       public C interface
│   ├── kst_internal.h     private definitions
│   ├── kst_os.c           operating-system layer (Win32 / POSIX)
│   ├── kst_stdio.c        buffered streams, file and character I/O
│   ├── kst_printf.c       formatted output engine (printf family)
│   ├── kst_scanf.c        formatted input engine (scanf family)
│   └── kst_core.c         start-up, helpers for generated code, string/math wrappers
├── lib/                   Kasturi standard library modules (Kannada names)
│   ├── ಐಒ.kst             standard I/O (stdio.h)
│   ├── ಸಾಮಾನ್ಯ.kst         general utilities (stdlib.h)
│   ├── ಪದಗಳು.kst           strings and characters (string.h, ctype.h)
│   └── ಗಣಿತ.kst            mathematics (math.h)
├── examples/              example programs
├── tests/                 test suite
└── docs/                  documentation
```

After a build the following are created:

```
bin/kasturi(.exe)          the compiler
lib/kasturi_rt.lib         runtime library (MSVC)       – or –
lib/libkasturi_rt.a        runtime library (gcc / clang)
```

---

## 3. Building on Windows

### 3.1 With `build.bat` (recommended)

Open any command prompt (cmd or PowerShell) in the `Kasturi` directory:

```bat
build.bat
```

The script picks the first available compiler:

1. `cl` if it is already on `PATH` (a Developer Command Prompt);
2. Visual Studio found through `vswhere.exe` (its `vcvars64.bat` is loaded automatically);
3. `gcc` (MinGW-w64);
4. `clang`.

You can force a compiler or run the tests:

```bat
build.bat msvc        rem Visual Studio
build.bat gcc         rem MinGW-w64
build.bat clang       rem clang
build.bat test        rem build, then run the test suite
build.bat gcc test    rem build with gcc, then test
build.bat clean       rem remove build outputs
```

The build compiles `src\*.c` into `bin\kasturi.exe`. It also compiles the runtime
into `lib\kasturi_rt.lib` (MSVC) or `lib\libkasturi_rt.a` (gcc/clang).

### 3.2 With CMake

```bat
cmake -S . -B build\cmake
cmake --build build\cmake --config Release
```

CMake writes `kasturi.exe` to `bin\` and the runtime library to `lib\`, so
the layout is the same as with `build.bat`.

### 3.3 Manually (MSVC)

From a Developer Command Prompt:

```bat
mkdir bin build\obj build\rt
cl /nologo /utf-8 /std:c11 /O2 src\*.c /Fobuild\obj\ /Febin\kasturi.exe
cl /nologo /utf-8 /std:c11 /O2 /c runtime\*.c /Fobuild\rt\
lib /nologo /OUT:lib\kasturi_rt.lib build\rt\*.obj
```

> `/utf-8` is required: the sources contain Kannada text in UTF-8.

---

## 4. Building on Linux and macOS

```sh
make            # builds bin/kasturi and lib/libkasturi_rt.a
make test       # runs the test suite
```

Choose the compiler with `make CC=clang`. CMake works as well:

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake
```

### Installing

```sh
sudo make install                 # PREFIX=/usr/local by default
make install PREFIX=$HOME/.local  # per-user install
```

This installs the compiler into `PREFIX/lib/kasturi/bin/` and links it as
`PREFIX/bin/kasturi`. The runtime, the `.kst` library modules and the runtime
sources go into `PREFIX/lib/kasturi/`.

---

## 5. How `kasturi` finds its files

`kasturi` needs two things at compile time: the library modules (`lib/*.kst`)
and the runtime (`lib/kasturi_rt.lib` / `lib/libkasturi_rt.a`, or the
sources in `runtime/`). It looks for them in the *Kasturi home* directory:

1. the environment variable `KASTURI_HOME`, if set;
2. otherwise, if the executable is in a directory named `bin`, its parent
   directory (so `Kasturi/bin/kasturi.exe` → home is `Kasturi/`);
3. otherwise the directory of the executable.

If the prebuilt runtime library for the selected C compiler is not found,
or `--rt-src` is given, `kasturi` compiles the runtime from `runtime/*.c`
together with the program. This always works, but it is slower.

To use `kasturi` from anywhere, add `Kasturi\bin` to your `PATH`:

```bat
setx PATH "%PATH%;C:\path\to\Kasturi\bin"
```

---

## 6. Running the tests

```bat
build.bat test                  rem Windows
tests\run_tests.bat             rem Windows, without rebuilding
tests\run_tests.bat -Filter strings
```

```sh
make test                       # Linux / macOS
sh tests/run_tests.sh strings   # one test
```

The suite contains two kinds of tests:

* `tests/cases/*.kst`: programs that are compiled and run. Each one's output is
  compared with `NAME.out`. Optional files supply standard input (`NAME.in`),
  arguments (`NAME.args`, one per line) and the expected exit code
  (`NAME.exit`).
* `tests/errors/*.kst`: programs that must be rejected. The first line,
  `// ಅಪೇಕ್ಷಿತ: …`, gives text that the error message must contain.

The runtime library also has a C self-test that compares the
`printf`/`scanf` engines against the host C library (more than 900 checks):

```bat
cl /nologo /utf-8 /I runtime tests\rt_selftest.c lib\kasturi_rt.lib /Febuild\rt_selftest.exe
build\rt_selftest.exe
```

```sh
cc -I runtime tests/rt_selftest.c lib/libkasturi_rt.a -lm -o build/rt_selftest && build/rt_selftest
```

---

## 7. Troubleshooting

| Problem | Solution |
|---------|----------|
| `ಯಾವುದೇ C ಸಂಕಲಕ (gcc, clang, cl) ಕಂಡುಬಂದಿಲ್ಲ` (no C compiler found) | Install Visual Studio with the C++ workload, or MinGW-w64, or set `KASTURI_CC` / `--cc` to the full path of a C compiler. |
| `ಆಮದು` (import) of `ಐಒ` fails | `kasturi` cannot find its home directory. Set `KASTURI_HOME` to the directory that contains `lib/`. |
| Linker errors about `kst_…` symbols | The prebuilt runtime is older than the compiler. Rebuild (`build.bat`), or use `--rt-src`. |
| Kannada text appears as `???` in the console | The runtime writes to the Windows console with the Unicode API, so programs display correctly. Other tools may not: use Windows Terminal, or run `chcp 65001`. |
| MinGW fails on paths with Kannada characters | `kasturi` already passes short (8.3) ASCII paths to the C compiler. If short names are disabled on the volume, build in a directory with an ASCII path. |
