def main():
    n = 1_500_000
    i = 0
    total = 0
    while i < n:
        k = i & 3
        if k == 0:
            total += 3
        elif k == 1:
            total += 7
        elif k == 2:
            total += 11
        else:
            total += 13
        if (i & 15) == 0:
            total -= 1
        elif (i & 7) == 0:
            total += 2
        i += 1
    if total <= 0:
        raise SystemExit(1)
    print(total)


main()
