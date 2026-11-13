#include <iostream>
static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int main() { std::cout << fib(35) << "\n"; return 0; }
