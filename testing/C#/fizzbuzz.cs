using System;
using System.Collections.Generic;

public static class FizzBuzzProblem
{
    public static string FizzBuzzAt(int i)
    {
        if (i % 15 == 0)
        {
            return "FizzBuzz";
        }
        if (i % 3 == 0)
        {
            return "Fizz";
        }
        if (i % 5 == 0)
        {
            return "Buzz";
        }
        return "" + i;
    }

    public static List<string> FizzBuzz(int n)
    {
        var result = new List<string>();
        int i = 1;
        while (i <= n)
        {
            result.Add(FizzBuzzAt(i));
            i = i + 1;
        }
        return result;
    }

    public static void Main(string[] args)
    {
        Cli.RunCli(typeof(FizzBuzzProblem).GetMethod(nameof(FizzBuzzAt))!, args);
    }
}