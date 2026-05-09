def main():
    n = 2_000_000
    i = 0
    total = 0
    while i < n:
        a = i & 0xFF
        b = a | 0x10
        c = b ^ 0x55
        d = c << 2
        e = d >> 1
        total += e
        i += 1
    if total <= 0:
        raise SystemExit(1)
    print(total)


main()
