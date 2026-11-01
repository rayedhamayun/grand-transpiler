let total = 0;

function add(n: number): void {
    total += n;
}

add(2);
add(3);
console.log(total === 5 ? "five" : "not five");
