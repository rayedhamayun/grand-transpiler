import "./tsDriver";

function fib(n: number): number {
  if (n < 2) {
    return n;
  }
  let prev: number = 0;
  let curr: number = 1;
  let i: number = 2;
  while (i <= n) {
    const next: number = prev + curr;
    prev = curr;
    curr = next;
    i = i + 1;
  }
  return curr;
}

runCli(fib);
