let n = 500000;
let i = 0;
let sum = 0;
let offset = 10;
const fn = (a) => {
  offset += a;
  return offset;
};
while (i < n) {
  sum += fn(i);
  i += 1;
}
if (sum <= 0) process.exit(1);
console.log(sum);
