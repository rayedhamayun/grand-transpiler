public class Steps {

    public static int numberOfSteps(int num) {
        int steps = 0;
        while (num > 0) {
            if (num % 2 == 0) {
                num = num / 2;
            } else {
                num = num - 1;
            }
            steps++;
        }
        return steps;
    }

    public static void main(String[] args) throws NoSuchMethodException {
        Cli.runCli(Steps.class.getMethod("numberOfSteps", int.class), args);
    }
}