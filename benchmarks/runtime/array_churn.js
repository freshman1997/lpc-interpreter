let n = 100000;
let i = 0;
let sum = 0;
while (i < n) {
  const arr = [i, i + 1, i + 2, i + 3];
  sum += arr[0];
  i += 1;
}
if (sum <= 0) process.exit(1);
console.log(sum);
