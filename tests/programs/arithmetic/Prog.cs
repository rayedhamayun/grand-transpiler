using System;

public class Prog
{
    public static void run()
    {
        int a = 17;
        int b = -5;
        Console.WriteLine(a + b * 2);
        Console.WriteLine((a + b) * 2);
        Console.WriteLine(a / b);
        Console.WriteLine(a % b);
        Console.WriteLine(-a / 3);
        int c = a;
        c += 3;
        c -= 1;
        c *= 2;
        c /= 3;
        c %= 7;
        Console.WriteLine(c);
        int d = c++;
        int e = ++c;
        Console.WriteLine(d);
        Console.WriteLine(e);
        Console.WriteLine(d-- - --e);
        Console.WriteLine(-(-a));
        Console.WriteLine(0x10 + 2);
    }
}
