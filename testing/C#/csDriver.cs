using System;
using System.Reflection;
using System.Text.RegularExpressions;

public static class Cli
{
    static int[] ParseInts(string[] args)
    {
        var nums = new int[args.Length];
        for (int i = 0; i < args.Length; i++)
        {
            if (!Regex.IsMatch(args[i], @"^-?\d+$"))
            {
                throw new ArgumentException($"not an integer: {args[i]}");
            }
            nums[i] = int.Parse(args[i]);   // throws OverflowException if too big
        }
        return nums;
    }

    public static void RunCli(MethodInfo fn, string[] args)
    {
        int arity = fn.GetParameters().Length;
        try
        {
            if (args.Length == 0 || args.Length % arity != 0)
            {
                throw new ArgumentException(
                    $"expected a multiple of {arity} integer argument(s), got {args.Length}");
            }

            int[] nums = ParseInts(args);

            for (int i = 0; i < nums.Length; i += arity)
            {
                var callArgs = new object[arity];
                for (int j = 0; j < arity; j++)
                {
                    callArgs[j] = nums[i + j];      // boxed to object
                }
                // null receiver because the method is static
                object result = fn.Invoke(null, callArgs);
                // Match Java and TypeScript, which print lowercase booleans.
                Console.WriteLine(result is bool flag ? (flag ? "true" : "false") : result);
            }
        }
        catch (TargetInvocationException e)
        {
            // the function itself threw; the real exception is inside
            Console.Error.WriteLine(e.InnerException?.Message);
            Environment.ExitCode = 1;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine(e.Message);
            Environment.ExitCode = 1;   // nonzero exit, but lets stdout flush first
        }
    }
}