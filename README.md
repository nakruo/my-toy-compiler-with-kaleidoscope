# Kaleidoscope Toy Compiler

This is a personal project I'm working on to learn C++ and compiler design using the LLVM infrastructure. I'm following the official LLVM Kaleidoscope tutorial.

### Current Status
The compiler is currently capable of parsing code and generating optimized LLVM IR 
Features include:
* A basic Lexer and Recursive Descent Parser
* An AST (Abstract Syntax Tree) builder
* LLVM IR Code Generation (for functions, externs, and top-level expressions)
* LLVM Optimization Passes (FunctionPassManager for constant folding, reassociation, and CFG simplification)
* JIT Compilation and Execution (via ORC JIT)
* Persistent Memory and External Symbol Resolution (C++ stdlib linkage)

### How to build and run
If you want to test the REPL and see the optimized LLVM IR output:

```bash
cd build
cmake ..
make
./kaleidoscope
```