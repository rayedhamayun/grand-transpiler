const LIMIT = 3;
let counter = LIMIT * 2;
let unset = 0;
let ready = false;
let label = "count";

function tick(): void {
    counter++;
    unset += 2;
    ready = !ready;
}

export function run(): void {
    tick();
    tick();
    tick();
    console.log(label + ": " + counter);
    console.log(unset);
    if (ready) {
        label = "done";
    }
    console.log(label);
}
