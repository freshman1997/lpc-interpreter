def main():
    n = 500_000
    i = 0
    total = 0
    while i < n:
        v = {"x": 0, "y": 0}
        v["x"] = i
        v["y"] = i + 1
        total += v["x"] + v["y"]
        i += 1
    if total <= 0:
        raise SystemExit(1)
    print(total)


main()
