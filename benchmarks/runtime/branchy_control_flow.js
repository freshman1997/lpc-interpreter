let n = 1500000;
let i = 0;
let sum = 0;
while (i < n) {
  switch (i & 3) {
    case 0: sum += 3; break;
    case 1: sum += 7; break;
    case 2: sum += 11; break;
    default: sum += 13; break;
  }
  if ((i & 15) === 0) {
    sum -= 1;
  } else if ((i & 7) === 0) {
    sum += 2;
  }
  i += 1;
}
if (sum <= 0) process.exit(1);
console.log(sum);
