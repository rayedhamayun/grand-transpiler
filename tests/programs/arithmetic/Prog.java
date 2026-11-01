public class Prog {
    public static void run() {
        int a = 17;
        int b = -5;
        System.out.println(a + b * 2);
        System.out.println((a + b) * 2);
        System.out.println(a / b);
        System.out.println(a % b);
        System.out.println(-a / 3);
        int c = a;
        c += 3;
        c -= 1;
        c *= 2;
        c /= 3;
        c %= 7;
        System.out.println(c);
        int d = c++;
        int e = ++c;
        System.out.println(d);
        System.out.println(e);
        System.out.println(d-- - --e);
        System.out.println(-(-a));
        System.out.println(0x10 + 2);
    }
}
