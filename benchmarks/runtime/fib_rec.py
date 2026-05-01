def fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)


out = fib(24)
if out <= 0:
    raise SystemExit(1)
print(out)
