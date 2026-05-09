def main():
    n = 1_000_000
    i = 0
    pi = 3.14159265
    total = 0.0
    while i < n:
        x = i * pi
        total += x / (x + 1.0)
        i += 1
    if total <= 0.0:
        raise SystemExit(1)
    print(total)


main()
