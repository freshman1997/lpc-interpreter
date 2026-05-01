def main():
    n = 100_000
    i = 0
    total = 0
    while i < n:
        arr = [i, i + 1, i + 2, i + 3]
        total += arr[0]
        i += 1
    if total <= 0:
        raise SystemExit(1)
    print(total)


main()
