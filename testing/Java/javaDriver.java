import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

public class Cli {

    static int[] parseInts(String[] args) {
        int[] nums = new int[args.length];
        for (int i = 0; i < args.length; i++) {
            if (!args[i].matches("-?\\d+")) {
                throw new IllegalArgumentException("not an integer: " + args[i]);
            }
            nums[i] = Integer.parseInt(args[i]);
        }
        return nums;
    }

    public static void runCli(Method fn, String[] args) {
        int arity = fn.getParameterCount();
        try {
            if (args.length == 0 || args.length % arity != 0) {
                throw new IllegalArgumentException(
                    "expected a multiple of " + arity + " integer argument(s), got " + args.length);
            }

            if (fn.getName().equals("numberOfSteps")) {
                int[] nums = parseInts(args);

                for (int i = 0; i < nums.length; i += arity) {
                    Object[] callArgs = new Object[arity];
                    for (int j = 0; j < arity; j++) {
                        callArgs[j] = nums[i + j];      // autoboxed to Integer
                    }
                    // null receiver because the method is static
                    System.out.println(fn.invoke(null, callArgs));
                }
            }
        } catch (InvocationTargetException e) {
            // the function itself threw
            System.err.println(e.getCause().getMessage());
            System.exitCode(1);
        } catch (Exception e) {
            System.err.println(e.getMessage());
            System.exit(1);
        }
    }
}