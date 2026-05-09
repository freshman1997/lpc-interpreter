let n = 100000;
let i = 0;
let sum = 0;
const base = "the quick brown fox jumps over the lazy dog";
while (i < n) {
  const pos = base.indexOf("fox");
  sum += pos;
  const s = base.replace("fox", "cat");
  sum += s.length;
  i += 1;
}
if (sum <= 0) process.exit(1);
console.log(sum);
