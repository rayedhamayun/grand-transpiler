import { runCli } from "./tsDriver";

function fizzBuzzAt(i: number): string {
  if (i % 15 === 0) {
    return "FizzBuzz";
  }
  if (i % 3 === 0) {
    return "Fizz";
  }
  if (i % 5 === 0) {
    return "Buzz";
  }
  return "" + i;
}

function fizzBuzz(n: number): string[] {
  const result: string[] = [];
  let i: number = 1;
  while (i <= n) {
    result.push(fizzBuzzAt(i));
    i = i + 1;
  }
  return result;
}

runCli(fizzBuzzAt);
