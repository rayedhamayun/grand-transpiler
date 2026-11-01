public class Prog {
    static final int LIMIT = 3;
    static int counter = LIMIT * 2;
    static int unset;
    static boolean ready;
    static String label = "count";

    static void tick() {
        counter++;
        unset += 2;
        ready = !ready;
    }

    public static void run() {
        tick();
        tick();
        tick();
        System.out.println(label + ": " + counter);
        System.out.println(unset);
        if (ready) {
            label = "done";
        }
        System.out.println(label);
    }
}
