using System;

int total = 0;

void add(int n)
{
    total += n;
}

add(2);
add(3);
Console.WriteLine(total == 5 ? "five" : "not five");
