let n = 500000;
let i = 0;
let sum = 0;
while (i < n) {
  try {
    sum += 1;
  } catch (e) {
    sum -= 1;
  }
  i += 1;
}
if (sum <= 0) process.exit(1);
console.log(sum);
