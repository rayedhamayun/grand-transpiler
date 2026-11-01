public class HappyNumber {

    public static int sumOfSquares(int n) {
        int total = 0;
        while (n > 0) {
            int digit = n % 10;
            total = total + digit * digit;
            n = n / 10;
        }
        return total;
    }

    public static boolean isHappy(int n) {
        int slow = n;
        int fast = sumOfSquares(n);
        while (fast != 1 && slow != fast) {
            slow = sumOfSquares(slow);
            fast = sumOfSquares(sumOfSquares(fast));
        }
        if (fast == 1) {
            return true;
        }
        return false;
    }

    public static void main(String[] args) throws NoSuchMethodException {
        Cli.runCli(HappyNumber.class.getMethod("isHappy", int.class), args);
    }
}