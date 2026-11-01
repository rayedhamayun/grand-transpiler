using System;

public class Prog
{
    public static void run()
    {
        string s = "tab\there \"quoted\" back\\slash";
        Console.WriteLine(s);
        int n = 4;
        Console.WriteLine($"n={n}");
        Console.WriteLine(n + 1 + " then " + n + 1);
        Console.WriteLine("" + (n + 1));
        string t = "ab";
        t += "cd";
        t += n;
        Console.WriteLine(t);
        Console.WriteLine(t.Length);
        Console.WriteLine(t == "abcd4" ? "same" : "different");
        Console.WriteLine(t != "x" ? "same" : "different");
        Console.WriteLine(n.ToString() + n.ToString());
        Console.WriteLine(("x" + t).Length);
        Console.Write("no newline");
        Console.Write(n);
        Console.WriteLine();
        Console.WriteLine("line1\nline2");
        Console.WriteLine("caf\u00e9 ok");
    }
}
