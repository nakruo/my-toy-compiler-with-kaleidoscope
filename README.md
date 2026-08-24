# Kaleidoscope Toy Compiler

This is a personal project I'm working on to learn C++ and compiler design using the LLVM infrastructure. I'm following the official LLVM Kaleidoscope tutorial.

### Current Status
Right now, the language doesn't do much. It only has:
* A basic Lexer
* A Recursive Descent Parser
* An AST (Abstract Syntax Tree) builder
* A simple REPL to test if the parsing works.

Code generation (LLVM IR) is not implemented yet.

### How to build and run
If you want to test the REPL:

```bash
cd build
make
./kaleidoscope
```