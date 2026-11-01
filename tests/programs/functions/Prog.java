public class Prog {
    public static int fact(int n) {
        if (n <= 1) {
            return 1;
        }
        return n * fact(n - 1);
    }

    static boolean isEven(int n) {
        return n == 0 ? true : isOdd(n - 1);
    }

    static boolean isOdd(int n) {
        return n == 0 ? false : isEven(n - 1);
    }

    static int bump(int value) {
        value += 10;
        return value;
    }

    static void greet(String name) {
        if (name.length() == 0) {
            return;
        }
        System.out.println("hello " + name);
    }

    static int sum(int a, int b) {
        return a + b;
    }

    static int sum(int a, int b, int c) {
        return a + b + c;
    }

    public static void run() {
        System.out.println(fact(6));
        System.out.println(isEven(10) ? "even" : "odd");
        int x = 1;
        System.out.println(bump(x));
        System.out.println(x);
        greet("");
        greet("world");
        System.out.println(sum(1, 2) + sum(1, 2, 3));
        Prog.greet("qualified");
    }
}
