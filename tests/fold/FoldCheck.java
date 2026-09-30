import java.nio.file.Files;
import java.nio.file.Path;

// Prints MT2Fold's result for a decompiled function body, for the tests to read. A second argument is the function's
// name, for the rules that depend on it (a destructor, a file's setup of its globals).
public class FoldCheck {
    public static void main(String[] arguments) throws Exception {
        String name = arguments.length > 1 ? arguments[1] : "";

        System.out.println(MT2Fold.fold(Files.readString(Path.of(arguments[0])), name));
    }
}
