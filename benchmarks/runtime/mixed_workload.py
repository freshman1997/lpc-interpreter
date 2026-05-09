def main():
    n = 200_000
    i = 0
    score = 0
    mode = "a"
    while i < n:
        m = {i: i + 1, i + 10: i + 2}
        v = m.get(i, 0)
        score += v
        if (i & 7) == 0 and mode == "a":
            score += 1
        i += 1
    if score <= 0:
        raise SystemExit(1)
    print(score)


main()
