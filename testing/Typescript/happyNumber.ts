import { runCli } from "./tsDriver";

function sumOfSquares(n: number): number {
  let total: number = 0;
  while (n > 0) {
    const digit: number = n % 10;
    total = total + digit * digit;
    n = (n - digit) / 10;
  }
  return total;
}

function isHappy(n: number): boolean {
  let slow: number = n;
  let fast: number = sumOfSquares(n);
  while (fast !== 1 && slow !== fast) {
    slow = sumOfSquares(slow);
    fast = sumOfSquares(sumOfSquares(fast));
  }
  if (fast === 1) {
    return true;
  }
  return false;
}

runCli(isHappy);
