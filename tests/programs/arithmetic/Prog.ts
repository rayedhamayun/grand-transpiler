export function run(): void {
    const a = 17;
    const b = -5;
    console.log(a + b * 2);
    console.log((a + b) * 2);
    console.log(Math.trunc(a / b));
    console.log(a % b);
    console.log(Math.trunc(-a / 3));
    let c = a;
    c += 3;
    c -= 1;
    c *= 2;
    c = Math.trunc(c / 3);
    c %= 7;
    console.log(c);
    let d = c++;
    let e = ++c;
    console.log(d);
    console.log(e);
    console.log(d-- - --e);
    console.log(-(-a));
    console.log(0x10 + 2);
}
