# ಕಸ್ತೂರಿ (Kasturi)

**ಕನ್ನಡದಲ್ಲಿ C:** a C-based programming language whose keywords are in Kannada.

```
ಆಮದು "ಐಒ";

ಕ್ರಿಯೆ ಮುಖ್ಯ() ಫಲಿತಾಂಶ ಸಂಖ್ಯೆ {
    ಎಣಿಕೆ (ಸಂಖ್ಯೆ ಇ = 1; ಇ <= 5; ಇ++) {
        (ಇ % 2 ಸಮಾನ 0) ಆದರೆ
            ರೂಪಮುದ್ರಣ("%~d ಸಮ ಸಂಖ್ಯೆ\n", ಇ);
        ಇಲ್ಲವಾದರೆ
            ರೂಪಮುದ್ರಣ("%~d ಬೆಸ ಸಂಖ್ಯೆ\n", ಇ);
    }
    ಮರಳಿ 0;
}
```

```
೧ ಬೆಸ ಸಂಖ್ಯೆ
೨ ಸಮ ಸಂಖ್ಯೆ
೩ ಬೆಸ ಸಂಖ್ಯೆ
೪ ಸಮ ಸಂಖ್ಯೆ
೫ ಬೆಸ ಸಂಖ್ಯೆ
```

* Every keyword, type and built-in is in Kannada (`ಕ್ರಿಯೆ`, `ಆದರೆ`, `ಸಮಯವರೆಗೆ`, `ಸಂಖ್ಯೆ`, …).
  Every operator has a Kannada word form as well as its C symbol.
* Identifiers and numbers may use Kannada letters and digits (`ಅಂಕ = ೧೦೦;`).
* The language covers C: pointers, structs, arrays, variadic functions, `switch`, and more.
* It ships a complete standard I/O library implemented from scratch, with Kannada
  names, UTF-8 throughout, and extensions such as `%~d` for Kannada digits.
* The compiler translates to C and builds native executables with MSVC, gcc or clang.
* Error messages are in Kannada.

## Quick start (Windows)

```bat
build.bat
bin\kasturi -r examples\ನಮಸ್ಕಾರ.kst
```

On Linux/macOS: `make && bin/kasturi -r examples/ನಮಸ್ಕಾರ.kst`.

## Documentation

* [docs/BUILD.md](docs/BUILD.md): building the compiler
* [docs/USAGE.md](docs/USAGE.md): using the compiler
* [docs/ಭಾಷಾ_ವಿವರಣೆ.md](docs/ಭಾಷಾ_ವಿವರಣೆ.md): ಭಾಷಾ ವಿವರಣೆ (language reference, in Kannada)
* [docs/ಗ್ರಂಥಾಲಯ_ಉಲ್ಲೇಖ.md](docs/ಗ್ರಂಥಾಲಯ_ಉಲ್ಲೇಖ.md): ಗ್ರಂಥಾಲಯ ಉಲ್ಲೇಖ (library reference, in Kannada)
* [docs/INTERNALS.md](docs/INTERNALS.md): compiler internals
* [grammar.txt](grammar.txt): the formal grammar

## Examples

| File | Shows |
|------|-------|
| `examples/ನಮಸ್ಕಾರ.kst` | Hello world |
| `examples/ಗುಣಾಕಾರ_ಕೋಷ್ಟಕ.kst` | Loops, formatted output with Kannada digits |
| `examples/ಊಹೆ_ಆಟ.kst` | Number-guessing game: input, random numbers |
| `examples/ಪದ_ಎಣಿಕೆ.kst` | Counting lines, words and characters (UTF-8 aware) |
| `examples/ವಿದ್ಯಾರ್ಥಿಗಳು.kst` | Structs, arrays of structs, sorting |
| `examples/ಕಡತ_ನಕಲು.kst` | Copying files, error handling |
| `examples/ಅವಿಭಾಜ್ಯ_ಜರಡಿ.kst` | Sieve of Eratosthenes, dynamic memory, arguments |
