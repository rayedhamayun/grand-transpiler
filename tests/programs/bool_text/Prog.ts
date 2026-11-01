export function run(): void {
    const yes = 1 < 2;
    console.log(yes);
    console.log("value: " + yes);
    console.log(`interpolated ${!yes} ${1 + 1}${2}`);
}
