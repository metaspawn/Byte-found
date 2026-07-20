# Bytefound

Bytefound is a from-scratch C compiler written in C99 with a drag-and-drop
workflow: drop your `.c` files onto the executable and it compiles them.
No external toolchain required — it implements its own lexer, parser and
x86 code generator.

## Status

Early development. Currently compiles arithmetic expressions to x86 assembly.

## Build

    make

## Usage

    bytefound.exe file.c

Or drag `.c` files onto `bytefound.exe` in the file explorer.

## Roadmap

- [x] v0.1 — Lexer, parser and code generation for arithmetic expressions
- [ ] v0.2 — Functions and `return`
- [ ] v0.3 — Local variables and assignment
- [ ] v0.4 — Control flow (`if`, `while`, `for`)
- [ ] v0.5 — Function calls with arguments
- [ ] v0.6 — Pointers and arrays
- [ ] v0.7 — Structs and unions
- [ ] v0.8 — Basic preprocessor
- [ ] Built-in assembler
- [ ] Built-in linker
- [ ] Self-hosting

## License

GPL-3.0
