def main():
    n = 2_000_000
    i = 0
    total = 0
    while i < n:
        total += i
        i += 1
    if total <= 0:
        raise SystemExit(1)
    print(total)


main()
