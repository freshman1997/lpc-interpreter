let n = 1000000;
let i = 0;
const pi = 3.14159265;
let sum = 0.0;
while (i < n) {
  const x = i * pi;
  sum += x / (x + 1.0);
  i += 1;
}
if (sum <= 0.0) process.exit(1);
console.log(sum);
