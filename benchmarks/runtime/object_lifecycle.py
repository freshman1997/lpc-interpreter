class Obj:
    pass

def main():
    n = 10_000
    i = 0
    total = 0
    while i < n:
        ob = Obj()
        total += 1
        del ob
        i += 1
    if total <= 0:
        raise SystemExit(1)
    print(total)


main()
