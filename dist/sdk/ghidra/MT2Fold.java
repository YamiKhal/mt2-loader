// Folds code the compiler copied in from small engine functions and macros back into what the source wrote, working on
// the decompiled text before its locals are renamed (a local's name still says where it is on the stack):
// - if (!this->m_scene) vsFailedAssert("m_scene", "...", file, line) is vsAssert(m_scene, "..."): the assert's text is
//   the source's own condition;
// - a string formatted into a stream and handed to vsLog_ is vsLog("Scenery object '%s:%d' ...", name, variant);
// - an object copied 8 bytes at a time is one assignment: transform = *(vsTransform3D *)source;
// - vsArray's add (grow the storage when full, copy, store at the end) is models.AddItem(instance);
// - a temporary std::string's destruction is left out, as the source never wrote it.
// Anything that doesn't match exactly is left as it was.

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class MT2Fold {

    private static final Pattern GHIDRA_LOCAL = Pattern.compile("^(?:[a-z]{1,4}Var\\d+|local_[0-9a-f]+|local_[A-Z0-9]+_[0-9a-f]+|[a-z]{1,3}Stack_[0-9a-f]+)$");
    private static final Pattern DECLARATION = Pattern.compile("^\\s+([\\w:<>, ]+?)\\s*(\\**)\\s*(\\w+)(\\s*\\[\\d+\\])?;$");
    // " = " with spaces only ever assigns; the target mustn't end in an operator (a += b).
    private static final Pattern ASSIGNMENT = Pattern.compile("^(\\s*)([^=;]+?[\\w\\])]) = (.+);$", Pattern.DOTALL);
    private static final Pattern IDENTIFIER = Pattern.compile("\\b[A-Za-z_]\\w*\\b");
    private static final Pattern FAILED_ASSERT = Pattern.compile(
        "^\\s*vsFailedAssert\\(\"((?:[^\"\\\\]|\\\\.)*)\",\\s*\"((?:[^\"\\\\]|\\\\.)*)\",\\s*\"([^\"]*)\",\\s*(0x[0-9a-f]+|\\d+)\\);$", Pattern.DOTALL);
    private static final Pattern LOG_CALL = Pattern.compile("^\\s*vsLog_\\(\"([^\"]*)\",\\s*(0x[0-9a-f]+|\\d+),\\s*(.+)\\);$", Pattern.DOTALL);
    private static final Pattern FORMAT_CALL = Pattern.compile(
        "^\\s*(?:(\\w+)(?:\\[0\\])?(?:->|\\.)format<[^>]*>\\(|tinyformat::format<[^>]*>\\(\\s*(?:\\([^()]*\\)\\s*)?&?(\\w+)\\s*,\\s*)(\".*)\\);$", Pattern.DOTALL);
    private static final Pattern FORMAT_IMPL = Pattern.compile(
        "tinyformat::detail::formatImpl\\s*\\(.*?,\\s*(\"(?:[^\"\\\\]|\\\\.)*\")\\s*,\\s*(?:\\(FormatArg \\*\\))?(&?[\\w.]+)\\s*,\\s*(\\d+)\\)", Pattern.DOTALL);
    private static final Pattern STACK_PLACE = Pattern.compile("^(?:local_([0-9a-f]+)(?:\\._(\\d+)_\\d+_|\\.field\\d+_0x([0-9a-f]+))?|stack0x([0-9a-f]+))$");
    private static final Pattern TYPEINFO = Pattern.compile("^&([\\w:<>, *]+)::typeinfo$");
    private static final Set<String> COPY_WORDS = Set.of("uint64_t", "uint32_t", "uint16_t", "uint8_t", "long", "unsigned", "int", "float",
        "double", "char", "short", "bool");

    private MT2Fold() {
    }

    // A statement of the decompiled text: its lines, and the text they make together.
    private record Statement(int first, int last, String text) {}

    static String fold(String body) {
        List<String> lines = new ArrayList<>(List.of(foldDynamicCasts(body).split("\n", -1)));
        Map<String, String> declared = declarations(lines);

        foldAsserts(lines);
        foldLogs(lines);
        foldStringCleanup(lines);
        foldCopies(lines, declared);
        foldArrayAdds(lines);
        foldLocalArrayCleanup(lines);
        dropUnusedLocals(lines);

        return String.join("\n", lines);
    }

    private static Map<String, String> declarations(List<String> lines) {
        Map<String, String> declared = new HashMap<>();

        for (String line : lines) {
            Matcher declaration = DECLARATION.matcher(line);

            if (declaration.matches()) {
                declared.put(declaration.group(3), declaration.group(1).trim() + declaration.group(2));
            } else if (line.trim().isEmpty() && !declared.isEmpty()) {
                break;
            }
        }

        return declared;
    }

    private static List<Statement> statements(List<String> lines) {
        List<Statement> statements = new ArrayList<>();
        int index = 0;

        while (index < lines.size()) {
            int first = index;
            StringBuilder text = new StringBuilder(lines.get(index).trim());

            while (!endsStatement(lines.get(index)) && index + 1 < lines.size() && index - first < 8) {
                index++;
                text.append(" ").append(lines.get(index).trim());
            }

            statements.add(new Statement(first, index, text.toString()));
            index++;
        }

        return statements;
    }

    private static boolean endsStatement(String line) {
        String trimmed = line.trim();

        return trimmed.isEmpty() || trimmed.endsWith(";") || trimmed.endsWith("{") || trimmed.endsWith("}") || trimmed.endsWith(":");
    }

    private static void replace(List<String> lines, int first, int last, String replacement) {
        remove(lines, first + 1, last);
        lines.set(first, replacement);
    }

    private static void remove(List<String> lines, int first, int last) {
        for (int index = last; index >= first; index--) {
            lines.remove(index);
        }
    }

    private static String indentOf(String line) {
        return line.substring(0, line.length() - line.stripLeading().length());
    }

    private static long number(String text) {
        return text.startsWith("0x") ? Long.parseLong(text.substring(2), 16) : Long.parseLong(text);
    }

    // if (<the condition failing>) { vsFailedAssert("m_scene", "message", file, line); } is vsAssert(m_scene, "message").
    private static void foldAsserts(List<String> lines) {
        for (int index = 0; index + 2 < lines.size(); index++) {
            String opening = lines.get(index).trim();

            if (!opening.startsWith("if (") || !opening.endsWith("{")) {
                continue;
            }

            int close = index + 1;
            StringBuilder call = new StringBuilder();

            while (close < lines.size() && close - index < 6 && !lines.get(close).trim().equals("}")) {
                call.append(" ").append(lines.get(close).trim());
                close++;
            }

            Matcher failed = FAILED_ASSERT.matcher(call.toString());

            if (close >= lines.size() || !failed.matches() || close + 1 < lines.size() && lines.get(close + 1).trim().startsWith("else")) {
                continue;
            }

            String condition = failed.group(1).replace("\\\"", "\"");
            String file = failed.group(3).substring(failed.group(3).lastIndexOf('/') + 1);
            String replacement = indentOf(lines.get(index)) + "vsAssert(" + condition + ", \"" + failed.group(2) + "\"); // " + file
                + " line " + number(failed.group(4));

            replace(lines, index, close, replacement);
        }
    }

    // __dynamic_cast(prop, &mmoProp::typeinfo, &mmoScenery::typeinfo, 0) is dynamic_cast<mmoScenery*>(prop).
    // The call can span lines and hold calls of its own, so it's read by its parentheses.
    private static String foldDynamicCasts(String body) {
        StringBuilder result = new StringBuilder();
        int position = 0;
        int at = body.indexOf("__dynamic_cast(");

        while (at >= 0) {
            int open = at + "__dynamic_cast".length();
            int close = closingParenthesis(body, open);
            List<String> arguments = close < 0 ? List.of() : splitArguments(body.substring(open + 1, close));
            Matcher target = arguments.size() == 4 ? TYPEINFO.matcher(arguments.get(2).trim()) : null;

            if (target == null || !target.matches()) {
                result.append(body, position, open);
                position = open;
                at = body.indexOf("__dynamic_cast(", open);
                continue;
            }

            // A cast Ghidra put in front of the call, (mmoScenery *)__dynamic_cast(...), goes too.
            int start = at;
            Matcher before = Pattern.compile("\\([\\w:<>, ]+ \\*\\)\\s*$").matcher(body.substring(position, at));

            if (before.find()) {
                start = position + before.start();
            }

            String object = stripCasts(arguments.get(0).replaceAll("\\s+", " ").trim());
            result.append(body, position, start).append("dynamic_cast<").append(target.group(1).trim()).append("*>(").append(object).append(")");
            position = close + 1;
            at = body.indexOf("__dynamic_cast(", position);
        }

        return result.append(body.substring(position)).toString();
    }

    private static int closingParenthesis(String text, int open) {
        int depth = 0;
        boolean inString = false;

        for (int index = open; index < text.length(); index++) {
            char character = text.charAt(index);

            if (character == '"' && text.charAt(index - 1) != '\\') {
                inString = !inString;
            } else if (!inString && character == '(') {
                depth++;
            } else if (!inString && character == ')' && --depth == 0) {
                return index;
            }
        }

        return -1;
    }

    // vsLog("...", a, b) as the source wrote it, from the formatting it expands to and the call to vsLog_.
    private static void foldLogs(List<String> lines) {
        for (int pass = 0; pass < 200; pass++) {
            List<Statement> statements = statements(lines);
            boolean folded = false;

            for (int index = 0; index < statements.size() && !folded; index++) {
                Matcher log = LOG_CALL.matcher(statements.get(index).text());

                if (log.matches()) {
                    folded = foldLog(lines, statements, index, log);
                }
            }

            if (!folded) {
                return;
            }
        }
    }

    private static boolean foldLog(List<String> lines, List<Statement> statements, int logIndex, Matcher log) {
        String file = log.group(1).substring(log.group(1).lastIndexOf('/') + 1);
        String where = " // " + file + " line " + number(log.group(2));
        int end = logIndex + cleanupLength(statements, logIndex + 1, log.group(3));
        String call = null;
        int start = -1;

        Statement before = logIndex > 0 ? statements.get(logIndex - 1) : null;
        Matcher format = before != null ? FORMAT_CALL.matcher(before.text()) : null;

        if (format != null && format.matches()) {
            List<String> arguments = splitArguments(format.group(3));
            List<String> values = new ArrayList<>();

            for (String argument : arguments) {
                values.add(argument.startsWith("&") ? argument.substring(1) : argument);
            }

            call = "vsLog(" + String.join(", ", values) + ");";
            start = logIndex - 1;
        } else {
            for (int index = logIndex - 1; index >= 0 && logIndex - index < 80; index--) {
                if (statements.get(index).text().startsWith("std::ostringstream::ostringstream(")) {
                    call = inlinedLog(statements, index, logIndex);
                    start = index;
                    break;
                }

                if (statements.get(index).text().endsWith("{") || statements.get(index).text().startsWith("}")) {
                    break;
                }
            }
        }

        if (call == null) {
            return false;
        }

        replace(lines, statements.get(start).first(), statements.get(end).last(), indentOf(lines.get(statements.get(start).first())) + call + where);

        return true;
    }

    // The format and the values given to tinyformat::detail::formatImpl: an array of FormatArg, 24 bytes each, whose
    // first 8 bytes point at the value. The array's name says where it starts on the stack.
    private static String inlinedLog(List<Statement> statements, int first, int last) {
        StringBuilder block = new StringBuilder();

        for (int index = first; index < last; index++) {
            block.append(statements.get(index).text()).append("\n");
        }

        Matcher impl = FORMAT_IMPL.matcher(block);

        if (!impl.find()) {
            return null;
        }

        String format = impl.group(1);
        int count = Integer.parseInt(impl.group(3));
        String given = impl.group(2);
        String assigned = given.startsWith("&") ? given : lastAssignment(statements, first, last, given);
        Long base = assigned != null && assigned.startsWith("&") ? stackOffset(assigned.substring(1)) : stackOffset(given);

        if (count == 0) {
            return "vsLog(" + format + ");";
        }

        if (base == null) {
            return "vsLog(" + format + ", /* " + count + " values */);";
        }

        List<String> values = new ArrayList<>();

        for (int argument = 0; argument < count; argument++) {
            String pointer = assignmentAt(statements, first, last, base - 24L * argument);
            String value = pointer == null ? null : pointer.startsWith("&") ? valueAt(statements, first, last, pointer.substring(1)) : pointer;

            if (value == null) {
                return "vsLog(" + format + ", /* " + count + " values */);";
            }

            values.add(value);
        }

        return "vsLog(" + format + ", " + String.join(", ", values) + ");";
    }

    // How far below the stack's top a place is: local_218 is 0x218, local_228._16_8_ and local_228.field7_0x10 are
    // 0x218 too, stack0xfffffffffffffdf0 is 0x210. Null for anything else.
    private static Long stackOffset(String place) {
        Matcher matcher = STACK_PLACE.matcher(place.trim());

        if (!matcher.matches()) {
            return null;
        }

        if (matcher.group(4) != null) {
            return -Long.parseUnsignedLong(matcher.group(4), 16);
        }

        long base = Long.parseLong(matcher.group(1), 16);

        if (matcher.group(2) != null) {
            return base - Long.parseLong(matcher.group(2));
        }

        return matcher.group(3) != null ? base - Long.parseLong(matcher.group(3), 16) : base;
    }

    private static String assignmentAt(List<Statement> statements, int first, int last, long offset) {
        String found = null;

        for (int index = first; index < last; index++) {
            Matcher assignment = ASSIGNMENT.matcher(statements.get(index).text());
            Long target = assignment.matches() ? stackOffset(assignment.group(2)) : null;

            if (target != null && target == offset) {
                found = stripCasts(assignment.group(3).trim());
            }
        }

        return found;
    }

    private static String lastAssignment(List<Statement> statements, int first, int last, String name) {
        String found = null;

        for (int index = first; index < last; index++) {
            Matcher assignment = ASSIGNMENT.matcher(statements.get(index).text());

            if (assignment.matches() && assignment.group(2).trim().equals(name)) {
                found = stripCasts(assignment.group(3).trim());
            }
        }

        return found;
    }

    // What was stored in a local whose address a FormatArg holds: local_298.x = (float)scenery->variant is the
    // variant (Ghidra typed the slot as a vector, but it's an int).
    private static String valueAt(List<Statement> statements, int first, int last, String name) {
        String value = null;

        for (int index = 0; index < last; index++) {
            Matcher assignment = ASSIGNMENT.matcher(statements.get(index).text());

            if (!assignment.matches()) {
                continue;
            }

            String target = assignment.group(2).trim();

            if (target.equals(name) || target.equals(name + ".x") || target.equals(name + "[0]")) {
                value = stripCasts(assignment.group(3).trim());
            }
        }

        return value != null ? value : name;
    }

    private static String stripCasts(String expression) {
        String result = expression;
        Matcher cast = Pattern.compile("^\\([\\w:<>, ]+\\s*\\**\\)\\s*(.+)$", Pattern.DOTALL).matcher(result);

        while (cast.matches() && !cast.group(1).startsWith(")")) {
            result = cast.group(1).trim();
            cast = Pattern.compile("^\\([\\w:<>, ]+\\s*\\**\\)\\s*(.+)$", Pattern.DOTALL).matcher(result);
        }

        return result;
    }

    // After vsLog_ the temporary string is destroyed: std::string::_M_dispose(s), or the inlined check for a string
    // that outgrew its own buffer.
    private static int cleanupLength(List<Statement> statements, int next, String loggedArgument) {
        String name = baseName(loggedArgument);

        if (next < statements.size() && statements.get(next).text().startsWith("std::string::_M_dispose(")
            && name != null && statements.get(next).text().contains(name)) {
            return 1;
        }

        if (next + 2 < statements.size() && isInlinedStringFree(statements, next, name)) {
            return 3;
        }

        return 0;
    }

    private static boolean isInlinedStringFree(List<Statement> statements, int at, String name) {
        return name != null && statements.get(at).text().matches("if \\(" + Pattern.quote(name) + "(\\[0\\])? != \\w+\\) \\{")
            && statements.get(at + 1).text().startsWith("operator_delete(") && statements.get(at + 2).text().equals("}");
    }

    private static String baseName(String expression) {
        Matcher name = Pattern.compile("(local_[0-9a-f]+|[a-z]{1,4}Var\\d+|\\w+)").matcher(stripCasts(expression).replace("&", ""));

        return name.find() ? name.group(1) : null;
    }

    // A temporary std::string destroyed on its own: the source never wrote that.
    private static void foldStringCleanup(List<String> lines) {
        for (int pass = 0; pass < 500; pass++) {
            List<Statement> statements = statements(lines);
            boolean removed = false;

            for (int index = 0; index < statements.size() && !removed; index++) {
                Statement statement = statements.get(index);

                if (statement.text().matches("std::string::_M_dispose\\(.*\\);")) {
                    remove(lines, statement.first(), statement.last());
                    removed = true;
                } else if (index + 2 < statements.size() && statement.text().matches("if \\((\\w+)(\\[0\\])? != (\\w+)\\) \\{")
                    && statements.get(index + 1).text().matches("operator_delete\\(\\w+(\\[0\\])?,\\w+(\\[0\\])? \\+ 1\\);")
                    && statements.get(index + 2).text().equals("}")) {
                    remove(lines, statement.first(), statements.get(index + 2).last());
                    removed = true;
                }
            }

            if (!removed) {
                return;
            }
        }
    }

    // An object copied piece by piece from one pointer, into one object (local_1e8[0].m_quaternion._0_8_ = *p;
    // local_1e8[0].m_quaternion._8_8_ = p[1]; ...), is one copy of the whole object.
    private static void foldCopies(List<String> lines, Map<String, String> declared) {
        for (int pass = 0; pass < 200; pass++) {
            List<Statement> statements = statements(lines);
            boolean folded = false;

            for (int index = 0; index < statements.size() && !folded; index++) {
                folded = foldCopyAt(lines, statements, index, declared);
            }

            if (!folded) {
                return;
            }
        }
    }

    private static boolean foldCopyAt(List<String> lines, List<Statement> statements, int start, Map<String, String> declared) {
        String source = null;
        String root = null;
        List<String> temporaries = new ArrayList<>();
        List<Integer> hoisted = new ArrayList<>();
        int stores = 0;
        int end = start;

        for (int index = start; index < statements.size(); index++) {
            Matcher assignment = ASSIGNMENT.matcher(statements.get(index).text());

            if (!assignment.matches()) {
                break;
            }

            String target = assignment.group(2).trim();
            String value = assignment.group(3).trim();
            List<String> names = namesIn(value);

            // A statement in between that neither reads what's copied nor writes where it goes moves before the copy
            // (matrix = matrix + index; count = index + 1; between the reads and the writes of an added item).
            if (source != null && stores + temporaries.size() > 0 && isUnrelated(statements.get(index).text(), source, temporaries, root)) {
                hoisted.add(index);
                continue;
            }

            if (source == null) {
                source = names.stream().filter(name -> GHIDRA_LOCAL.matcher(name).matches() && !temporaries.contains(name)).findFirst().orElse(null);

                if (source == null) {
                    return false;
                }
            }

            final String from = source;
            boolean onlyFromSource = !names.isEmpty() && names.stream().allMatch(name -> name.equals(from) || temporaries.contains(name)
                || COPY_WORDS.contains(name) || name.matches("SUB\\d+|CONCAT\\d+"));

            if (!onlyFromSource) {
                break;
            }

            if (GHIDRA_LOCAL.matcher(target).matches() && !target.equals(source)) {
                temporaries.add(target);
            } else {
                String targetRoot = copyRoot(target);

                if (targetRoot == null || root != null && !root.equals(targetRoot)) {
                    break;
                }

                root = targetRoot;
                stores++;
            }

            end = index;
        }

        String type = root != null ? rootType(root, declared) : null;

        if (stores < 4 || type == null || !usedOnlyWithin(statements, end + 1, temporaries)) {
            return false;
        }

        String copied = "*(" + type + " *)" + source;
        int first = start;
        Matcher sourceSet = start > 0 ? ASSIGNMENT.matcher(statements.get(start - 1).text()) : null;

        // p = (uint64_t *)group->GetPropLocalTransform(i); followed by the copy: the copy reads the call's result.
        if (sourceSet != null && sourceSet.matches() && sourceSet.group(2).trim().equals(source) && usedOnlyWithin(statements, end + 1, List.of(source))) {
            copied = "*(" + type + " *)" + stripCasts(sourceSet.group(3).trim());
            first = start - 1;
        }

        String indent = indentOf(lines.get(statements.get(first).first()));
        List<String> replacement = new ArrayList<>();

        for (int index : hoisted) {
            if (index > end) {
                break;
            }

            replacement.add(indent + statements.get(index).text());
        }

        replacement.add(indent + root + " = " + copied + ";");
        replace(lines, statements.get(first).first(), statements.get(end).last(), replacement.get(0));
        lines.addAll(statements.get(first).first() + 1, replacement.subList(1, replacement.size()));

        return true;
    }

    private static boolean isUnrelated(String statement, String source, List<String> temporaries, String root) {
        List<String> names = namesIn(statement);

        if (names.contains(source) || temporaries.stream().anyMatch(names::contains)) {
            return false;
        }

        return root == null || !root.startsWith("*") || !statement.startsWith("(" + root.substring(1) + "->")
            && !statement.startsWith(root.substring(1) + "->");
    }

    private static List<String> namesIn(String expression) {
        List<String> names = new ArrayList<>();
        Matcher identifier = IDENTIFIER.matcher(expression.replaceAll("0x[0-9a-fA-F]+", ""));

        while (identifier.find()) {
            names.add(identifier.group());
        }

        return names;
    }

    // local_1e8[0].m_quaternion._0_8_ is part of local_1e8[0]; (matrix->x).x is part of *matrix.
    private static String copyRoot(String target) {
        Matcher throughPointer = Pattern.compile("^\\(?(\\w+)->.*").matcher(target);

        if (throughPointer.matches()) {
            return "*" + throughPointer.group(1);
        }

        Matcher local = Pattern.compile("^(\\w+(?:\\[\\d+\\])?)\\..+").matcher(target);

        return local.matches() ? local.group(1) : null;
    }

    private static String rootType(String root, Map<String, String> declared) {
        String name = root.replace("*", "").replaceAll("\\[\\d+\\]$", "");
        String type = declared.get(name);

        if (type == null) {
            return null;
        }

        if (root.startsWith("*")) {
            return type.endsWith("*") ? type.substring(0, type.length() - 1).trim() : null;
        }

        return type.endsWith("*") || COPY_WORDS.contains(type) ? null : type;
    }

    // A name read after the fold must first be given a new value there.
    private static boolean usedOnlyWithin(List<Statement> statements, int from, List<String> names) {
        for (String name : names) {
            Pattern use = Pattern.compile("\\b" + Pattern.quote(name) + "\\b");

            for (int index = from; index < statements.size(); index++) {
                String text = statements.get(index).text();

                if (text.startsWith(name + " = ")) {
                    break;
                }

                if (use.matcher(text).find()) {
                    return false;
                }
            }
        }

        return true;
    }

    // vsArray<T>::AddItem inlined: count read; if full, a bigger buffer is made and the items moved; the item stored at
    // the end and the count raised. All of it is items.AddItem(item).
    private static void foldArrayAdds(List<String> lines) {
        for (int pass = 0; pass < 100; pass++) {
            List<Statement> statements = statements(lines);
            boolean folded = false;

            for (int index = 0; index < statements.size() && !folded; index++) {
                folded = foldArrayAddAt(lines, statements, index);
            }

            if (!folded) {
                return;
            }
        }
    }

    private static boolean foldArrayAddAt(List<String> lines, List<Statement> statements, int start) {
        Matcher read = Pattern.compile("^(\\w+) = (?:\\(int\\))?\\(?(.+?)\\)?\\.count;$").matcher(statements.get(start).text());

        if (!read.matches()) {
            return false;
        }

        String counter = read.group(1);
        String array = read.group(2).replaceAll("^\\(|\\)$", "");
        String countText = statements.get(start).text().substring(statements.get(start).text().indexOf(" = ") + 3).replaceAll(";$", "")
            .replace("(int)", "").trim();
        int check = -1;

        for (int index = start + 1; index < statements.size() && index <= start + 3; index++) {
            if (statements.get(index).text().startsWith("if (" + counter + " < ")) {
                check = index;
                break;
            }
        }

        if (check < 0) {
            return false;
        }

        int afterBranches = endOfIfElse(statements, check);

        if (afterBranches < 0 || !contains(statements, check, afterBranches, "operator_new__(")) {
            return false;
        }

        int raise = -1;

        for (int index = afterBranches; index < statements.size() && index <= afterBranches + 2; index++) {
            if (statements.get(index).text().startsWith(countText + " = ") && statements.get(index).text().endsWith("+ 1;")) {
                raise = index;
                break;
            }
        }

        if (raise < 0 || raise + 1 >= statements.size()) {
            return false;
        }

        Matcher store = ASSIGNMENT.matcher(statements.get(raise + 1).text());

        if (!store.matches() || !labelsStayInside(statements, start, raise + 1)) {
            return false;
        }

        String item = stripCasts(store.group(3).trim());
        replace(lines, statements.get(start).first(), statements.get(raise + 1).last(),
            indentOf(lines.get(statements.get(start).first())) + array + ".AddItem(" + item + ");");

        return true;
    }

    // The statement just after "if (...) { ... } else { ... }".
    private static int endOfIfElse(List<Statement> statements, int check) {
        int depth = 0;

        for (int index = check; index < statements.size(); index++) {
            String text = statements.get(index).text();

            depth += text.chars().filter(character -> character == '{').count();
            depth -= text.chars().filter(character -> character == '}').count();

            if (depth == 0 && index > check && !(index + 1 < statements.size() && statements.get(index + 1).text().startsWith("else"))) {
                return index + 1;
            }
        }

        return -1;
    }

    private static boolean contains(List<Statement> statements, int first, int last, String text) {
        for (int index = first; index < last; index++) {
            if (statements.get(index).text().contains(text)) {
                return true;
            }
        }

        return false;
    }

    // Labels inside a folded stretch mustn't be jumped to from outside it.
    private static boolean labelsStayInside(List<Statement> statements, int first, int last) {
        for (int index = first; index <= last; index++) {
            Matcher label = Pattern.compile("^(LAB_[0-9a-f]+):$").matcher(statements.get(index).text());

            if (!label.matches()) {
                continue;
            }

            for (int other = 0; other < statements.size(); other++) {
                if ((other < first || other > last) && statements.get(other).text().contains("goto " + label.group(1) + ";")) {
                    return false;
                }
            }
        }

        return true;
    }

    // A local vsArray going away at the end: its vtable set back to vsArray's, its buffer freed. The source never wrote
    // that.
    private static void foldLocalArrayCleanup(List<String> lines) {
        List<Statement> statements = statements(lines);

        for (int index = statements.size() - 4; index >= 0; index--) {
            // The local may already be typed as a vsArray: local_288._0_8_ = ...; if (local_288.items != (uint8_t *)0x0).
            Matcher vtable = Pattern.compile("^[\\w.]+ = (?:\\([^()]*\\))?&PTR__vsArray(?:Store)?_[0-9a-f]+;$").matcher(statements.get(index).text());

            if (vtable.matches() && statements.get(index + 1).text().matches("if \\([\\w.]+ != \\([\\w ]+\\*\\)0x0\\) \\{")
                && statements.get(index + 2).text().matches("operator_delete__\\((?:\\([^()]*\\))?[\\w.]+\\);") && statements.get(index + 3).text().equals("}")) {
                remove(lines, statements.get(index).first(), statements.get(index + 3).last());
            }
        }
    }

    // Declarations of Ghidra's locals that nothing uses any more.
    private static void dropUnusedLocals(List<String> lines) {
        String all = String.join("\n", lines);

        for (int index = lines.size() - 1; index >= 0; index--) {
            Matcher declaration = DECLARATION.matcher(lines.get(index));

            if (!declaration.matches() || !GHIDRA_LOCAL.matcher(declaration.group(3)).matches()) {
                continue;
            }

            Matcher uses = Pattern.compile("\\b" + Pattern.quote(declaration.group(3)) + "\\b").matcher(all);
            int count = 0;

            while (uses.find()) {
                count++;
            }

            if (count <= 1) {
                lines.remove(index);
            }
        }
    }

    private static List<String> splitArguments(String list) {
        List<String> parts = new ArrayList<>();
        int depth = 0;
        int start = 0;
        boolean inString = false;

        for (int index = 0; index < list.length(); index++) {
            char character = list.charAt(index);

            if (character == '"' && (index == 0 || list.charAt(index - 1) != '\\')) {
                inString = !inString;
            } else if (inString) {
                continue;
            } else if (character == '(' || character == '[') {
                depth++;
            } else if (character == ')' || character == ']') {
                depth--;
            } else if (character == ',' && depth == 0) {
                parts.add(list.substring(start, index).trim());
                start = index + 1;
            }
        }

        if (!list.trim().isEmpty()) {
            parts.add(list.substring(start).trim());
        }

        return parts;
    }
}
