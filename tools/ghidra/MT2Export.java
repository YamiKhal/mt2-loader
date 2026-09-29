// Writes MT2.exe's code as C++ source files laid out like the game's own source tree (Games/MMORPG/.../MMO_Quest.cpp),
// one .cpp per source file the exe names, with a .h beside it declaring its classes. Functions keep the order the
// exe has them in, which follows the original files. Uses what MT2Apply taught Ghidra (fields, enums, mappings), the
// exact C++ parameter types from the exe's mangled names, and names locals after what they hold.
// Arguments: the output folder, program.json (mt2sdk program), mappings.json or "-", "game" (the game's and engine's
// own code) or "all", and optionally a text a file's path must have (MMO_Quest) to export only those files.
// @category MT2

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.parallel.DecompileConfigurer;
import ghidra.app.decompiler.parallel.DecompilerCallback;
import ghidra.app.decompiler.parallel.ParallelDecompiler;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.demangler.DemangledObject;
import ghidra.app.util.demangler.DemanglerUtil;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.Symbol;
import ghidra.util.task.TaskMonitor;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class MT2Export extends GhidraScript {

    private static final int TIMEOUT_SECONDS = 60;
    private static final String TEMPLATES = "_templates/";

    private static final Pattern GHIDRA_LOCAL = Pattern.compile(
        "^(?:[a-z]{1,4}Var\\d+|this_\\d+|local_[0-9a-f]+|local_[A-Z0-9]+_[0-9a-f]+|[a-z]{1,3}Stack_[0-9a-f]+|in_\\w+|extraout_\\w+|unaff_\\w+|param_\\d+)$");
    private static final Pattern DECLARATION = Pattern.compile("^\\s+([\\w:<>, ]+?)\\s*(\\**)\\s*(\\w+)(\\s*\\[\\d+\\])?;$");
    private static final Set<String> KEYWORDS = Set.of("new", "delete", "class", "this", "default", "switch", "case", "int", "float",
        "bool", "char", "double", "long", "short", "signed", "unsigned", "void", "const", "static", "return", "if", "else",
        "for", "while", "do", "break", "continue", "goto", "struct", "union", "enum", "template", "typename", "operator",
        "private", "public", "protected", "virtual", "friend", "namespace", "using", "true", "false", "sizeof", "auto");
    private static final String[][] TYPE_WORDS = {
        { "ulonglong", "unsigned long long" }, { "longlong", "long long" }, { "uint", "unsigned int" },
        { "ushort", "unsigned short" }, { "uchar", "unsigned char" }, { "undefined8", "uint64_t" },
        { "undefined4", "uint32_t" }, { "undefined2", "uint16_t" }, { "undefined1", "uint8_t" }, { "undefined", "uint8_t" },
        { "byte", "uint8_t" }, { "sbyte", "int8_t" }, { "qword", "uint64_t" }, { "dword", "uint32_t" }, { "word", "uint16_t" },
    };

    private record Decompiled(Function function, String code) {}

    private static class SourceFile {
        String name;
        String path;
        boolean guessed;
    }

    private static class ClassInfo {
        String name;
        int size = -1;
        List<String> bases = new ArrayList<>();
        List<long[]> vtable = new ArrayList<>();
        TreeMap<Integer, String[]> fields = new TreeMap<>();
    }

    private static class Output {
        String path;
        boolean guessed;
        List<Decompiled> functions = new ArrayList<>();
        Set<String> classes = new HashSet<>();
    }

    private final List<SourceFile> files = new ArrayList<>();
    private final Map<Long, Integer> fileOfFunction = new HashMap<>();
    private final Map<String, ClassInfo> classes = new HashMap<>();
    private final Map<String, List<String>> enums = new LinkedHashMap<>();
    private final Set<String> methods = new HashSet<>();
    // Functions returning a class by value (MT2Apply gave them "result" first), and whether they're methods ("self").
    private final Map<String, Boolean> byValue = new HashMap<>();
    private final Map<Function, String> signatures = new HashMap<>();
    private final Map<Function, String> returnTypes = new HashMap<>();
    private String build = "";
    // Only the files whose path has this in it (for trying the export on a few files), or null for all.
    private String onlyPathsWith;

    @Override
    public void run() throws Exception {
        String[] arguments = getScriptArgs();
        File output = arguments.length > 0 ? new File(arguments[0]) : askDirectory("Folder for the source files", "Export");
        File programFile = arguments.length > 1 ? new File(arguments[1]) : askFile("program.json from mt2sdk program", "Use");
        File mappingsFile = arguments.length > 2 && !arguments[2].equals("-") ? new File(arguments[2]) : null;
        boolean everything = arguments.length > 3 && arguments[3].equals("all");
        onlyPathsWith = arguments.length > 4 ? arguments[4] : null;

        readProgram(readJson(programFile));

        if (mappingsFile != null) {
            readMappedFields(readJson(mappingsFile));
        }

        List<Function> chosen = choose(everything);
        println("Decompiling " + chosen.size() + " functions into " + output);

        List<Decompiled> results = decompile(chosen);
        Map<String, Output> outputs = arrange(results);

        write(output.toPath(), outputs);
    }

    private JsonObject readJson(File file) throws Exception {
        return JsonParser.parseString(Files.readString(file.toPath(), StandardCharsets.UTF_8)).getAsJsonObject();
    }

    private void readProgram(JsonObject program) {
        build = program.get("build").getAsString();

        for (JsonElement element : program.getAsJsonArray("files")) {
            JsonObject item = element.getAsJsonObject();
            SourceFile file = new SourceFile();
            file.name = item.get("name").getAsString();
            file.path = item.get("path").getAsString();
            file.guessed = item.get("pathGuessed").getAsBoolean();
            files.add(file);
        }

        for (JsonElement element : program.getAsJsonArray("functions")) {
            JsonArray pair = element.getAsJsonArray();
            fileOfFunction.put(pair.get(0).getAsLong(), pair.get(1).getAsInt());
        }

        for (JsonElement element : program.getAsJsonArray("classes")) {
            JsonObject item = element.getAsJsonObject();
            ClassInfo info = new ClassInfo();
            info.name = item.get("name").getAsString();
            info.size = item.has("size") ? item.get("size").getAsInt() : -1;

            for (JsonElement base : item.getAsJsonArray("bases")) {
                info.bases.add(base.getAsJsonObject().get("name").getAsString());
            }

            for (JsonElement entry : item.getAsJsonArray("vtable")) {
                JsonArray values = entry.getAsJsonArray();
                info.vtable.add(new long[] { values.get(0).getAsLong(), values.get(1).getAsLong(), values.get(2).getAsLong() });
            }

            for (JsonElement fieldElement : item.getAsJsonArray("fields")) {
                JsonObject field = fieldElement.getAsJsonObject();
                info.fields.put(field.get("offset").getAsInt(), new String[] { field.get("type").getAsString(), field.get("name").getAsString(), "" });
            }

            classes.put(info.name, info);
        }

        for (JsonElement element : program.getAsJsonArray("enums")) {
            JsonObject item = element.getAsJsonObject();
            List<String> values = new ArrayList<>();

            for (JsonElement value : item.getAsJsonArray("values")) {
                values.add(value.getAsString());
            }

            enums.put(item.get("name").getAsString(), values);
        }
    }

    private void readMappedFields(JsonObject mappings) {
        for (JsonElement element : mappings.getAsJsonArray("classes")) {
            JsonObject item = element.getAsJsonObject();
            ClassInfo info = classes.computeIfAbsent(item.get("name").getAsString(), name -> {
                ClassInfo created = new ClassInfo();
                created.name = name;
                return created;
            });

            if (item.has("size") && !item.has("sizeFrom")) {
                info.size = item.get("size").getAsInt();
            }

            for (JsonElement fieldElement : item.getAsJsonArray("fields")) {
                JsonObject field = fieldElement.getAsJsonObject();
                StringBuilder doc = new StringBuilder();

                for (JsonElement line : field.getAsJsonArray("doc")) {
                    doc.append(doc.length() > 0 ? " " : "").append(line.getAsString());
                }

                info.fields.put(field.get("offset").getAsInt(), new String[] { field.get("type").getAsString(), field.get("name").getAsString(),
                    doc.length() > 0 ? doc.toString() : "(mt2-mappings)" });
            }
        }
    }

    // The game's and engine's own code has a source path from an assert (or its neighbors); the C++ library, the C
    // runtime and the other libraries linked in don't.
    private List<Function> choose(boolean everything) {
        List<Function> chosen = new ArrayList<>();

        for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
            if (function.isThunk() || function.isExternal()) {
                continue;
            }

            SourceFile file = fileOf(function);
            boolean gamesOwn = file != null && !file.path.isEmpty();
            boolean wanted = onlyPathsWith == null || (file != null && (file.path + file.name).contains(onlyPathsWith));

            if ((everything || gamesOwn) && wanted) {
                chosen.add(function);
            }

            if ("__thiscall".equals(function.getCallingConventionName())) {
                methods.add(function.getName(true));
            }

            Parameter[] parameters = function.getParameters();

            if (parameters.length > 0 && parameters[0].getName().equals("result")) {
                byValue.put(function.getName(true), parameters.length > 1 && parameters[1].getName().equals("self"));
            }
        }

        return chosen;
    }

    private SourceFile fileOf(Function function) {
        Integer index = fileOfFunction.get(function.getEntryPoint().getOffset());

        return index == null ? null : files.get(index);
    }

    private List<Decompiled> decompile(List<Function> functions) throws Exception {
        DecompilerCallback<Decompiled> callback = new DecompilerCallback<>(currentProgram, new Configurer()) {
            @Override
            public Decompiled process(DecompileResults results, TaskMonitor monitor) {
                String code = results.decompileCompleted() ? results.getDecompiledFunction().getC()
                    : "{\n  // Ghidra couldn't decompile this function: " + results.getErrorMessage() + "\n}\n";

                return new Decompiled(results.getFunction(), code);
            }
        };
        callback.setTimeout(TIMEOUT_SECONDS);

        try {
            return ParallelDecompiler.decompileFunctions(callback, functions, monitor);
        } finally {
            callback.dispose();
        }
    }

    // Each function goes to its source file; code the compiler made from a template (vsProperty<int, mmoNPC>::Type)
    // goes to _templates/vsProperty.cpp, beside the other copies of that template.
    private Map<String, Output> arrange(List<Decompiled> results) {
        Map<String, Output> outputs = new TreeMap<>();
        Map<String, Map<String, Integer>> classVotes = new HashMap<>();

        for (Decompiled result : results) {
            Function function = result.function();
            SourceFile file = fileOf(function);
            String template = templateOf(function);
            String path = template != null ? TEMPLATES + safe(template) + ".cpp"
                : file != null && !file.path.isEmpty() ? file.path : "_other/" + (file != null ? safe(file.name) : "unknown.cpp");
            Output out = outputs.computeIfAbsent(path, key -> {
                Output created = new Output();
                created.path = key;
                created.guessed = template == null && file != null && file.guessed;
                return created;
            });

            out.functions.add(result);

            String owner = ownerClass(function);

            if (owner != null && template == null) {
                classVotes.computeIfAbsent(owner, key -> new HashMap<>()).merge(path, 1, Integer::sum);
            }
        }

        // A class is declared in the header beside the .cpp that has most of its functions.
        for (Map.Entry<String, Map<String, Integer>> vote : classVotes.entrySet()) {
            String best = vote.getValue().entrySet().stream().max(Map.Entry.comparingByValue()).get().getKey();
            outputs.get(best).classes.add(vote.getKey());
        }

        return outputs;
    }

    private String ownerClass(Function function) {
        Namespace namespace = function.getParentNamespace();

        return namespace == null || namespace.isGlobal() ? null : namespace.getName(true);
    }

    private String templateOf(Function function) {
        String full = function.getName(true);
        int angle = full.indexOf('<');

        if (angle < 0) {
            return null;
        }

        String before = full.substring(0, angle);
        int last = before.lastIndexOf("::");

        return last >= 0 ? before.substring(last + 2) : before;
    }

    private void write(Path root, Map<String, Output> outputs) throws Exception {
        StringBuilder index = new StringBuilder();
        int functionCount = 0;

        for (Output out : outputs.values()) {
            out.functions.sort(Comparator.comparing(result -> result.function().getEntryPoint()));

            String cpp = cppFile(out, index);
            Path path = root.resolve(out.path);
            Files.createDirectories(path.getParent());
            Files.writeString(path, cpp, StandardCharsets.UTF_8);
            functionCount += out.functions.size();

            if (!out.classes.isEmpty()) {
                Files.writeString(root.resolve(headerPath(out.path)), headerFile(out), StandardCharsets.UTF_8);
            }
        }

        Files.writeString(root.resolve("Enums.h"), enumsFile(), StandardCharsets.UTF_8);
        Files.writeString(root.resolve("index.txt"), index.toString(), StandardCharsets.UTF_8);
        Files.writeString(root.resolve("README.md"), readme(outputs.size(), functionCount), StandardCharsets.UTF_8);
        println("Wrote " + functionCount + " functions in " + outputs.size() + " files, with index.txt and README.md");
    }

    private String headerPath(String cppPath) {
        return cppPath.replaceAll("\\.(cpp|c|cc)$", "") + ".h";
    }

    private String cppFile(Output out, StringBuilder index) {
        StringBuilder text = new StringBuilder();
        String fileName = out.path.substring(out.path.lastIndexOf('/') + 1);
        Map<String, String> seenBodies = new HashMap<>();

        text.append("// ").append(fileName).append(", rebuilt from MT2.exe ").append(build).append(" by mt2sdk decompile.\n");
        text.append("// Not the game's own source: its machine code turned back into C++, with the names the exe keeps and mt2-mappings.\n");

        if (out.path.startsWith(TEMPLATES)) {
            text.append("// The compiler's copies of one template, one per type it was used with, gathered from every source file.\n");
        } else if (out.guessed) {
            text.append("// The folder is a guess: no assert in this file names it, so it's the folder of the files built next to it.\n");
        }

        if (!out.classes.isEmpty()) {
            String header = headerPath(fileName);
            text.append("\n#include \"").append(header).append("\"\n");
        }

        for (Decompiled result : out.functions) {
            Function function = result.function();
            String body = cppFunction(function, result.code());
            String variant = variantNote(function);
            String withoutAddress = body.replaceAll("LAB_[0-9a-f]+", "LAB");
            String earlier = seenBodies.putIfAbsent(withoutAddress, function.getEntryPoint().toString());

            index.append("0x").append(function.getEntryPoint()).append("  ").append(function.getName(true)).append("  ")
                .append(out.path).append("\n");

            text.append("\n// 0x").append(function.getEntryPoint());

            if (variant != null) {
                text.append(" (").append(variant).append(")");
            }

            if (earlier != null) {
                text.append(": the same code as 0x").append(earlier).append(" above\n");
                continue;
            }

            text.append("\n").append(body);
        }

        return text.toString();
    }

    // GCC makes several copies of constructors and destructors, and splits some functions in two; the mangled name
    // says which copy this is.
    private String variantNote(Function function) {
        for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(function.getEntryPoint())) {
            String name = symbol.getName();

            if (!name.startsWith("_Z")) {
                continue;
            }

            if (name.contains(".part.")) {
                return "split out of the function of the same name by the compiler";
            }

            if (name.contains(".isra.") || name.contains(".constprop.")) {
                return "a copy the compiler specialized";
            }

            if (name.contains(".cold")) {
                return "the rarely used end of the function of the same name";
            }

            if (name.matches(".*D0E.*")) {
                return "deleting destructor: destroys, then frees the memory";
            }

            if (name.matches(".*D2E.*")) {
                return "base destructor: used by derived classes' destructors";
            }

            if (name.matches(".*C2E.*")) {
                return "base constructor: used by derived classes' constructors";
            }
        }

        return null;
    }

    private String cppFunction(Function function, String decompiled) {
        // Ghidra ends lines the Windows way; everything below reads lines ending in \n.
        String code = decompiled.replace("\r\n", "\n");
        int open = code.indexOf("\n{");

        if (open < 0) {
            return code;
        }

        String prelude = code.substring(0, open);
        String body = code.substring(open + 1);
        Map<String, String> renames = new LinkedHashMap<>();

        rememberReturnType(function, prelude);

        String signature = cppSignature(function, renames);
        String comment = plateComment(prelude);

        body = types(body);
        body = foldedOrAsIs(body);
        body = operators(body);
        // In a function returning a class by value, the object is the parameter after the result.
        if (byValue.getOrDefault(function.getName(true), false)) {
            body = body.replaceAll("\\bself\\b", "this");
        }

        body = methodCalls(body, ownerClass(function));
        body = nameLocals(body, renames);
        body = renameAll(body, renames);

        return comment + signature + "\n" + body.replaceAll("\n{3,}", "\n\n");
    }

    // Folding reads patterns in the text; a function it trips over is written as the decompiler gave it.
    private String foldedOrAsIs(String body) {
        try {
            return MT2Fold.fold(body);
        } catch (RuntimeException problem) {
            return body;
        }
    }

    // The decompiler works out what a function returns while decompiling it; the stored signature doesn't know yet.
    // Its first line reads: "void __thiscall mmoCharacter::EquipWeaponModel(mmoCharacter *this,bool param_1)".
    private void rememberReturnType(Function function, String prelude) {
        String qualified = function.getName(true);
        String spelled = qualified.replace("[abi:cxx11]", "_abi_cxx11_");
        String[] lines = prelude.split("\n");

        // The signature is the last line naming the function outside a comment; comments can name it too.
        for (int index = lines.length - 1; index >= 0; index--) {
            String line = lines[index];
            int at = Math.max(line.indexOf(qualified + "("), line.indexOf(spelled + "("));

            if (at <= 0 || line.contains("/*") || line.trim().startsWith("//")) {
                continue;
            }

            String returns = line.substring(0, at).replaceAll("\\b__\\w+\\b", "").trim();

            // A class returned by value comes back through a hidden pointer: the function "returns" that pointer.
            if (line.contains("__return_storage_ptr__") && returns.endsWith("*")) {
                returns = returns.substring(0, returns.length() - 1).trim();
            }

            if (!returns.isEmpty()) {
                returnTypes.put(function, cleanType(types(returns)));
            }

            return;
        }
    }

    // The comment Ghidra puts before the function: the mappings' notes. Its demangled-name line is left out, since the
    // signature now says the same.
    private String plateComment(String prelude) {
        int start = prelude.indexOf("/*");
        int end = prelude.lastIndexOf("*/");

        if (start < 0 || end < 0) {
            return "";
        }

        String inside = prelude.substring(start + 2, end).trim();

        if (inside.isEmpty() || inside.matches("[\\w:~<>, *&\\[\\]]+\\(.*\\)( const)?")) {
            return "";
        }

        StringBuilder comment = new StringBuilder();

        for (String line : inside.split("\n")) {
            comment.append("// ").append(line.trim()).append("\n");
        }

        return comment.toString();
    }

    // The signature as C++ declares it: the return type Ghidra worked out, then the qualified name and the exact
    // parameter types from the mangled name (float const*, std::string const&), with a name for each parameter.
    private String cppSignature(Function function, Map<String, String> renames) {
        if (signatures.containsKey(function)) {
            return signatures.get(function);
        }

        DemangledObject demangled = demangled(function);
        // [abi:cxx11] marks functions returning a std::string; the source didn't write it.
        String qualified = function.getName(true).replace("[abi:cxx11]", "");
        List<String> types = new ArrayList<>();
        boolean isConst = false;

        if (demangled != null) {
            String full = demangled.getSignature(false);
            int open = full.indexOf('(');
            int close = full.lastIndexOf(')');

            if (open >= 0 && close > open) {
                types = splitParameters(full.substring(open + 1, close));
                isConst = full.substring(close).contains("const");
            }
        }

        List<Parameter> parameters = new ArrayList<>();

        for (Parameter parameter : function.getParameters()) {
            String name = parameter.getName();

            boolean hidden = name.equals("this") || name.equals("__return_storage_ptr__")
                || byValue.containsKey(function.getName(true)) && (name.equals("result") || name.equals("self"));

            if (!parameter.isAutoParameter() && !hidden) {
                parameters.add(parameter);
            }
        }

        List<String> declared = new ArrayList<>();
        Set<String> used = new HashSet<>();

        for (int index = 0; index < types.size(); index++) {
            String type = cleanType(types.get(index));
            String ghidraName = index < parameters.size() ? parameters.get(index).getName() : null;
            String name = ghidraName != null && !GHIDRA_LOCAL.matcher(ghidraName).matches() ? ghidraName : nameForType(type, "value");

            name = unique(name, used);

            if (ghidraName != null && !ghidraName.equals(name)) {
                renames.put(ghidraName, name);
            }

            declared.add(type.equals("void") && types.size() == 1 ? "" : type + " " + name);
        }

        if (types.isEmpty()) {
            for (Parameter parameter : parameters) {
                declared.add(types(parameter.getDataType().getDisplayName()) + " " + parameter.getName());
            }
        }

        String returns = returnTypes.getOrDefault(function, cleanType(types(function.getReturnType().getDisplayName())));

        // What such a function returns is what "result" points at.
        if (byValue.containsKey(function.getName(true))) {
            returns = cleanType(types(function.getParameter(0).getDataType().getDisplayName())).replaceAll("\\*$", "").trim();
        }

        // GCC tags functions that return a std::string with [abi:cxx11]. Ghidra misreads their hidden result pointer
        // (GCC passes it before "this"), so the tag is the surer guide.
        if (function.getName(true).contains("[abi:cxx11]")) {
            returns = "std::string";
        }
        boolean isStructor = qualified.endsWith("::" + function.getName()) && ownerClass(function) != null
            && (ownerClass(function).endsWith(function.getName()) || function.getName().startsWith("~"));
        String signature = (isStructor ? "" : returns + " ") + qualified + "(" + String.join(", ", declared) + ")" + (isConst ? " const" : "");

        if (returnTypes.containsKey(function)) {
            signatures.put(function, signature);
        }

        return signature;
    }

    private DemangledObject demangled(Function function) {
        for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(function.getEntryPoint())) {
            String name = symbol.getName().replaceAll("\\.(part|isra|constprop|cold)\\.?\\d*.*$", "");

            if (name.startsWith("_Z")) {
                try {
                    List<DemangledObject> found = DemanglerUtil.demangle(currentProgram, name, function.getEntryPoint());

                    if (found != null && !found.isEmpty()) {
                        return found.get(0);
                    }
                } catch (Exception problem) {
                    return null;
                }
            }
        }

        return null;
    }

    private List<String> splitParameters(String list) {
        List<String> parts = new ArrayList<>();
        int depth = 0;
        int start = 0;

        for (int index = 0; index < list.length(); index++) {
            char character = list.charAt(index);

            if (character == '<' || character == '(') {
                depth++;
            } else if (character == '>' || character == ')') {
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

    // "float const*" as C++ writes it more often: "const float*"; the long std::string spelling as std::string.
    private String cleanType(String type) {
        String cleaned = type.replace("std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >", "std::string")
            .replace("std::__cxx11::basic_string<char,std::char_traits<char>,std::allocator<char>>", "std::string")
            .replace("std::__cxx11::", "std::").trim();
        Matcher constAfter = Pattern.compile("^([\\w:<>, ]+?) const\\s*([*&].*)?$").matcher(cleaned);

        if (constAfter.matches()) {
            cleaned = "const " + constAfter.group(1) + (constAfter.group(2) != null ? constAfter.group(2) : "");
        }

        return cleaned.replace(" *", "*").replace(" &", "&");
    }

    private String types(String text) {
        String result = text.replace("std::__cxx11::", "std::").replace("_abi_cxx11_", "")
            .replace("basic_string<char,std::char_traits<char>,std::allocator<char>>", "string")
            .replaceAll("\\b__thiscall\\b\\s*|\\b__fastcall\\b\\s*|\\b__cdecl\\b\\s*", "");

        for (String[] word : TYPE_WORDS) {
            result = result.replaceAll("\\b" + word[0] + "\\b", word[1]);
        }

        // The structure MT2Apply made for a template shows its name spelled for Ghidra: vsArray_T_ is vsArray<T>.
        result = result.replaceAll("\\b([A-Za-z]\\w*?)_T_\\b", "$1<T>");

        return result.replaceAll("(?<![\\w:])string(?=\\s*[*&)])", "std::string");
    }

    // std::operator==(a, b) is a == b.
    private String operators(String text) {
        String[] symbols = { "==", "!=", "<=", ">=", "<", ">", "+" };
        String result = text;

        for (String symbol : symbols) {
            String call = "std::operator" + symbol + "(";
            int at = result.indexOf(call);

            while (at >= 0) {
                int close = matchingParenthesis(result, at + call.length() - 1);
                List<String> arguments = close < 0 ? List.of() : splitArguments(result.substring(at + call.length(), close));

                if (arguments.size() != 2) {
                    at = result.indexOf(call, at + call.length());
                    continue;
                }

                String replacement = "(" + reference(arguments.get(0)) + " " + symbol + " " + reference(arguments.get(1)) + ")";
                result = result.substring(0, at) + replacement + result.substring(close + 1);
                at = result.indexOf(call, at + replacement.length());
            }
        }

        return result;
    }

    // mmoCharacterType::GetCostume(characterType) is characterType->GetCostume(); RemoveWeaponModel(this) inside a
    // method of the same class is RemoveWeaponModel().
    private String methodCalls(String text, String owner) {
        StringBuilder result = new StringBuilder();
        int position = 0;
        Matcher name = Pattern.compile("([A-Za-z_~][\\w]*(?:<[^()]*?>)?(?:::[A-Za-z_~][\\w]*(?:<[^()]*?>)?)*)\\(").matcher(text);

        while (name.find(position)) {
            int open = name.end() - 1;
            int close = matchingParenthesis(text, open);
            String called = name.group(1);
            String full = called.contains("::") ? called : owner != null ? owner + "::" + called : called;

            boolean isByValue = byValue.containsKey(full);

            if (close < 0 || !methods.contains(full) && !isByValue || isDeclarationLine(text, name.start())) {
                result.append(text, position, name.end());
                position = name.end();
                continue;
            }

            List<String> arguments = splitArguments(text.substring(open + 1, close));

            if (arguments.isEmpty() || isByValue && byValue.get(full) && arguments.size() < 2) {
                result.append(text, position, name.end());
                position = name.end();
                continue;
            }

            // mmoGroup::GetProps(&props, group) is props = group->GetProps().
            String destination = isByValue ? reference(arguments.get(0)) + " = " : "";

            if (isByValue) {
                arguments = arguments.subList(1, arguments.size());
            }

            if (isByValue && !byValue.get(full)) {
                result.append(text, position, name.start()).append(destination).append(called).append("(")
                    .append(methodCalls(String.join(", ", arguments), owner)).append(")");
                position = close + 1;
                continue;
            }

            String object = stripCast(arguments.get(0));
            String method = full.substring(full.lastIndexOf("::") + 2);
            String rest = methodCalls(String.join(", ", arguments.subList(1, arguments.size())), owner);

            result.append(text, position, name.start()).append(destination);

            if (object.equals("this")) {
                result.append(method);
            } else {
                String target = methodCalls(object, owner);
                boolean simple = target.matches("[\\w.>\\-\\[\\]]+") || target.matches("\\(.*\\)");
                Matcher addressOf = Pattern.compile("^\\(?&([\\w:.\\[\\]>\\-]+)\\)?$").matcher(target);

                // (&vsRandomSource::Default)->GetFloat() is vsRandomSource::Default.GetFloat().
                if (addressOf.matches()) {
                    result.append(addressOf.group(1)).append(".").append(method);
                } else {
                    result.append(simple ? target : "(" + target + ")").append("->").append(method);
                }
            }

            result.append("(").append(rest).append(")");
            position = close + 1;
        }

        result.append(text.substring(position));

        return result.toString();
    }

    private boolean isDeclarationLine(String text, int at) {
        int lineStart = text.lastIndexOf('\n', at) + 1;

        return at == lineStart;
    }

    // A reference is an address in machine code, so Ghidra writes &x where the source passed x.
    private String reference(String argument) {
        String stripped = stripCast(argument);

        return stripped.startsWith("&") && !stripped.startsWith("&&") ? stripped.substring(1) : stripped;
    }

    private String stripCast(String expression) {
        String trimmed = expression.trim();
        Matcher cast = Pattern.compile("^\\(([\\w:<>, ]+\\s*\\**)\\)\\s*(.+)$", Pattern.DOTALL).matcher(trimmed);

        // A pointer cast whose type has a pointer in its template arguments: (vsArray<mmoProp_const*> *)&props.
        if (!cast.matches()) {
            cast = Pattern.compile("^\\(([\\w:<>, *]*<[\\w:<>, *]*>\\s*\\*+)\\)\\s*(.+)$", Pattern.DOTALL).matcher(trimmed);
        }

        return cast.matches() && !cast.group(2).startsWith("(") ? cast.group(2).trim() : trimmed;
    }

    private int matchingParenthesis(String text, int open) {
        int depth = 0;
        boolean inString = false;

        for (int index = open; index < text.length(); index++) {
            char character = text.charAt(index);

            if (character == '"' && text.charAt(index - 1) != '\\') {
                inString = !inString;
            } else if (inString) {
                continue;
            } else if (character == '(') {
                depth++;
            } else if (character == ')' && --depth == 0) {
                return index;
            }
        }

        return -1;
    }

    private List<String> splitArguments(String list) {
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
            } else if (character == '(' || character == '[' || character == '{') {
                depth++;
            } else if (character == ')' || character == ']' || character == '}') {
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

    // Locals are named after what's first put in them: the result of GetCostume() is costume, of
    // vsSingleton<mmoWeaponModelManager>::Instance() weaponModelManager; else after their class (mmoZone* is zone).
    private String nameLocals(String body, Map<String, String> renames) {
        Set<String> used = new HashSet<>(renames.values());
        Map<String, String> declaredTypes = new LinkedHashMap<>();

        for (String line : body.split("\n")) {
            Matcher declaration = DECLARATION.matcher(line);

            if (line.trim().isEmpty() && !declaredTypes.isEmpty()) {
                break;
            }

            if (declaration.matches()) {
                declaredTypes.put(declaration.group(3), declaration.group(1).trim() + declaration.group(2));
            }
        }

        for (String name : declaredTypes.keySet()) {
            if (!GHIDRA_LOCAL.matcher(name).matches()) {
                used.add(name);
            }
        }

        for (Map.Entry<String, String> local : declaredTypes.entrySet()) {
            String name = local.getKey();

            if (!GHIDRA_LOCAL.matcher(name).matches() || renames.containsKey(name)) {
                continue;
            }

            String chosen = nameFromAssignment(body, name);

            if (chosen == null && (local.getValue().endsWith("*") || classes.containsKey(local.getValue()))) {
                chosen = nameForType(local.getValue(), null);
            }

            if (chosen != null) {
                renames.put(name, unique(chosen, used));
            }
        }

        return body;
    }

    private String nameFromAssignment(String body, String local) {
        Matcher assignment = Pattern.compile("\\b" + Pattern.quote(local) + " = (?:\\([^()]*\\))?\\s*([^;]+);").matcher(body);

        if (!assignment.find()) {
            return null;
        }

        Matcher call = Pattern.compile("(?:[\\w<>:]+(?:->|\\.|::))?(\\w+)\\(([^()]*)\\)$").matcher(assignment.group(1).trim());

        if (!call.find()) {
            return null;
        }

        String method = call.group(1);
        String expression = assignment.group(1);

        if (method.equals("Instance") || method.equals("GetInstance")) {
            Matcher owner = Pattern.compile("(\\w+)>?::(?:Get)?Instance\\(").matcher(expression);

            return owner.find() ? nameForType(owner.group(1), null) : null;
        }

        Matcher verb = Pattern.compile("^(?:Get|Find|Create|Make|New|Pick|Choose)([A-Z]\\w*)$").matcher(method);

        if (verb.matches()) {
            return lowerFirst(verb.group(1));
        }

        if (method.matches("^(?:Is|Has|Can|Should|Was)[A-Z]\\w*$")) {
            return lowerFirst(method);
        }

        return null;
    }

    private String nameForType(String type, String fallback) {
        String base = type.replace("const", "").replace("*", "").replace("&", "").trim();

        base = base.replaceAll("<.*>", "");
        base = base.contains("::") ? base.substring(base.lastIndexOf("::") + 2) : base;

        switch (base) {
            case "string": return "text";
            case "bool": return fallback != null ? "flag" : null;
            case "float": case "double": return fallback != null ? "amount" : null;
            case "int": case "long": case "short": case "unsigned int": case "long long": case "unsigned long long":
                return fallback != null ? "number" : null;
            case "char": return fallback != null ? "character" : null;
            case "void": case "uint8_t": case "uint16_t": case "uint32_t": case "uint64_t": return fallback;
            default: break;
        }

        if (!base.matches("[A-Za-z_]\\w*")) {
            return fallback;
        }

        String stripped = base.replaceFirst("^(?:mmo|vs|MMO|VS)(?=[A-Z])", "");

        return lowerFirst(stripped.isEmpty() ? base : stripped);
    }

    private String lowerFirst(String text) {
        return text.isEmpty() ? text : Character.toLowerCase(text.charAt(0)) + text.substring(1);
    }

    private String unique(String name, Set<String> used) {
        String base = KEYWORDS.contains(name) ? name + "Value" : name;
        String chosen = base;

        for (int number = 2; used.contains(chosen); number++) {
            chosen = base + number;
        }

        used.add(chosen);

        return chosen;
    }

    private String renameAll(String body, Map<String, String> renames) {
        String result = body;

        for (Map.Entry<String, String> rename : renames.entrySet()) {
            result = result.replaceAll("\\b" + Pattern.quote(rename.getKey()) + "\\b", Matcher.quoteReplacement(rename.getValue()));
        }

        return result;
    }

    private String headerFile(Output out) {
        StringBuilder text = new StringBuilder();
        String fileName = headerPath(out.path.substring(out.path.lastIndexOf('/') + 1));

        text.append("// ").append(fileName).append(", rebuilt from MT2.exe ").append(build).append(" by mt2sdk decompile.\n");
        text.append("// The game's headers aren't in the exe: this is what the exe says about each class. Field offsets are in the\n");
        text.append("// comments; fields nobody has named yet are left out, so it doesn't compile as it is.\n\n#pragma once\n");

        List<String> sorted = new ArrayList<>(out.classes);
        sorted.sort(String::compareTo);

        for (String name : sorted) {
            text.append("\n").append(classDeclaration(name, out));
        }

        return text.toString();
    }

    private String classDeclaration(String name, Output out) {
        ClassInfo info = classes.get(name);
        StringBuilder text = new StringBuilder();
        String shortName = name.contains("::") ? name.substring(name.lastIndexOf("::") + 2) : name;

        text.append("class ").append(shortName);

        if (info != null && !info.bases.isEmpty()) {
            text.append(" : public ").append(String.join(", public ", info.bases));
        }

        text.append("\n{\npublic:\n");

        if (info != null && info.size > 0) {
            text.append("    // 0x").append(Integer.toHexString(info.size)).append(" bytes\n");
        }

        appendEnums(text, name);
        appendFields(text, info);
        appendVirtuals(text, info, name);
        appendMethods(text, out, name, info);
        text.append("};\n");

        return text.toString();
    }

    private void appendEnums(StringBuilder text, String owner) {
        for (Map.Entry<String, List<String>> game : enums.entrySet()) {
            String name = game.getKey();

            if (name.startsWith(owner + "::") && !name.substring(owner.length() + 2).contains("::")) {
                text.append("    enum ").append(name.substring(owner.length() + 2)).append(" { ")
                    .append(enumValues(game.getValue())).append(" };\n");
            }
        }
    }

    private void appendFields(StringBuilder text, ClassInfo info) {
        if (info == null || info.fields.isEmpty()) {
            return;
        }

        text.append("\n");

        for (Map.Entry<Integer, String[]> field : info.fields.entrySet()) {
            // Inside mmoNPC, its own enum mmoNPC::Type is just Type.
            String type = cleanType(field.getValue()[0]).replace(info.name + "::", "");
            String declaration = "    " + type + " " + field.getValue()[1] + ";";
            String note = field.getValue()[2].isEmpty() ? "" : " " + field.getValue()[2];

            text.append(String.format("%-60s // +0x%x%s%n", declaration, field.getKey(), note));
        }
    }

    // Only the virtual functions this class writes itself: the rest it has from its bases.
    private void appendVirtuals(StringBuilder text, ClassInfo info, String owner) {
        if (info == null) {
            return;
        }

        // A destructor fills two slots (it destroys; its deleting copy also frees): one line, with both slots.
        Map<String, List<Long>> own = new LinkedHashMap<>();

        for (long[] entry : info.vtable) {
            Function function = entry[1] == 0 ? getFunctionAt(toAddr(entry[0])) : null;

            if (function != null && owner.equals(ownerClass(function))) {
                own.computeIfAbsent(declarationOf(function, owner), key -> new ArrayList<>()).add(entry[2]);
            }
        }

        if (own.isEmpty()) {
            return;
        }

        text.append("\n");

        for (Map.Entry<String, List<Long>> virtual : own.entrySet()) {
            List<String> slots = virtual.getValue().stream().map(String::valueOf).toList();
            text.append("    virtual ").append(virtual.getKey()).append("; // slot").append(slots.size() > 1 ? "s " : " ")
                .append(String.join(", ", slots)).append("\n");
        }
    }

    private void appendMethods(StringBuilder text, Output out, String owner, ClassInfo info) {
        Set<String> virtuals = new HashSet<>();

        if (info != null) {
            for (long[] entry : info.vtable) {
                virtuals.add(Long.toHexString(entry[0]));
            }
        }

        Set<String> declared = new java.util.LinkedHashSet<>();

        for (Decompiled result : out.functions) {
            Function function = result.function();

            if (owner.equals(ownerClass(function)) && !virtuals.contains(function.getEntryPoint().toString())) {
                declared.add("    " + declarationOf(function, owner) + ";");
            }
        }

        if (!declared.isEmpty()) {
            text.append("\n").append(String.join("\n", declared)).append("\n");
        }
    }

    private String declarationOf(Function function, String owner) {
        String signature = cppSignature(function, new LinkedHashMap<>());

        return signature.replace(owner + "::", "");
    }

    // A value without a word (a gap in the game's list) gets its number, so the others keep theirs.
    private String enumValues(List<String> words) {
        List<String> values = new ArrayList<>();

        for (int index = 0; index < words.size(); index++) {
            values.add(words.get(index).isEmpty() ? "Value" + index + " = " + index : words.get(index));
        }

        return String.join(", ", values);
    }

    private String enumsFile() {
        StringBuilder text = new StringBuilder("// The game's enums that belong to no class, rebuilt from MT2.exe " + build
            + ": the words are the ones its data files and saves use.\n\n#pragma once\n");

        for (Map.Entry<String, List<String>> game : enums.entrySet()) {
            String name = game.getKey();
            String owner = name.contains("::") ? name.substring(0, name.lastIndexOf("::")) : null;

            if (owner == null || !classes.containsKey(owner)) {
                text.append("\nenum ").append(name.replace("::", "_")).append(" { ").append(enumValues(game.getValue())).append(" };\n");
            }
        }

        return text.toString();
    }

    private String readme(int fileCount, int functionCount) {
        return "# MT2 " + build + ", rebuilt as C++\n\n"
            + "Made by `mt2sdk decompile` from your own copy of MT2.exe: " + functionCount + " functions in " + fileCount + " files.\n"
            + "Don't share these files: they're the game's code. Share what you learn in mt2-mappings instead.\n\n"
            + "- The folders are the game's own (`Games/MMORPG/...`, `vectorstorm/...`): each `.cpp` holds the functions the exe\n"
            + "  says came from that source file, in the exe's order, and the `.h` beside it declares its classes.\n"
            + "- Field names in `this->field` are the game's own (the names its data files and saves use) or from mt2-mappings.\n"
            + "  The rest come from the code itself (the header says which, in each field's comment): `m_scene` from an assert\n"
            + "  or a getter such as `GetScene()` (the game's own `m_` names), and a type from how the code uses the field. A\n"
            + "  field known only by its type is named after it (`scene` for a `vsScene*`); one with no name keeps `field_0x4c`.\n"
            + "- `vsAssert(m_scene, \"...\")` and `vsLog(\"...\", a, b)` are the game's own asserts and log lines, with the source\n"
            + "  line they were on. The asserts' conditions are the source's own text.\n"
            + "- Small engine functions the compiler copied in are folded back into their call (`models.AddItem(model)`), and an\n"
            + "  object copied piece by piece is one assignment.\n"
            + "- Enum values are the game's words (`Enums.h`, and each class's own enums in its header).\n"
            + "- Locals are named after what they hold (`costume`, `weaponModelManager`); those Ghidra couldn't name keep names\n"
            + "  like `iVar3`. Bodies come from optimized machine code: loops may be unrolled and branches joined with `goto`.\n"
            + "- `_templates/` holds the compiler's copies of templates (`vsProperty<int, mmoNPC>`), `_other/` code whose source\n"
            + "  file has no known folder.\n"
            + "- `index.txt` lists every function's address, name and file. Search it, or search the folder.\n";
    }

    private String safe(String name) {
        String cleaned = name.replaceAll("[<>:\"/\\\\|?*\\s]", "_");

        return cleaned.length() > 120 ? cleaned.substring(0, 120) + "_" + Integer.toHexString(name.hashCode()) : cleaned;
    }

    private static class Configurer implements DecompileConfigurer {
        @Override
        public void configure(DecompInterface decompiler) {
            DecompileOptions options = new DecompileOptions();
            options.setPLATECommentIncluded(true);
            options.setPRECommentIncluded(true);
            options.setEOLCommentIncluded(true);
            decompiler.setOptions(options);
            decompiler.toggleCCode(true);
            decompiler.toggleSyntaxTree(false);
        }
    }
}
