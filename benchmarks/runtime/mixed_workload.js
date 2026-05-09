let n = 200000;
let i = 0;
let score = 0;
const mode = "a";
while (i < n) {
  const m = new Map([[i, i + 1], [i + 10, i + 2]]);
  const v = m.get(i) || 0;
  score += v;
  if ((i & 7) === 0 && mode === "a") {
    score += 1;
  }
  i += 1;
}
if (score <= 0) process.exit(1);
console.log(score);
