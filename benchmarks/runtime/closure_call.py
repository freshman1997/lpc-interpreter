def main():
    n = 500_000
    i = 0
    total = 0
    offset = [10]
    def fn(a):
        offset[0] += a
        return offset[0]
    while i < n:
        total += fn(i)
        i += 1
    if total <= 0:
        raise SystemExit(1)
    print(total)


main()
