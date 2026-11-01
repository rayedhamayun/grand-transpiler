using System;

public static class FibonacciNumber
{
    public static int Fib(int n)
    {
        if (n < 2)
        {
            return n;
        }
        int prev = 0;
        int curr = 1;
        int i = 2;
        while (i <= n)
        {
            int next = prev + curr;
            prev = curr;
            curr = next;
            i = i + 1;
        }
        return curr;
    }
}