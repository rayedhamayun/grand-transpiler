using System;

public class Prog
{
    const int LIMIT = 3;
    static int counter = LIMIT * 2;
    static int unset;
    static bool ready;
    static string label = "count";

    static void tick()
    {
        counter++;
        unset += 2;
        ready = !ready;
    }

    public static void run()
    {
        tick();
        tick();
        tick();
        Console.WriteLine(label + ": " + counter);
        Console.WriteLine(unset);
        if (ready)
        {
            label = "done";
        }
        Console.WriteLine(label);
    }
}
