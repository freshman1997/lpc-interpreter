let n = 50000;
let i = 0;
let sum = 0;
const pat = /[a-z]+-[0-9]+/;
while (i < n) {
  const ok = pat.test("hello-123") ? 1 : 0;
  sum += ok;
  const s = "abc-123-xyz".replace(/[0-9]+/g, "N");
  sum += s.length;
  i += 1;
}
if (sum <= 0) process.exit(1);
console.log(sum);
