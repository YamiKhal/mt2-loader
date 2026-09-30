// Folds code the compiler copied in from small engine functions and macros back into what the source wrote, working on
// the decompiled text before its locals are renamed (a local's name still says where it is on the stack):
// - if (!this->m_scene) vsFailedAssert("m_scene", "...", file, line) is vsAssert(m_scene, "..."): the assert's text is
//   the source's own condition;
// - a string formatted into a stream and handed to vsLog_ is vsLog("Scenery object '%s:%d' ...", name, variant);
// - an object copied 8 bytes at a time is one assignment: transform = *(vsTransform3D *)source;
// - vsArray's add (grow the storage when full, copy, store at the end) is models.AddItem(instance);
// - a singleton's lookup and its "No instance of %s?" assert is vsSingleton<mmoClock>::Instance();
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
    // vsFailedAssert("m_scene", "message", file, line), or vsFailedAssertF with the values its message is formatted with.
    private static final Pattern FAILED_ASSERT = Pattern.compile(
        "^\\s*vsFailedAssertF?\\(\"((?:[^\"\\\\]|\\\\.)*)\",\\s*\"((?:[^\"\\\\]|\\\\.)*)\",\\s*\"([^\"]*)\",\\s*(0x[0-9a-f]+|\\d+)((?:,\\s*.+)?)\\);$",
        Pattern.DOTALL);
    private static final Pattern FORMAT_INTO = Pattern.compile(
        "^tinyformat::format<(.*?)>\\s*\\(\\s*(?:\\([^()]*\\)\\s*)?&(\\w+),\\s*(?:\\([^()]*\\)\\s*)?(\"(?:[^\"\\\\]|\\\\.)*\")\\s*,?\\s*(.*)\\);$",
        Pattern.DOTALL);
    // vsFailedAssert("id >= 0 && id < m_arrayLength", message.text, file, line): a message made at run time.
    private static final Pattern MESSAGE_ASSERT = Pattern.compile(
        "^vsFailedAssert\\(\"((?:[^\"\\\\]|\\\\.)*)\",\\s*(?:\\([^()]*\\)\\s*)?([\\w.\\[\\]]+),\\s*\"([^\"]*)\",\\s*(0x[0-9a-f]+|\\d+)\\);$", Pattern.DOTALL);
    private static final Pattern ASSERT_OF_TEXT = Pattern.compile(
        "^vsFailedAssert\\((\"(?:[^\"\\\\]|\\\\.)*\"),\\s*(\\w+)\\.text,\\s*(\"[^\"]*\"),\\s*(0x[0-9a-f]+|\\d+)\\);$", Pattern.DOTALL);
    private static final Pattern COPIED_TEXT = Pattern.compile("^std::string::_M_assign\\(&(\\w+),.*\\);$");
    private static final Pattern STREAM_TEXT = Pattern.compile("^std::stringbuf::str\\(&(\\w+),.*\\);$");
    // A stream going away: its destructor, or the destructor copied in (vtables put back, the locale and ios_base).
    private static final Pattern STREAM_CLEANUP = Pattern.compile(
        "^(?:std::ostringstream::~ostringstream\\(.*\\);|[\\w.\\[\\]]+ = (?:\\([^()]*\\))?&(?:PTR__(?:ostringstream|stringbuf|streambuf|ios)_\\w+|DAT_\\w+);"
            + "|[\\w.\\[\\]]+(?:->|\\.)~(?:locale|ios_base)\\(\\);)$");
    private static final Pattern SINGLETON_CHECK = Pattern.compile("^if \\(vsSingleton<(.+?)>::s_instance == (?:\\([^()]*\\))?0(?:x0)?\\) \\{$");
    private static final Pattern REGISTER_FILL = Pattern.compile("^in_\\w+ = [^;]+;$");
    private static final Pattern LOG_CALL = Pattern.compile("^\\s*vsLog_\\(\"([^\"]*)\",\\s*(0x[0-9a-f]+|\\d+),\\s*(.+)\\);$", Pattern.DOTALL);
    private static final Pattern FORMAT_CALL = Pattern.compile(
        "^\\s*(?:(\\w+)(?:\\[0\\])?(?:->|\\.)format<[^>]*>\\(|tinyformat::format<[^>]*>\\(\\s*(?:\\([^()]*\\)\\s*)?&?(\\w+)\\s*,\\s*)(\".*)\\);$", Pattern.DOTALL);
    private static final Pattern FORMAT_IMPL = Pattern.compile(
        "tinyformat::detail::formatImpl\\s*\\(.*?,\\s*(\"(?:[^\"\\\\]|\\\\.)*\")\\s*,\\s*(?:\\(FormatArg \\*\\))?(&?[\\w.]+)\\s*,\\s*(\\d+)\\)", Pattern.DOTALL);
    private static final Pattern STACK_PLACE = Pattern.compile("^(?:local_([0-9a-f]+)(?:\\._(\\d+)_\\d+_|\\.field\\d+_0x([0-9a-f]+))?|stack0x([0-9a-f]+))$");
    private static final Pattern TYPEINFO = Pattern.compile("^&([\\w:<>, *]+)::typeinfo$");
    private static final Set<String> COPY_WORDS = Set.of("long", "unsigned", "int", "float", "double", "char", "short", "bool");
    // this->vtable = &PTR__mmoCostume_1417..., or through a cast: the compiler's, in every constructor and destructor.
    private static final Pattern VTABLE_WRITE = Pattern.compile("^\\s*([^=;]+?) = (?:\\([^()]*\\))?&PTR__[A-Za-z]\\w*_[0-9a-f]{6,};$");
    private static final Pattern LOCAL_MENTION = Pattern.compile("\\b(?:[a-z]{1,4}Var\\d+|local_[0-9a-f]+|[a-z]{1,3}Stack_[0-9a-f]+)\\b");
    private static final Pattern NULL_CAST = Pattern.compile("\\((?:const )?[\\w:<>, ]+?\\s*\\*+\\)0x0\\b");

    private MT2Fold() {
    }

    // A statement of the decompiled text: its lines, and the text they make together.
    record Statement(int first, int last, String text) {}

    static boolean isDeclaration(String line) {
        return DECLARATION.matcher(line).matches();
    }

    static String fold(String body) {
        return fold(body, "");
    }

    // The function's name says which rules apply: a destructor's own cleanup, a file's setup of its globals.
    static String fold(String body, String functionName) {
        List<String> lines = new ArrayList<>(List.of(foldDynamicCasts(body).split("\n", -1)));
        Map<String, String> declared = declarations(lines);

        foldFormattedAsserts(lines);
        foldStringCleanup(lines);
        foldSingletons(lines);
        foldMessageBlocks(lines);
        foldFormatStrings(lines);
        foldAsserts(lines);
        foldLogs(lines);
        foldStringCleanup(lines);
        foldCopies(lines, declared);
        foldArrayAdds(lines);
        foldLocalArrayCleanup(lines);
        foldZeroRuns(lines);
        MT2Idioms.apply(lines, functionName);
        dropVtableWrites(lines);
        dropUnusedLocals(lines);

        return NULL_CAST.matcher(String.join("\n", lines)).replaceAll("nullptr");
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

    static List<Statement> statements(List<String> lines) {
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

    static void replace(List<String> lines, int first, int last, String replacement) {
        remove(lines, first + 1, last);
        lines.set(first, replacement);
    }

    static void remove(List<String> lines, int first, int last) {
        for (int index = last; index >= first; index--) {
            lines.remove(index);
        }
    }

    static String indentOf(String line) {
        return line.substring(0, line.length() - line.stripLeading().length());
    }

    private static long number(String text) {
        return text.startsWith("0x") ? Long.parseLong(text.substring(2), 16) : Long.parseLong(text);
    }

    // if (<the condition failing>) { vsFailedAssert("m_scene", "message", file, line); } is vsAssert(m_scene, "message").
    // The condition can run over a few lines. Inside the block, a register Ghidra saw filled for the call (in_R8 =
    // "../code/...";) and a value read again after it (the call clobbered its register: the same line as before the if)
    // aren't part of it.
    private static void foldAsserts(List<String> lines) {
        for (int index = 0; index + 2 < lines.size(); index++) {
            if (!lines.get(index).trim().startsWith("if (")) {
                continue;
            }

            int opening = index;

            while (opening < lines.size() - 1 && opening - index < 4 && !lines.get(opening).trim().endsWith("{")
                && !lines.get(opening).trim().endsWith(";")) {
                opening++;
            }

            if (!lines.get(opening).trim().endsWith("{")) {
                continue;
            }

            int close = opening + 1;
            StringBuilder call = new StringBuilder();

            while (close < lines.size() && close - opening < 8 && !lines.get(close).trim().equals("}")) {
                String line = lines.get(close).trim();

                if (!REGISTER_FILL.matcher(line).matches() && !(line.contains(" = ") && appearsBefore(lines, index, line))) {
                    call.append(" ").append(line);
                }

                close++;
            }

            Matcher failed = FAILED_ASSERT.matcher(call.toString());

            if (close >= lines.size() || !failed.matches() || close + 1 < lines.size() && lines.get(close + 1).trim().startsWith("else")) {
                continue;
            }

            String condition = failed.group(1).replace("\\\"", "\"");
            String file = failed.group(3).substring(failed.group(3).lastIndexOf('/') + 1);
            String values = failed.group(5);
            String replacement = indentOf(lines.get(index)) + (values.isEmpty() ? "vsAssert(" : "vsAssertF(") + condition + ", \""
                + failed.group(2) + "\"" + values + "); // " + file + " line " + number(failed.group(4));

            replace(lines, index, close, replacement);
        }

        shortenFailedAsserts(lines);
    }

    // vsAssertF's message is formatted into a temporary string first: tinyformat::format<float>(&message, "Setting a %f
    // adjustment", &adjustment) then vsFailedAssert("adjustment >= 0.f", message.text, file, line). Joined, it's
    // vsFailedAssertF("adjustment >= 0.f", "Setting a %f adjustment", file, line, adjustment), which foldAsserts reads.
    private static void foldFormattedAsserts(List<String> lines) {
        List<Statement> all = statements(lines);

        for (int index = all.size() - 2; index >= 0; index--) {
            Matcher format = FORMAT_INTO.matcher(all.get(index).text());
            Matcher failed = format.matches() ? ASSERT_OF_TEXT.matcher(all.get(index + 1).text()) : null;

            if (failed == null || !failed.matches() || !failed.group(2).equals(format.group(2))) {
                continue;
            }

            // The template says how many values there are; Ghidra may show more (a register it thought was passed).
            int valueCount = splitArguments(format.group(1)).size();
            List<String> given = splitArguments(format.group(4));
            StringBuilder values = new StringBuilder();

            for (int value = 0; value < valueCount && value < given.size(); value++) {
                values.append(", ").append(stripCasts(given.get(value).replaceAll("\\s+", " ").trim()).replaceFirst("^&", ""));
            }

            String replacement = indentOf(lines.get(all.get(index).first())) + "vsFailedAssertF(" + failed.group(1) + ", " + format.group(3)
                + ", " + failed.group(3) + ", " + failed.group(4) + values + ");";

            replace(lines, all.get(index).first(), all.get(index + 1).last(), replacement);
        }
    }

    // An assert whose message is made at run time, copied in whole: the stream or tinyformat, the type name Demangle
    // writes, the temporary strings, their cleanup. The block only runs when the check fails and only makes and reports
    // the message, so it's the assert: vsAssertF(id >= 0 && id < m_arrayLength, "Out of bounds vsArray access: requested
    // element %d ...", index, count, "vsColor"). When a value can't be found, the message goes alone.
    private static void foldMessageBlocks(List<String> lines) {
        for (int index = lines.size() - 1; index >= 0; index--) {
            if (!lines.get(index).trim().startsWith("if (")) {
                continue;
            }

            int close = closingBraceLine(lines, index);

            if (close < 0 || !lines.get(close).trim().equals("}") || close + 1 < lines.size() && lines.get(close + 1).trim().startsWith("else")) {
                continue;
            }

            String block = String.join("\n", lines.subList(index, close + 1));

            if (!block.contains("vsFailedAssert") || block.contains("return") || block.contains("break;") || block.contains("continue;")) {
                continue;
            }

            List<Statement> all = statements(lines);
            int first = statementAt(all, index);
            int last = statementAt(all, close);
            int assertAt = last - 1;

            // What the failed assert leaves behind: its message string freed.
            while (assertAt - 3 > first && all.get(assertAt).text().equals("}") && isStringFree(all.get(assertAt - 2).text(), all.get(assertAt - 1).text())) {
                assertAt -= 3;
            }

            Matcher failed = MESSAGE_ASSERT.matcher(all.get(assertAt).text());

            if (!failed.matches() || !labelsStayInside(all, first, last) || !all.get(first).text().endsWith("{")) {
                continue;
            }

            String replacement = indentOf(lines.get(index)) + messageAssert(all, first + 1, assertAt, failed);

            replace(lines, index, close, replacement);
        }
    }

    // vsFormatString copied in: a stream made, tinyformat writing into it, its text copied out, the stream destroyed
    // (called, or copied in as its vtables put back). It's text = vsFormatString("FocusOn %d", uid). Left as it is when
    // a value can't be found, or something else happens between.
    private static void foldFormatStrings(List<String> lines) {
        for (int pass = 0; pass < 100; pass++) {
            List<Statement> all = statements(lines);
            boolean folded = false;

            for (int index = 0; index < all.size() && !folded; index++) {
                Matcher copy = STREAM_TEXT.matcher(all.get(index).text());

                if (copy.matches()) {
                    folded = foldFormatString(lines, all, index, index, copy.group(1));
                    continue;
                }

                // The stream's text copied out by hand (str() copied in): if (...) { _M_assign(&text, ...) } else
                // { _M_replace(&text, ...) }.
                Matcher assign = index + 1 < all.size() ? COPIED_TEXT.matcher(all.get(index + 1).text()) : null;
                int copyEnd = assign != null && assign.matches() && all.get(index).text().matches("if \\(\\w+ == 0\\) \\{")
                    ? endOfIfElse(all, index) - 1 : -1;

                if (copyEnd > index && contains(all, index, copyEnd + 1, "_M_replace(&" + assign.group(1) + ",")) {
                    folded = foldFormatString(lines, all, index, copyEnd, assign.group(1));
                }
            }

            if (!folded) {
                return;
            }
        }
    }

    private static boolean foldFormatString(List<String> lines, List<Statement> all, int copyAt, int copyEnd, String target) {
        int start = copyAt - 1;

        while (start >= 0 && copyAt - start < 40 && !all.get(start).text().startsWith("std::ostringstream::ostringstream(")) {
            if (!ASSIGNMENT.matcher(all.get(start).text()).matches() && !all.get(start).text().startsWith("tinyformat::detail::formatImpl")) {
                return false;
            }

            start--;
        }

        if (start < 0 || copyAt - start >= 40) {
            return false;
        }

        StringBuilder block = new StringBuilder();

        for (int index = start; index < copyAt; index++) {
            block.append(all.get(index).text()).append("\n");
        }

        Matcher impl = FORMAT_IMPL.matcher(block);

        if (!impl.find()) {
            return false;
        }

        List<String> values = inlinedValues(all, start, copyAt, impl);

        if (values == null) {
            return false;
        }

        // What's stored in between only feeds tinyformat: nothing after reads it.
        for (int index = start; index < copyAt; index++) {
            Matcher assignment = ASSIGNMENT.matcher(all.get(index).text());
            String name = assignment.matches() ? baseName(assignment.group(2)) : null;

            if (name != null && !name.equals(target) && countOutside(lines, name, all.get(start).first(), all.get(copyEnd).last()) > 0) {
                return false;
            }
        }

        int end = copyEnd;

        // A global's address put back is the stream's only when the place was the stream's.
        while (end + 1 < all.size() && STREAM_CLEANUP.matcher(all.get(end + 1).text()).matches()
            && (!all.get(end + 1).text().contains("&DAT_") || block.toString().contains(baseName(all.get(end + 1).text().split(" = ")[0])))) {
            end++;
        }

        String call = values.isEmpty() ? impl.group(1) : impl.group(1) + ", " + String.join(", ", values);

        replace(lines, all.get(start).first(), all.get(end).last(), indentOf(lines.get(all.get(start).first())) + target + " = vsFormatString(" + call + ");");

        return true;
    }

    private static int statementAt(List<Statement> statements, int line) {
        for (int index = 0; index < statements.size(); index++) {
            if (statements.get(index).last() >= line) {
                return index;
            }
        }

        return statements.size() - 1;
    }

    private static String messageAssert(List<Statement> statements, int first, int assertAt, Matcher failed) {
        String condition = failed.group(1).replace("\\\"", "\"");
        String file = failed.group(3).substring(failed.group(3).lastIndexOf('/') + 1);
        String where = "; // " + file + " line " + number(failed.group(4));
        String format = null;
        List<String> values = null;

        for (int index = first; index < assertAt && format == null; index++) {
            Matcher into = FORMAT_INTO.matcher(statements.get(index).text());

            if (into.matches()) {
                format = into.group(3);
                values = formattedValues(statements, first, assertAt, splitArguments(into.group(1)).size(), splitArguments(into.group(4)));
            }
        }

        if (format == null) {
            StringBuilder block = new StringBuilder();

            for (int index = first; index < assertAt; index++) {
                block.append(statements.get(index).text()).append("\n");
            }

            Matcher impl = FORMAT_IMPL.matcher(block);

            if (!impl.find()) {
                return "vsAssert(" + condition + ")" + where;
            }

            format = impl.group(1);
            values = inlinedValues(statements, first, assertAt, impl);
        }

        if (values == null || values.isEmpty()) {
            return "vsAssert(" + condition + ", " + format + ")" + where;
        }

        return "vsAssertF(" + condition + ", " + format + ", " + String.join(", ", values) + ")" + where;
    }

    // The values given to tinyformat::format: the template names how many, Ghidra may show one more. Null when one of
    // them is only a place Ghidra named.
    private static List<String> formattedValues(List<Statement> statements, int first, int last, int count, List<String> given) {
        List<String> values = new ArrayList<>();

        for (int value = 0; value < count && value < given.size(); value++) {
            String found = readable(statements, first, last, stripCasts(given.get(value).replaceAll("\\s+", " ").trim()).replaceFirst("^&", ""));

            if (found == null) {
                return null;
            }

            values.add(found);
        }

        return values;
    }

    private static List<String> inlinedValues(List<Statement> statements, int first, int last, Matcher impl) {
        int count = Integer.parseInt(impl.group(3));
        String given = impl.group(2);
        String assigned = given.startsWith("&") ? given : lastAssignment(statements, first, last, given);
        Long base = assigned != null && assigned.startsWith("&") ? stackOffset(assigned.substring(1)) : stackOffset(given);
        List<String> values = new ArrayList<>();

        for (int argument = 0; argument < count && base != null; argument++) {
            String pointer = assignmentAt(statements, first, last, base - 24L * argument);
            String value = pointer == null ? null : readable(statements, first, last, pointer.replaceFirst("^&", ""));

            if (value == null) {
                return null;
            }

            values.add(value);
        }

        return base == null ? null : values;
    }

    // A value as the source would name it: what was put in a place Ghidra named, a class's name Demangle wrote.
    private static String readable(List<Statement> statements, int first, int last, String value) {
        String found = GHIDRA_LOCAL.matcher(value).matches() ? valueAt(statements, first, last, value) : value;

        for (int index = first; index < last && GHIDRA_LOCAL.matcher(found).matches(); index++) {
            Matcher demangle = Pattern.compile("^Demangle\\(&" + Pattern.quote(found) + ",\\s*&?(\\w+)\\);$").matcher(statements.get(index).text());

            if (demangle.matches()) {
                return mangledName(statements, first, index, demangle.group(1));
            }
        }

        return GHIDRA_LOCAL.matcher(found).matches() || found.contains("local_") || found.contains("Stack_") ? null : found;
    }

    // "7vsColor" or "P10mmoOptions", put into the string Demangle reads, is "vsColor".
    private static String mangledName(List<Statement> statements, int first, int last, String source) {
        Pattern mangled = Pattern.compile("\"P?\\d+([A-Za-z_]\\w*)\"");

        for (int index = last - 1; index >= first; index--) {
            String text = statements.get(index).text();
            Matcher name = mangled.matcher(text);

            if (text.contains(source) && name.find()) {
                return "\"" + name.group(1) + "\"";
            }
        }

        return null;
    }

    private static boolean appearsBefore(List<String> lines, int end, String line) {
        for (int index = 0; index < end; index++) {
            if (lines.get(index).trim().equals(line)) {
                return true;
            }
        }

        return false;
    }

    // A failed assert Ghidra didn't put under its if (the condition is somewhere above, or the code only gets there when
    // it failed) keeps its call, with the source's file and line as a comment instead of the path.
    private static void shortenFailedAsserts(List<String> lines) {
        List<Statement> all = statements(lines);

        for (int index = all.size() - 1; index >= 0; index--) {
            Statement statement = all.get(index);
            Matcher failed = statement.text().startsWith("vsFailedAssert") ? FAILED_ASSERT.matcher(statement.text()) : null;

            if (failed == null || !failed.matches()) {
                continue;
            }

            String file = failed.group(3).substring(failed.group(3).lastIndexOf('/') + 1);
            String values = failed.group(5);
            String replacement = indentOf(lines.get(statement.first())) + (values.isEmpty() ? "vsFailedAssert(\"" : "vsFailedAssertF(\"")
                + failed.group(1) + "\", \"" + failed.group(2) + "\"" + values + "); // " + file + " line " + number(failed.group(4));

            replace(lines, statement.first(), statement.last(), replacement);
        }
    }

    // vsSingleton<T>::Instance() copied in: if (vsSingleton<T>::s_instance == 0) { ask vsSingletonManager, or assert
    // "No instance of %s?" in VS_Singleton.h } then the code reads s_instance. Folded, the reads are Instance().
    private static void foldSingletons(List<String> lines) {
        for (int index = 0; index < lines.size(); index++) {
            Matcher check = SINGLETON_CHECK.matcher(lines.get(index).trim());

            if (!check.matches()) {
                continue;
            }

            int close = closingBraceLine(lines, index);
            String block = close < 0 ? "" : String.join("\n", lines.subList(index, close + 1));

            // The message is made with tinyformat or a stream copied in; either way the assert names VS_Singleton.h.
            if (!block.contains("VS_Singleton.h") || !block.contains("vsFailedAssert")) {
                continue;
            }

            String instance = "vsSingleton<" + check.group(1) + ">::s_instance";
            int last = close;
            Matcher label = close + 1 < lines.size() ? Pattern.compile("^(LAB_[0-9a-f]+):$").matcher(lines.get(close + 1).trim()) : null;

            if (label != null && label.matches() && countOutside(lines, "goto " + label.group(1) + ";", index, close) == 0) {
                last = close + 1;
            }

            remove(lines, index, last);

            // The singleton manager read just before, for the lookup inside, has no other use.
            Matcher manager = index > 0 ? Pattern.compile("^(\\w+) = vsSingletonManager::s_instance;$").matcher(lines.get(index - 1).trim()) : null;

            if (manager != null && manager.matches() && countOutside(lines, manager.group(1), index - 1, index - 1) == 0) {
                lines.remove(index - 1);
                index--;
            }

            for (int after = index; after < lines.size(); after++) {
                lines.set(after, lines.get(after).replace(instance, "vsSingleton<" + check.group(1) + ">::Instance()"));
            }

            index--;
        }
    }

    // The line holding the brace that closes the one opening this line; braces inside strings don't count.
    static int closingBraceLine(List<String> lines, int open) {
        int depth = 0;

        for (int index = open; index < lines.size(); index++) {
            boolean inString = false;
            String line = lines.get(index);

            for (int at = 0; at < line.length(); at++) {
                char character = line.charAt(at);

                if (character == '"' && (at == 0 || line.charAt(at - 1) != '\\')) {
                    inString = !inString;
                } else if (!inString && character == '{') {
                    depth++;
                } else if (!inString && character == '}' && --depth == 0) {
                    return index;
                }
            }
        }

        return -1;
    }

    // How many lines outside first..last mention the word, not counting declarations.
    private static int countOutside(List<String> lines, String word, int first, int last) {
        Pattern mention = Pattern.compile("(?<![\\w])" + Pattern.quote(word) + "(?![\\w])");
        int count = 0;

        for (int index = 0; index < lines.size(); index++) {
            boolean outside = index < first || index > last;

            if (outside && !DECLARATION.matcher(lines.get(index)).matches() && mention.matcher(lines.get(index)).find()) {
                count++;
            }
        }

        return count;
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
    // if (local_48 != local_38) { operator_delete(local_48, local_38[0] + 1); }, or with std::string's names:
    // if (message.text != message.buffer) { operator_delete(message.text, ... + 1); }. A string only frees its text
    // when it's longer than its own buffer.
    private static boolean isStringFree(String check, String free) {
        if (check.matches("if \\((\\w+)(\\[0\\])? != (\\w+)\\) \\{")) {
            return free.matches("operator_delete\\(\\w+(\\[0\\])?,\\w+(\\[0\\])? \\+ 1\\);");
        }

        Matcher named = Pattern.compile("^if \\(([\\w.>\\[\\]-]+)\\.text != (?:&?\\1\\.buffer|\\w+)\\) \\{$").matcher(check);

        return named.matches() && free.startsWith("operator_delete(" + named.group(1) + ".text,") && free.endsWith("+ 1);");
    }

    private static void foldStringCleanup(List<String> lines) {
        for (int pass = 0; pass < 500; pass++) {
            List<Statement> statements = statements(lines);
            boolean removed = false;

            for (int index = 0; index < statements.size() && !removed; index++) {
                Statement statement = statements.get(index);

                if (statement.text().matches("std::string::_M_dispose\\(.*\\);")) {
                    remove(lines, statement.first(), statement.last());
                    removed = true;
                } else if (index + 2 < statements.size() && isStringFree(statement.text(), statements.get(index + 1).text())
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
    static boolean labelsStayInside(List<Statement> statements, int first, int last) {
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

    // Bytes cleared one by one (text[0] = '\0'; text[1] = '\0'; ... text[0xf] = '\0';) are one memset(text, 0, 16).
    private static void foldZeroRuns(List<String> lines) {
        Pattern zero = Pattern.compile("^(\\s*)([A-Za-z_][\\w.]*)\\[(0x[0-9a-f]+|\\d+)\\] = (?:'\\\\0'|0);$");

        for (int index = 0; index < lines.size(); index++) {
            Matcher first = zero.matcher(lines.get(index));

            if (!first.matches()) {
                continue;
            }

            long start = number(first.group(3));
            int end = index + 1;

            while (end < lines.size()) {
                Matcher next = zero.matcher(lines.get(end));

                if (!next.matches() || !next.group(2).equals(first.group(2)) || number(next.group(3)) != start + (end - index)) {
                    break;
                }

                end++;
            }

            int count = end - index;

            if (count < 4) {
                continue;
            }

            String target = start == 0 ? first.group(2) : first.group(2) + " + " + start;

            replace(lines, index, end - 1, first.group(1) + "memset(" + target + ", 0, " + count + ");");
        }
    }

    // An object's vtable set by the compiler (a constructor sets its class's, a destructor its base's on the way down):
    // the source never wrote it. Kept for a local, where it's the only sign of what the local is.
    private static void dropVtableWrites(List<String> lines) {
        for (int index = lines.size() - 1; index >= 0; index--) {
            Matcher write = VTABLE_WRITE.matcher(lines.get(index));

            if (write.matches() && !LOCAL_MENTION.matcher(write.group(1)).find()) {
                lines.remove(index);
            }
        }
    }

    // Declarations, at the top of the body, of locals that nothing uses any more.
    private static void dropUnusedLocals(List<String> lines) {
        int end = 1;

        while (end < lines.size() && !lines.get(end).trim().isEmpty()) {
            end++;
        }

        // Without locals Ghidra leaves no blank line: the body is all statements.
        if (end == lines.size()) {
            return;
        }

        for (int index = end - 1; index >= 1; index--) {
            Matcher declaration = DECLARATION.matcher(lines.get(index));

            if (!declaration.matches()) {
                continue;
            }

            // A use is the name itself, not a field that shares it (items against (this->costumePart).items), and not
            // the declaration (allocator *allocator names it twice).
            Pattern use = Pattern.compile("(?<![\\w.>])" + Pattern.quote(declaration.group(3)) + "\\b");
            boolean used = false;

            for (int other = 0; other < lines.size() && !used; other++) {
                used = other != index && use.matcher(lines.get(other)).find();
            }

            if (!used) {
                lines.remove(index);
            }
        }

        // No locals left: nor the blank line after them.
        if (lines.size() > 1 && lines.get(1).trim().isEmpty()) {
            lines.remove(1);
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
