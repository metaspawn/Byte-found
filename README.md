# Bytefound

Bytefound is a from-scratch compiler written in C99 with a drag-and-drop
workflow: drop your source files onto the executable and it compiles them.
No external toolchain required — it implements its own lexer, parser and
x86 code generator.

## Status

Early development. Currently compiles arithmetic expressions to x86 assembly.

## Build

    make

## Usage

    bytefound.exe file.bf

Or drag files onto `bytefound.exe` in the file explorer.

## Roadmap

- [x] Lexer, parser and code generation for arithmetic expressions
- [ ] AST, local variables, control flow
- [ ] Types, pointers, arrays, structs
- [ ] Built-in assembler
- [ ] Built-in linker

## License

GPL-3.0
