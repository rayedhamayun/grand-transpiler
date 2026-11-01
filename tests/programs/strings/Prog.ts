export function run(): void {
    const s = "tab\there \"quoted\" back\\slash";
    console.log(s);
    const n = 4;
    console.log(`n=${n}`);
    console.log(n + 1 + " then " + n + 1);
    console.log("" + (n + 1));
    let t = 'ab';
    t += "cd";
    t += n;
    console.log(t);
    console.log(t.length);
    console.log(t === "abcd4" ? "same" : "different");
    console.log(t !== "x" ? "same" : "different");
    console.log(String(n) + n.toString());
    console.log(("x" + t).length);
    process.stdout.write("no newline");
    process.stdout.write(String(n));
    console.log();
    console.log("line1\nline2");
    console.log("caf\u00e9 ok");
}
