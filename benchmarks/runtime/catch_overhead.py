def main():
    n = 500_000
    i = 0
    total = 0
    while i < n:
        try:
            total += 1
        except Exception:
            total -= 1
        i += 1
    if total <= 0:
        raise SystemExit(1)
    print(total)


main()
