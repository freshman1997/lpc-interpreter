def main():
    n = 100_000
    i = 0
    total = 0
    while i < n:
        m = {i: i + 1, i + 100: i + 2, i + 200: i + 3}
        m[i + 300] = i + 4
        total += m[i]
        total += len(m)
        i += 1
    if total <= 0:
        raise SystemExit(1)
    print(total)


main()
