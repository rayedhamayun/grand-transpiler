type IntFunction = (...args: number[]) => number | string | boolean;

function parseInts(args: string[]): number[] {
  return args.map((arg) => {
    if (!/^-?\d+$/.test(arg)) {
      throw new Error(`not an integer: ${arg}`);
    }
    return parseInt(arg, 10);
  });
}

function runCli(fn: IntFunction, arity: number = fn.length): void {
  try {
    const args: string[] = process.argv.slice(2);

    if (args.length === 0 || args.length % arity !== 0) {
      throw new Error(
        `expected a multiple of ${arity} integer argument(s), got ${args.length}`,
      );
    }

    const nums: number[] = parseInts(args);

    for (let i = 0; i < nums.length; i += arity) {
      //prints the output
      console.log(fn(...nums.slice(i, i + arity)));
    }
  } catch (err) {
    console.error((err as Error).message);
    process.exitCode = 1; // nonzero exit, but lets stdout flush first
  }
}
