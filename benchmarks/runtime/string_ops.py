def main():
    n = 100_000
    i = 0
    total = 0
    base = "the quick brown fox jumps over the lazy dog"
    while i < n:
        pos = base.find("fox")
        total += pos
        s = base.replace("fox", "cat")
        total += len(s)
        i += 1
    if total <= 0:
        raise SystemExit(1)
    print(total)


main()
