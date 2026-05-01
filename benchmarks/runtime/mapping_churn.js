let n = 100000;
let i = 0;
let sum = 0;
while (i < n) {
  const m = new Map([[i, i + 1], [i + 100, i + 2]]);
  sum += m.get(i);
  i += 1;
}
if (sum <= 0) process.exit(1);
console.log(sum);
