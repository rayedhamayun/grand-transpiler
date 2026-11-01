using System;

public static class SubtractProductAndSum
{
    public static int Compute(int n)
    {
        int product = 1;
        int sum = 0;
        while (n > 0)
        {
            int digit = n % 10;
            product *= digit;
            sum += digit;
            n = n / 10;   // integer division
        }
        return product - sum;
    }
}