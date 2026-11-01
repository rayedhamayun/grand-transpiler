public class Prog {
    static int total = 0;

    static void add(int n) {
        total += n;
    }

    public static void main(String[] args) {
        add(2);
        add(3);
        if (total == 5) {
            System.out.println("five");
            return;
        }
        System.out.println("not five");
    }
}
