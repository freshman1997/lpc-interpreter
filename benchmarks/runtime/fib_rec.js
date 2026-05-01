function fib(n) {
  if (n <= 1) return n;
  return fib(n - 1) + fib(n - 2);
}

const out = fib(24);
if (out <= 0) process.exit(1);
console.log(out);
