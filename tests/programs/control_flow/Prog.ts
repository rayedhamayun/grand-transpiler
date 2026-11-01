export function run(): void {
    for (let i = 0; i < 12; i++) {
        if (i === 2) {
            continue;
        }
        if (i % 2 === 0 && i > 6) {
            process.stdout.write("even ");
        } else if (i % 3 === 0 || i === 1) {
            process.stdout.write("three ");
        } else {
            process.stdout.write("other ");
        }
        if (i === 10) {
            break;
        }
    }
    console.log();
    let n = 3;
    while (n > 0) {
        console.log(n);
        n--;
    }
    do {
        console.log(n);
        n++;
    } while (n < 2);
    for (let i = 0, j = 10; i < j; i += 3, j -= 2) {
        console.log(i * j);
    }
    let k = 0;
    for (;;) {
        k++;
        if (k > 3) break;
    }
    console.log(k > 3 ? 1 : 0);
    const flag = !(k === 4);
    if (!flag) console.log("flag off");
    while (true) {
        for (let i = 0; i < 3; i++) {
            if (i === 1) break;
            console.log(i);
        }
        break;
    }
}
