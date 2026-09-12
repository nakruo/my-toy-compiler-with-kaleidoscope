
# Print a character using the standard C library
extern putchard(char);

# Calculate the nth Fibonacci number (Tests recursion and if/else)
def fib(x)
  if x < 3 then
    1
  else
    fib(x-1) + fib(x-2);

# Print a row of stars (Tests for-loops)
def printstar(n)
  for i = 1, i < n, 1.0 in
    putchard(42);

# Test the compiler features
printstar(10);
fib(10);