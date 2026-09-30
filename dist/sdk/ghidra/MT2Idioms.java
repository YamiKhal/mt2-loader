// What the compiler turned a person's C++ into, turned back: string literals, ++, for loops, the cleanup a destructor
// does by itself, the setup of a file's globals. Each rule only rewrites what it recognizes whole.

import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class MT2Idioms {

    private static final String NULL = "(?:nullptr|\\([^()]*\\)0x0)";
    private static final String PLACE = "[\\w.\\[\\]>()-]+?";
    private static final Pattern STRING_FROM_LITERAL = Pattern.compile(
        "^std::string::string<std::allocator<char>>\\s*\\(\\s*&?([\\w.\\[\\]>-]+)\\s*,\\s*(?:\\([^()]*\\)\\s*)?(\"(?:[^\"\\\\]|\\\\.)*\")\\s*,\\s*[^,()]*\\);$");
    private static final Pattern STRING_POINTS_AT_BUFFER = Pattern.compile("^(" + PLACE + ")\\.text = (?:\\([^()]*\\))?&?\\1\\.buffer;$");
    private static final Pattern STEP = Pattern.compile("^(\\s*)([\\w.\\[\\]>-]+) = \\2 ([+-]) (-?)(0x[0-9a-f]+|\\d+);$");
    private static final Pattern GUARD = Pattern.compile("^if \\(0 < (" + PLACE + ")\\) \\{$");
    private static final Pattern START_AT_ZERO = Pattern.compile("^(\\w+) = 0;$");
    private static final Pattern LOOP_END = Pattern.compile("^\\} while \\((.+)\\);$");
    private static final Pattern NUMBER_CAST = Pattern.compile("\\((?:int|long long|unsigned int|unsigned long long|short|char)\\)");
    private static final Pattern MEMBER_ITEMS = Pattern.compile("^(\\w+) = (?:\\([^()]*\\))?\\(this->([\\w.]+)\\)\\.items;$");
    private static final Pattern MEMBER_TEXT = Pattern.compile("^(\\w+) = (?:\\([^()]*\\))?\\(this->([\\w.]+)\\)\\.text;$");
    // Before calls are written as methods: vsNullObject::~vsNullObject((vsNullObject *)this), or on a member.
    private static final Pattern MEMBER_DESTRUCTOR = Pattern.compile(
        "^(?:\\(&[^;]*\\)->|\\(this->[\\w.]+\\)\\.)?~\\w+\\(\\);$|^[\\w:<>, ]+::~\\w+\\s*\\(\\s*(?:\\([^()]*\\)\\s*)?&?\\(?this(?:->[^;]*)?\\);$");
    private static final Pattern DELETE_THROUGH_TABLE = Pattern.compile(
        "^\\(\\*\\*\\(code \\*\\*\\)\\(\\*\\([\\w ]+\\*\\)(\\w+\\[\\w+\\]) \\+ (?:0x[0-9a-f]+|\\d+)\\)\\)\\(\\);$");
    private static final Pattern GLOBAL_CONSTRUCTED = Pattern.compile(
        "^(\\w+(?:<.*?>)?)::(\\w+)\\s*\\(\\s*\\([^()]*\\)\\s*&([\\w:]+)\\s*,\\s*(.*)\\);$", Pattern.DOTALL);
    private static final Pattern PROPERTY_OFFSET = Pattern.compile("^_DAT_\\w+ = (0x[0-9a-f]+|\\d+);$");

    private MT2Idioms() {
    }

    static void apply(List<String> lines, String functionName) {
        foldStringLiterals(lines);
        foldStringSetup(lines);
        foldContainerDeletes(lines);

        if (functionName.contains("::~")) {
            dropMemberCleanup(lines);
        }

        if (isConstructor(functionName)) {
            dropDefaultConstruction(lines);
        }

        if (isGlobalSetup(functionName)) {
            foldGlobalSetup(lines);
        }

        foldSteps(lines);
        foldCountedLoops(lines);
        dropFinalReturn(lines);
    }

    // The compiler's function that sets up a file's globals when the game starts.
    static boolean isGlobalSetup(String functionName) {
        return functionName.contains("_GLOBAL__sub_I_") || functionName.contains("__static_initialization_and_destruction_");
    }

    // A body with no statement left: only its braces and declarations.
    static boolean isEmpty(String body) {
        for (String line : body.split("\n")) {
            String trimmed = line.trim();

            if (!trimmed.isEmpty() && !trimmed.equals("{") && !trimmed.equals("}") && !MT2Fold.isDeclaration(line)) {
                return false;
            }
        }

        return true;
    }

    // std::string::string<std::allocator<char>>(&text, "raven", allocator) is text = "raven".
    private static void foldStringLiterals(List<String> lines) {
        List<MT2Fold.Statement> all = MT2Fold.statements(lines);

        for (int index = all.size() - 1; index >= 0; index--) {
            Matcher literal = STRING_FROM_LITERAL.matcher(all.get(index).text());

            if (literal.matches()) {
                MT2Fold.replace(lines, all.get(index).first(), all.get(index).last(),
                    MT2Fold.indentOf(lines.get(all.get(index).first())) + literal.group(1) + " = " + literal.group(2) + ";");
            }
        }
    }

    // An empty std::string made in place: its text pointing at its own buffer, no length, a 0 at the start. That's
    // what "std::string text;" does, so the source never wrote it. The pointer can go through a local first.
    private static void foldStringSetup(List<String> lines) {
        List<MT2Fold.Statement> all = MT2Fold.statements(lines);

        for (int index = all.size() - 1; index >= 0; index--) {
            String text = all.get(index).text();
            Matcher direct = STRING_POINTS_AT_BUFFER.matcher(text);
            String place = direct.matches() ? direct.group(1) : null;
            int through = -1;

            if (place == null) {
                Matcher viaLocal = Pattern.compile("^(" + PLACE + ")\\.text = (\\w+);$").matcher(text);

                for (int before = index - 1; viaLocal.matches() && before >= 0 && index - before <= 4 && place == null; before--) {
                    if (all.get(before).text().equals(viaLocal.group(2) + " = " + viaLocal.group(1) + ".buffer;")) {
                        place = viaLocal.group(1);
                        through = before;
                    }
                }
            }

            if (place == null) {
                continue;
            }

            int length = find(all, index, place + ".length = 0;");
            int terminator = find(all, index, place + ".buffer[0] = '\\0';");

            if (length < 0 || terminator < 0) {
                continue;
            }

            int[] doomed = through >= 0 ? new int[] { index, length, terminator, through } : new int[] { index, length, terminator };

            java.util.Arrays.sort(doomed);

            for (int at = doomed.length - 1; at >= 0; at--) {
                MT2Fold.remove(lines, all.get(doomed[at]).first(), all.get(doomed[at]).last());
            }

            all = MT2Fold.statements(lines);
            index = Math.min(index, all.size());
        }
    }

    // A statement within a few of this one.
    private static int find(List<MT2Fold.Statement> all, int near, String text) {
        for (int index = Math.max(0, near - 8); index < Math.min(all.size(), near + 9); index++) {
            if (index != near && all.get(index).text().equals(text)) {
                return index;
            }
        }

        return -1;
    }

    // A container deleting what it holds (vsArrayStore's Clear or destructor, copied in): the call through the item's
    // vtable, then its place set to null. That call is the item's deleting destructor, whatever its slot.
    private static void foldContainerDeletes(List<String> lines) {
        List<MT2Fold.Statement> all = MT2Fold.statements(lines);

        for (int index = 0; index < all.size(); index++) {
            Matcher call = DELETE_THROUGH_TABLE.matcher(all.get(index).text());

            if (!call.matches()) {
                continue;
            }

            for (int after = index + 1; after < all.size() && after - index <= 4; after++) {
                if (all.get(after).text().matches(Pattern.quote(call.group(1)) + " = " + NULL + ";")) {
                    MT2Fold.replace(lines, all.get(index).first(), all.get(index).last(),
                        MT2Fold.indentOf(lines.get(all.get(index).first())) + "delete " + call.group(1) + ";");
                    all = MT2Fold.statements(lines);
                    break;
                }
            }
        }
    }

    // In a destructor, what the members' own destructors do: a vsArray's buffer freed, a string's text freed, a
    // member's or the base class's destructor called. The source's destructor doesn't write them.
    private static void dropMemberCleanup(List<String> lines) {
        List<MT2Fold.Statement> all = MT2Fold.statements(lines);

        for (int index = all.size() - 1; index >= 0; index--) {
            String text = all.get(index).text();
            int end = -1;

            if (MEMBER_DESTRUCTOR.matcher(text).matches()) {
                end = index;
            }

            Matcher items = MEMBER_ITEMS.matcher(text);

            if (items.matches() && index + 3 < all.size()
                && all.get(index + 1).text().matches("if \\(" + items.group(1) + " != " + NULL + "\\) \\{")
                && all.get(index + 2).text().matches("operator_delete__\\((?:\\([^()]*\\))?" + items.group(1) + "\\);") && all.get(index + 3).text().equals("}")) {
                end = index + 3;
            }

            Matcher member = MEMBER_TEXT.matcher(text);
            String buffer = member.matches() ? Pattern.quote("(this->" + member.group(2) + ").buffer") : null;

            if (buffer != null && index + 3 < all.size()
                && all.get(index + 1).text().matches("if \\(" + member.group(1) + " != " + buffer + "\\) \\{")
                && all.get(index + 2).text().startsWith("operator_delete(" + member.group(1) + ",") && all.get(index + 3).text().equals("}")) {
                end = index + 3;
            }

            if (end >= 0) {
                MT2Fold.remove(lines, all.get(index).first(), all.get(end).last());
                all = MT2Fold.statements(lines);
                index = Math.min(index, all.size());
            }
        }
    }

    // mmoCostume::mmoCostume: the name is its class's.
    private static boolean isConstructor(String functionName) {
        String[] parts = functionName.replaceAll("<.*>", "").split("::");

        return parts.length >= 2 && parts[parts.length - 1].equals(parts[parts.length - 2]);
    }

    // In a constructor, the base class's and the members' constructors called with nothing to pass
    // (vsNullObject::vsNullObject((vsNullObject *)this)): C++ calls those by itself. One given values stays; it says
    // something the source wrote.
    private static void dropDefaultConstruction(List<String> lines) {
        Pattern defaultConstructor = Pattern.compile("^([\\w:]+?)(?:<[^;]*>)?::(\\w+)\\s*\\(\\s*(?:\\([^()]*\\)\\s*)?&?\\(?this(?:->[^;,]*)?\\);$");
        List<MT2Fold.Statement> all = MT2Fold.statements(lines);

        for (int index = all.size() - 1; index >= 0; index--) {
            Matcher call = defaultConstructor.matcher(all.get(index).text());

            if (call.matches() && call.group(1).replaceAll(".*::", "").equals(call.group(2))) {
                MT2Fold.remove(lines, all.get(index).first(), all.get(index).last());
            }
        }
    }

    // A file's globals set up when the game starts. What stays is what the source wrote: each global's value, and a
    // property (the engine's list of a class's saved fields) as the global it is. The compiler's own parts go: each
    // template's run-time type info registered once, its destructor handed to atexit, the call to the next setup.
    private static void foldGlobalSetup(List<String> lines) {
        dropTypeInfoRegistration(lines);

        List<MT2Fold.Statement> all = MT2Fold.statements(lines);

        for (int index = all.size() - 1; index >= 0; index--) {
            String text = all.get(index).text();

            if (text.matches("(?:::)?atexit\\(.*\\);") || text.matches("(?:::)?__static_initialization_and_destruction_\\d+\\(\\);")) {
                MT2Fold.remove(lines, all.get(index).first(), all.get(index).last());
                continue;
            }

            Matcher constructed = GLOBAL_CONSTRUCTED.matcher(text);

            if (!constructed.matches() || !constructed.group(1).replaceAll("<.*>", "").endsWith(constructed.group(2))) {
                continue;
            }

            String indent = MT2Fold.indentOf(lines.get(all.get(index).first()));
            String value = constructed.group(3) + " = " + constructed.group(1) + "(" + constructed.group(4).replaceAll("\\s+", " ") + ");";
            Matcher offset = index + 1 < all.size() ? PROPERTY_OFFSET.matcher(all.get(index + 1).text()) : null;
            int last = index;

            // A property keeps its field's offset just after its constructor: that's which field it is.
            if (offset != null && offset.matches() && constructed.group(1).contains("Property")) {
                value += " // the field at +" + offset.group(1);
                last = index + 1;
            }

            MT2Fold.replace(lines, all.get(index).first(), all.get(last).last(), indent + value);
        }
    }

    // if (vsObject<mmoCostume,vsNullObject>::s_RTTI == '\0') { ...register it...; atexit(__tcf_4); }, or, last in the
    // function, the same written as "if (... != '\0') { return; }" and the registration after it.
    private static void dropTypeInfoRegistration(List<String> lines) {
        for (int index = lines.size() - 1; index >= 0; index--) {
            String trimmed = lines.get(index).trim();

            if (!trimmed.startsWith("if (") || !trimmed.contains("::s_RTTI ")) {
                continue;
            }

            int close = MT2Fold.closingBraceLine(lines, index);

            if (close < 0) {
                continue;
            }

            String block = String.join("\n", lines.subList(index, close + 1));

            if (trimmed.contains("::s_RTTI == ") && block.contains("vsRTTI")) {
                MT2Fold.remove(lines, index, close);
                continue;
            }

            if (!trimmed.contains("::s_RTTI != ") || !lines.get(index + 1).trim().equals("return;")) {
                continue;
            }

            int last = close;

            while (last + 1 < lines.size() && last - close < 12 && !lines.get(last).contains("vsRTTI")) {
                last++;
            }

            if (lines.get(last).contains("vsRTTI")) {
                MT2Fold.remove(lines, index, last);
            }
        }
    }

    // index = index + 1 is index++; count = count - 4 is count -= 4.
    private static void foldSteps(List<String> lines) {
        for (int index = 0; index < lines.size(); index++) {
            Matcher step = STEP.matcher(lines.get(index));

            if (!step.matches()) {
                continue;
            }

            boolean down = step.group(3).equals("-") != step.group(4).equals("-");
            String amount = step.group(5);
            String place = step.group(2);

            if (amount.equals("1") || amount.equals("0x1")) {
                lines.set(index, step.group(1) + place + (down ? "--;" : "++;"));
            } else {
                lines.set(index, step.group(1) + place + (down ? " -= " : " += ") + amount + ";");
            }
        }
    }

    // if (0 < count) { index = 0; do { ...; index++; } while (index < count); } is a for loop, and so is
    // index = 0; do { ...; index++; } while (index < 8). Only when the body doesn't continue (in a do-while that skips
    // the ++, in a for it doesn't) and the loop is counted with <.
    private static void foldCountedLoops(List<String> lines) {
        for (int index = lines.size() - 1; index >= 1; index--) {
            if (!lines.get(index).trim().equals("do {")) {
                continue;
            }

            Matcher start = START_AT_ZERO.matcher(lines.get(index - 1).trim());
            int close = MT2Fold.closingBraceLine(lines, index);
            Matcher end = close > 0 ? LOOP_END.matcher(lines.get(close).trim()) : null;

            if (!start.matches() || end == null || !end.matches()) {
                continue;
            }

            String counter = start.group(1);
            String limit = limitOf(NUMBER_CAST.matcher(end.group(1)).replaceAll(""), counter);
            String body = String.join("\n", lines.subList(index + 1, close));

            if (limit == null || !lines.get(close - 1).trim().equals(counter + "++;") || body.contains("continue;")) {
                continue;
            }

            boolean constant = limit.matches("0x[0-9a-f]+|\\d+") && !limit.equals("0") && !limit.equals("0x0");
            int guardLine = index - 2;
            Matcher guard = guardLine >= 0 ? GUARD.matcher(lines.get(guardLine).trim()) : null;
            boolean guarded = guard != null && guard.matches() && NUMBER_CAST.matcher(guard.group(1)).replaceAll("").equals(limit)
                && close + 1 < lines.size() && lines.get(close + 1).trim().equals("}")
                && MT2Fold.closingBraceLine(lines, guardLine) == close + 1
                && !(close + 2 < lines.size() && lines.get(close + 2).trim().startsWith("else"));

            // Or the guard is further up, with other statements before the counter starts: the if stays, around them.
            boolean enclosed = !guarded && isGuardedAbove(lines, index - 1, close, limit);

            if (!constant && !guarded && !enclosed) {
                continue;
            }

            String indent = MT2Fold.indentOf(lines.get(guarded ? guardLine : index - 1));
            String header = indent + "for (" + counter + " = 0; " + counter + " < " + limit + "; " + counter + "++) {";
            List<String> inner = new java.util.ArrayList<>(lines.subList(index + 1, close - 1));

            if (guarded) {
                inner.replaceAll(line -> line.startsWith("  ") ? line.substring(2) : line);
            }

            int first = guarded ? guardLine : index - 1;
            int last = guarded ? close + 1 : close;

            MT2Fold.remove(lines, first, last);
            lines.add(first, indent + "}");
            lines.addAll(first, inner);
            lines.add(first, header);
            index = first;
        }
    }

    // if (0 < limit) { a few plain statements; counter = 0; do { ... } while (...); }: the loop ends the if's block.
    private static boolean isGuardedAbove(List<String> lines, int startLine, int close, String limit) {
        String indent = MT2Fold.indentOf(lines.get(startLine));

        for (int line = startLine - 1; line >= 0 && startLine - line <= 8; line--) {
            String text = lines.get(line);

            if (MT2Fold.indentOf(text).length() == indent.length() - 2) {
                Matcher guard = GUARD.matcher(text.trim());

                return guard.matches() && NUMBER_CAST.matcher(guard.group(1)).replaceAll("").equals(limit)
                    && MT2Fold.closingBraceLine(lines, line) == close + 1
                    && !(close + 2 < lines.size() && lines.get(close + 2).trim().startsWith("else"));
            }

            // A statement changing the limit would make the for's first check differ from the guard's.
            if (!MT2Fold.indentOf(text).equals(indent) || text.contains("{") || text.contains("}") || text.trim().startsWith(limit + " = ")) {
                return false;
            }
        }

        return false;
    }

    // The limit of "counter < limit" ((int)index < count once casts are gone); null for any other condition.
    private static String limitOf(String condition, String counter) {
        Matcher less = Pattern.compile("^" + Pattern.quote(counter) + " < (.+)$").matcher(condition);

        return less.matches() ? less.group(1).trim() : null;
    }

    // "return;" as a void function's last line: the source never needs it.
    private static void dropFinalReturn(List<String> lines) {
        int last = lines.size() - 1;

        while (last >= 0 && lines.get(last).trim().isEmpty()) {
            last--;
        }

        if (last >= 1 && lines.get(last).equals("}") && lines.get(last - 1).equals("  return;")) {
            lines.remove(last - 1);
        }
    }
}
