using System;

public static class HappyNumber
{
    public static int SumOfSquares(int n)
    {
        int total = 0;
        while (n > 0)
        {
            int digit = n % 10;
            total = total + digit * digit;
            n = n / 10;
        }
        return total;
    }

    public static bool IsHappy(int n)
    {
        int slow = n;
        int fast = SumOfSquares(n);
        while (fast != 1 && slow != fast)
        {
            slow = SumOfSquares(slow);
            fast = SumOfSquares(SumOfSquares(fast));
        }
        if (fast == 1)
        {
            return true;
        }
        return false;
    }

    public static void Main(string[] args)
    {
        Cli.RunCli(typeof(HappyNumber).GetMethod(nameof(IsHappy))!, args);
    }
}