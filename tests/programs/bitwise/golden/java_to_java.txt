public class Prog {
    public static void run() {
        int a = 12;
        int b = 10;
        System.out.println(a & b);
        System.out.println(a | b);
        System.out.println(a ^ b);
        System.out.println(~a);
        System.out.println(a << 3);
        System.out.println(-16 >> 2);
        System.out.println(-16 >>> 28);
        System.out.println(-16 >>> 0);
        int c = 255;
        c &= 15;
        c |= 32;
        c ^= 3;
        c <<= 2;
        c >>= 1;
        c >>>= 1;
        System.out.println(c);
        System.out.println(1 + 2 << 3 & 12);
        System.out.println((a & b) == 8 ? 1 : 0);
    }
}
