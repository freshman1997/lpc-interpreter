import re

def main():
    n = 50_000
    i = 0
    total = 0
    pat = re.compile(r"[a-z]+-[0-9]+")
    while i < n:
        m = pat.search("hello-123")
        total += 1 if m else 0
        s = re.sub(r"[0-9]+", "N", "abc-123-xyz")
        total += len(s)
        i += 1
    if total <= 0:
        raise SystemExit(1)
    print(total)


main()
