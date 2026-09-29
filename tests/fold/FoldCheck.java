import java.nio.file.Files;
import java.nio.file.Path;

// Prints MT2Fold's result for a decompiled function body, for the tests to read.
public class FoldCheck {
    public static void main(String[] arguments) throws Exception {
        System.out.println(MT2Fold.fold(Files.readString(Path.of(arguments[0]))));
    }
}
