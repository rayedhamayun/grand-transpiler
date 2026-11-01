import { runCli } from "./tsDriver";

function subtractProductAndSum(n: number): number {
  let product: number = 1;
  let sum: number = 0;
  while (n > 0) {
    const digit: number = n % 10;
    product = product * digit;
    sum = sum + digit;
    n = (n - digit) / 10; // exact: n - (n % 10) is always divisible by 10
  }
  return product - sum;
}

runCli(subtractProductAndSum);
