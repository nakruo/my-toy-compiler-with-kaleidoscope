# Kaleidoscope Toy Compiler

This is a custom implementation of the LLVM Kaleidoscope compiler, written in C++ to explore compiler design, low-level system programming, and the LLVM backend architecture.

### Features Implemented
* **Lexer & Parser**: Custom recursive descent parser building an Abstract Syntax Tree (AST).
* **Control Flow**: `if/then/else` conditionals and `for` loop iterations.
* **User-Defined Operators**: Support for custom unary and binary operators with adjustable precedence.
* **Mutable Variables**: Local variables via `var/in` expressions and the assignment operator (`=`), utilizing stack memory allocation (`alloca`).
* **AOT Compilation**: Emits native `.o` object files directly for the target machine, moving beyond interactive JIT execution.
* **DWARF Debug Information**: Full debug metadata generation (source locations, scopes, variables) for seamless integration with standard debuggers like GDB/LLDB.

### How to build and run
The compiler is currently set up to read a source file named `input.ks` from the project root and output native machine code.

1. **Build the compiler:**
```bash
mkdir build && cd build
cmake ..
make
```

2. **Write your Kaleidoscope Code:**
Open the existing `input.ks` file located in the root directory of the project (alongside `main.cpp`). You can write your own top-level logic, or simply use the pre-written example code already provided inside the file to test the compiler immediately.

3. **Compile to Object Code:** 
Run the compiler from the 'build' directory. It will read 'input.ks' and generate an 'output.o' file.

```bash
./kaleidoscope
```

4. **Link and Execute:**
Link the generated object file with a standard C++ driver to run your compiled program.

```bash
g++ ../test.cpp output.o -o test_app
./test_app
```

