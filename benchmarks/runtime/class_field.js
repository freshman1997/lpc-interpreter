class Vec2 {
    constructor() {
        this.x = 0;
        this.y = 0;
    }
}

function main() {
    let n = 500000;
    let i = 0;
    let sum = 0;
    while (i < n) {
        const v = new Vec2();
        v.x = i;
        v.y = i + 1;
        sum += v.x + v.y;
        i += 1;
    }
    if (sum <= 0) process.exit(1);
    console.log(sum);
}

main();
