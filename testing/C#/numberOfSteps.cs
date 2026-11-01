using System;

public static class Steps
{
    public static int NumberOfSteps(int num)
    {
        int steps = 0;
        while (num > 0)
        {
            if (num % 2 == 0)
            {
                num = num / 2;
            }
            else
            {
                num = num - 1;
            }
            steps++;
        }
        return steps;
    }

    public static void Main(string[] args)
    {
        Cli.RunCli(typeof(Steps).GetMethod(nameof(NumberOfSteps))!, args);
    }
}