let n = 2000000;
let i = 0;
let sum = 0;
while (i < n) {
  const a = i & 0xFF;
  const b = a | 0x10;
  const c = b ^ 0x55;
  const d = c << 2;
  const e = d >> 1;
  sum += e;
  i += 1;
}
if (sum <= 0) process.exit(1);
console.log(sum);
