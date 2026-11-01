import java.util.ArrayList;
import java.util.List;

public class FizzBuzzProblem {

    public static String fizzBuzzAt(int i) {
        if (i % 15 == 0) {
            return "FizzBuzz";
        }
        if (i % 3 == 0) {
            return "Fizz";
        }
        if (i % 5 == 0) {
            return "Buzz";
        }
        return "" + i;
    }

    public static List<String> fizzBuzz(int n) {
        List<String> result = new ArrayList<String>();
        int i = 1;
        while (i <= n) {
            result.add(fizzBuzzAt(i));
            i = i + 1;
        }
        return result;
    }

    public static void main(String[] args) throws NoSuchMethodException {
        Cli.runCli(FizzBuzzProblem.class.getMethod("fizzBuzzAt", int.class), args);
    }
}