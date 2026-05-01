let n = 2000000;
let i = 0;
let sum = 0;
while (i < n) {
  sum += i;
  i += 1;
}
if (sum <= 0) process.exit(1);
console.log(sum);
