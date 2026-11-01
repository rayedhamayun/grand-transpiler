using System;

public class Prog
{
    public static void run()
    {
        for (int i = 0; i < 12; i++)
        {
            if (i == 2)
            {
                continue;
            }
            if (i % 2 == 0 && i > 6)
            {
                Console.Write("even ");
            }
            else if (i % 3 == 0 || i == 1)
            {
                Console.Write("three ");
            }
            else
            {
                Console.Write("other ");
            }
            if (i == 10)
            {
                break;
            }
        }
        Console.WriteLine();
        int n = 3;
        while (n > 0)
        {
            Console.WriteLine(n);
            n--;
        }
        do
        {
            Console.WriteLine(n);
            n++;
        } while (n < 2);
        for (int i = 0, j = 10; i < j; i += 3, j -= 2)
        {
            Console.WriteLine(i * j);
        }
        int k = 0;
        for (;;)
        {
            k++;
            if (k > 3) break;
        }
        Console.WriteLine(k > 3 ? 1 : 0);
        bool flag = !(k == 4);
        if (!flag) Console.WriteLine("flag off");
        while (true)
        {
            for (int i = 0; i < 3; i++)
            {
                if (i == 1) break;
                Console.WriteLine(i);
            }
            break;
        }
    }
}
