// Writes MT2.exe's code as C++ source files laid out like the game's own source tree (Games/MMORPG/.../MMO_Quest.cpp),
// one .cpp per source file the exe names, with a .h beside it declaring its classes. Functions keep the order the
// exe has them in, which follows the original files. Uses what MT2Apply taught Ghidra (fields, enums, mappings), the
// exact C++ parameter types from the exe's mangled names, and names locals after what they hold.
// Arguments: the output folder, program.json (mt2sdk program), mappings.json or "-", "game" (the game's and engine's
// own code) or "all", and optionally the source files to write alone, with commas between (MMO_District.cpp, or its
// path when the name repeats).
// @category MT2

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.decompiler.ClangFieldToken;
import ghidra.app.decompiler.ClangNode;
import ghidra.app.decompiler.ClangToken;
import ghidra.app.decompiler.ClangVariableToken;
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
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeComponent;
import ghidra.program.model.data.Structure;
import ghidra.program.model.data.Undefined;
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
    private static final Set<String> LIBRARIES = Set.of("std", "__gnu_cxx", "__cxxabiv1", "tinyformat", "physfs", "SDL");

    private static final Pattern GHIDRA_LOCAL = Pattern.compile(
        "^(?:[a-z]{1,4}Var\\d+|this_\\d+|local_[0-9a-f]+|local_[A-Z0-9]+_[0-9a-f]+|[a-z]{1,3}Stack_[0-9a-f]+|in_\\w+|extraout_\\w+|unaff_\\w+|param_\\d+)$");
    private static final Pattern DECLARATION = Pattern.compile("^\\s+([\\w:<>, ]+?)\\s*(\\**)\\s*(\\w+)(\\s*\\[\\d+\\])?;$");
    private static final Set<String> KEYWORDS = Set.of("new", "delete", "class", "this", "default", "switch", "case", "int", "float",
        "bool", "char", "double", "long", "short", "signed", "unsigned", "void", "const", "static", "return", "if", "else",
        "for", "while", "do", "break", "continue", "goto", "struct", "union", "enum", "template", "typename", "operator",
        "private", "public", "protected", "virtual", "friend", "namespace", "using", "true", "false", "sizeof", "auto");
    private static final String[][] TYPE_WORDS = {
        { "ulonglong", "unsigned long long" }, { "longlong", "long long" }, { "uint", "unsigned int" },
        { "ushort", "unsigned short" }, { "uchar", "unsigned char" },
        // Ghidra's undefinedN is N bytes whose type nobody knows yet: the plain type of that size (int is 4 bytes, long
        // long 8; long is only 4 on Windows, so it's never used here).
        { "undefined8", "long long" }, { "undefined4", "int" }, { "undefined2", "short" }, { "undefined1", "char" },
        { "undefined", "char" }, { "byte", "char" }, { "sbyte", "signed char" }, { "qword", "long long" }, { "dword", "int" },
        { "word", "short" },
    };

    private static final Pattern UNNAMED_FIELD = Pattern.compile("^field\\d*_0x[0-9a-f]+$");
    private static final String UNNAMED_FIELDS = "unnamed_fields.tsv";
    private static final int MAX_USERS_LISTED = 5;

    // A field nobody has named, as one function reads or writes it: its class, offset and type.
    private record FieldUse(String owner, int offset, String type) {}

    private record Decompiled(Function function, String code, List<FieldUse> unnamed) {}

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
    private final Map<String, Map<String, Integer>> classVotes = new HashMap<>();
    // The files to write (after a change to the mappings or the scripts), or null for all.
    private Set<String> onlyFiles;

    @Override
    public void run() throws Exception {
        String[] arguments = getScriptArgs();
        File output = arguments.length > 0 ? new File(arguments[0]) : askDirectory("Folder for the source files", "Export");
        File programFile = arguments.length > 1 ? new File(arguments[1]) : askFile("program.json from mt2sdk program", "Use");
        File mappingsFile = arguments.length > 2 && !arguments[2].equals("-") ? new File(arguments[2]) : null;
        boolean everything = arguments.length > 3 && arguments[3].equals("all");
        readProgram(readJson(programFile));
        onlyFiles = arguments.length > 4 ? findOnlyFiles(arguments[4]) : null;

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
            String path = outputPath(function);

            if (everything || gamesOwn) {
                voteForHome(function, path);
            }

            if ((everything || gamesOwn) && (onlyFiles == null || onlyFiles.contains(path))) {
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

    // The decompiler's own tokens know which structure and offset each "->field_0x4c" is.
    private void collectUnnamedFields(ClangNode node, List<FieldUse> into) {
        List<ClangNode> tokens = new ArrayList<>();
        node.flatten(tokens);

        for (int at = 0; at < tokens.size(); at++) {
            if (!(tokens.get(at) instanceof ClangFieldToken field) || !(field.getDataType() instanceof Structure structure)
                || !UNNAMED_FIELD.matcher(field.getText()).matches() || isMistypedPointer(tokens, at)) {
                continue;
            }

            DataTypeComponent component = structure.getComponentContaining(field.getOffset());
            DataType type = component != null ? component.getDataType() : null;
            String typeName = type == null || type == DataType.DEFAULT || type instanceof Undefined ? "?" : cleanType(types(type.getDisplayName()));

            into.add(new FieldUse(className(structure), field.getOffset(), typeName));
        }
    }

    // nullObject[3].field_0x8, this[-1].field_0x18: a pointer stepped a fixed number of whole structures away is one
    // Ghidra gave the wrong type (a base class, or the class a member sits in), not an array; the offset isn't that
    // class's field. A real array steps by a counter (items[index].field_0x8) or is a member (this->colors[2]).
    private boolean isMistypedPointer(List<ClangNode> tokens, int field) {
        int at = previousToken(tokens, field);

        if (at < 0 || !text(tokens, at).equals(".") || (at = previousToken(tokens, at)) < 0 || !text(tokens, at).equals("]")) {
            return false;
        }

        boolean fixedStep = false;
        int depth = 0;

        for (at = previousToken(tokens, at); at >= 0; at = previousToken(tokens, at)) {
            String text = text(tokens, at);

            if (text.equals("]")) {
                depth++;
            } else if (text.equals("[") && depth-- == 0) {
                break;
            } else if (text.matches("-?(0x[0-9a-f]+|\\d+)")) {
                fixedStep = true;
            }
        }

        int base = at < 0 ? -1 : previousToken(tokens, at);

        return fixedStep && base >= 0 && tokens.get(base) instanceof ClangVariableToken;
    }

    private int previousToken(List<ClangNode> tokens, int at) {
        for (int index = at - 1; index >= 0; index--) {
            if (!text(tokens, index).isBlank()) {
                return index;
            }
        }

        return -1;
    }

    private String text(List<ClangNode> tokens, int at) {
        return tokens.get(at) instanceof ClangToken token && token.getText() != null ? token.getText().trim() : "";
    }

    // A class's structure sits in a folder named after the namespace it's in: /Demangler/mmoNPC/Advert is mmoNPC::Advert.
    private String className(Structure structure) {
        String name = types(structure.getName());
        String folder = structure.getCategoryPath().getPath();

        if (folder.startsWith("/Demangler/")) {
            return cleanType(types(folder.substring("/Demangler/".length()).replace("/", "::") + "::" + name));
        }

        return name;
    }

    // Each function goes to its source file; code the compiler made from a template (vsProperty<int, mmoNPC>::Type)
    // goes to _templates/vsProperty.cpp, beside the other copies of that template.
    private String outputPath(Function function) {
        SourceFile file = fileOf(function);
        String template = templateOf(function);

        if (template != null) {
            return TEMPLATES + safe(template) + ".cpp";
        }

        // A library's function the compiler copied into a game file (std::ios::widen, specialized for it): the library's
        // code, not the game's.
        String root = function.getName(true).split("::")[0];

        if (LIBRARIES.contains(root)) {
            return "_library/" + safe(root) + ".cpp";
        }

        return file != null && !file.path.isEmpty() ? file.path : "_other/" + (file != null ? safe(file.name) : "unknown.cpp");
    }

    // A class is declared in the header beside the .cpp that has most of its functions. Every function votes, so a
    // single file's export declares the same classes as the whole one.
    private void voteForHome(Function function, String path) {
        String owner = ownerClass(function);

        if (owner != null && templateOf(function) == null) {
            classVotes.computeIfAbsent(owner, key -> new HashMap<>()).merge(path, 1, Integer::sum);
        }
    }

    // "MMO_District.cpp,_templates/vsArray" names Games/MMORPG/MapObjects/MMO_District.cpp and _templates/vsArray.cpp:
    // a file's name, or as much of its path as tells it apart.
    private Set<String> findOnlyFiles(String givenList) throws Exception {
        Set<String> paths = new java.util.TreeSet<>();
        Set<String> chosen = new java.util.TreeSet<>();

        for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
            paths.add(outputPath(function));
        }

        for (String given : givenList.split(",")) {
            String wanted = given.trim().replace('\\', '/');
            List<String> found = new ArrayList<>();

            for (String path : paths) {
                String withoutExtension = path.replaceAll("\\.(cpp|c|cc)$", "");

                if (path.equals(wanted) || withoutExtension.equals(wanted) || path.endsWith("/" + wanted) || withoutExtension.endsWith("/" + wanted)) {
                    found.add(path);
                }
            }

            if (found.size() != 1) {
                throw new IllegalArgumentException(found.isEmpty() ? "No source file is called " + wanted
                    : wanted + " could be any of: " + String.join(", ", found));
            }

            chosen.add(found.get(0));
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
                boolean completed = results.decompileCompleted();
                String code = completed ? results.getDecompiledFunction().getC()
                    : "{\n  // Ghidra couldn't decompile this function: " + results.getErrorMessage() + "\n}\n";
                List<FieldUse> unnamed = new ArrayList<>();

                if (completed) {
                    collectUnnamedFields(results.getCCodeMarkup(), unnamed);
                }

                return new Decompiled(results.getFunction(), code, unnamed);
            }
        };
        callback.setTimeout(TIMEOUT_SECONDS);

        try {
            return ParallelDecompiler.decompileFunctions(callback, functions, monitor);
        } finally {
            callback.dispose();
        }
    }

    private Map<String, Output> arrange(List<Decompiled> results) {
        Map<String, Output> outputs = new TreeMap<>();

        for (Decompiled result : results) {
            Function function = result.function();
            SourceFile file = fileOf(function);
            boolean isTemplate = templateOf(function) != null;
            Output out = outputs.computeIfAbsent(outputPath(function), key -> {
                Output created = new Output();
                created.path = key;
                created.guessed = !isTemplate && file != null && file.guessed;
                return created;
            });

            out.functions.add(result);
        }

        for (Map.Entry<String, Map<String, Integer>> vote : classVotes.entrySet()) {
            String best = vote.getValue().entrySet().stream().max(Map.Entry.comparingByValue()).get().getKey();

            if (outputs.containsKey(best)) {
                outputs.get(best).classes.add(vote.getKey());
            }
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

        writeUnnamedFields(root, outputs);

        // Some files' export leaves the rest of the folder as the whole export wrote it.
        if (onlyFiles != null) {
            println("Wrote " + String.join(", ", onlyFiles) + " (" + functionCount + " functions)");

            return;
        }

        Files.writeString(root.resolve("Enums.h"), enumsFile(), StandardCharsets.UTF_8);
        Files.writeString(root.resolve("index.txt"), index.toString(), StandardCharsets.UTF_8);
        Files.writeString(root.resolve("README.md"), readme(outputs.size(), functionCount), StandardCharsets.UTF_8);
        println("Wrote " + functionCount + " functions in " + outputs.size() + " files, with index.txt and README.md");
    }

    // For "mt2sdk mappings todo": a line per unnamed field and file, with how often and in how many functions it's used
    // there, and the first few of them. Some files' export replaces only their lines.
    private void writeUnnamedFields(Path root, Map<String, Output> outputs) throws Exception {
        Path path = root.resolve(UNNAMED_FIELDS);
        List<String> lines = new ArrayList<>();

        if (onlyFiles != null && Files.exists(path)) {
            for (String line : Files.readAllLines(path, StandardCharsets.UTF_8)) {
                String[] columns = line.split("\t");

                if (columns.length > 3 && !onlyFiles.contains(columns[3])) {
                    lines.add(line);
                }
            }
        }

        for (Output out : outputs.values()) {
            Map<String, int[]> counts = new TreeMap<>();
            Map<String, List<String>> users = new HashMap<>();

            for (Decompiled result : out.functions) {
                Set<String> inThisFunction = new HashSet<>();

                for (FieldUse use : result.unnamed()) {
                    String key = use.owner() + "\t0x" + Integer.toHexString(use.offset()) + "\t" + use.type();
                    int[] count = counts.computeIfAbsent(key, ignored -> new int[2]);

                    count[0]++;

                    List<String> functions = users.computeIfAbsent(key, ignored -> new ArrayList<>());

                    if (inThisFunction.add(key)) {
                        count[1]++;

                        if (functions.size() < MAX_USERS_LISTED) {
                            functions.add(result.function().getName(true));
                        }
                    }
                }
            }

            for (Map.Entry<String, int[]> count : counts.entrySet()) {
                lines.add(count.getKey() + "\t" + out.path + "\t" + count.getValue()[0] + "\t" + count.getValue()[1] + "\t"
                    + String.join(";", users.get(count.getKey())));
            }
        }

        Files.writeString(path, lines.isEmpty() ? "" : String.join("\n", lines) + "\n", StandardCharsets.UTF_8);
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

        boolean isTemplate = out.path.startsWith(TEMPLATES);
        List<String> bodies = new ArrayList<>();

        for (Decompiled result : out.functions) {
            bodies.add(cppFunction(result.function(), result.code()));
        }

        for (int position = 0; position < out.functions.size(); position++) {
            Function function = out.functions.get(position).function();
            String body = bodies.get(position);
            String variant = variantNote(function);

            index.append("0x").append(function.getEntryPoint()).append("  ").append(function.getName(true)).append("  ")
                .append(out.path).append("\n");

            // The compiler's own: a global's destructor handed to atexit, a file's setup with nothing of the source's left.
            if (function.getName().startsWith("__tcf_") || MT2Idioms.isGlobalSetup(function.getName(true)) && MT2Idioms.isEmpty(bodyOf(body))) {
                continue;
            }

            String destroyer = deletingDestructorNote(out, bodies, position, variant);

            if (destroyer != null) {
                text.append("\n// 0x").append(function.getEntryPoint()).append(" ").append(destroyer).append("\n");
                continue;
            }

            String earlier = seenBodies.putIfAbsent(comparable(body, function, isTemplate), function.getEntryPoint().toString());

            text.append("\n// 0x").append(function.getEntryPoint());

            if (variant != null) {
                text.append(" (").append(variant).append(")");
            }

            // A template's copy for another type: only its name says which.
            if (earlier != null && isTemplate) {
                text.append(" ").append(function.getName(true).replace("[abi:cxx11]", "")).append(": the same code as 0x").append(earlier)
                    .append(" above, for its own types\n");
                continue;
            }

            if (earlier != null) {
                text.append(": the same code as 0x").append(earlier).append(" above\n");
                continue;
            }

            text.append("\n").append(body);
        }

        return text.toString();
    }

    // What's between a function's first "{" and its end (its comment and signature come before).
    private String bodyOf(String function) {
        int open = function.indexOf("\n{");

        return open < 0 ? function : function.substring(open + 1);
    }

    // A deleting destructor is the class's destructor and then operator_delete(this, size): said in a line, when the
    // file has that destructor with the same code. Null otherwise.
    private String deletingDestructorNote(Output out, List<String> bodies, int position, String variant) {
        if (variant == null || !variant.startsWith("deleting destructor")) {
            return null;
        }

        Function function = out.functions.get(position).function();
        Matcher free = Pattern.compile("(?m)^\\s*operator_delete\\(this,\\s*(0x[0-9a-f]+|\\d+)\\);\\n").matcher(bodyOf(bodies.get(position)));

        if (!free.find()) {
            return null;
        }

        String destroys = sameLines(free.replaceFirst(""));

        for (int other = 0; other < out.functions.size(); other++) {
            Function candidate = out.functions.get(other).function();

            if (other != position && candidate.getName(true).equals(function.getName(true)) && sameLines(bodyOf(bodies.get(other))).equals(destroys)) {
                long size = free.group(1).startsWith("0x") ? Long.parseLong(free.group(1).substring(2), 16) : Long.parseLong(free.group(1));

                return function.getName(true) + " (deleting destructor): the destructor at 0x" + candidate.getEntryPoint() + ", then frees its "
                    + size + " bytes";
            }
        }

        return null;
    }

    private String sameLines(String body) {
        return body.replaceAll("LAB_[0-9a-f]+", "LAB").replaceAll("\\n\\s*\\n", "\n").trim();
    }

    // The body as it's compared with the ones before it: labels' addresses don't count, and in a template's copy
    // neither do its own type arguments (vsObject<mmoNPC, mmoCharacter> writes mmoNPC where vsObject<mmoZone, ...>
    // writes mmoZone), its name, nor the addresses of the data each copy has for itself.
    private String comparable(String body, Function function, boolean isTemplate) {
        String result = body.replaceAll("LAB_[0-9a-f]+", "LAB");

        if (!isTemplate) {
            return result;
        }

        String owner = ownerClass(function);
        List<String> arguments = new ArrayList<>();

        if (owner != null && owner.contains("<")) {
            arguments.add(owner);
            arguments.addAll(splitParameters(owner.substring(owner.indexOf('<') + 1, owner.lastIndexOf('>'))));
        }

        // Numbered by place, replaced longest first, so mmoNPC inside vsObject<mmoNPC, ...> doesn't break the whole.
        List<Integer> order = new ArrayList<>();

        for (int index = 0; index < arguments.size(); index++) {
            order.add(index);
        }

        order.sort(Comparator.comparingInt((Integer index) -> arguments.get(index).length()).reversed());
        result = result.replace(function.getName(true).replace("[abi:cxx11]", ""), "SELF");

        for (int index : order) {
            String argument = arguments.get(index).trim();

            if (argument.length() > 2) {
                result = result.replace(argument, "T" + index).replace(argument.replace(" ", ""), "T" + index)
                    .replace(argument.replace(" ", "_"), "T" + index);
            }
        }

        // The mangled spelling in type names: 8mmoClock and 10mmoMonster are both a length and T.
        return result.replaceAll("\\d+T(\\d)", "T$1").replaceAll("\\b(?:DAT|PTR|FUN|s)_[0-9a-f]+", "ADDRESS");
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

        // The compiler's name for the code setting up a file's globals, which no class owns and which returns nothing.
        if (MT2Idioms.isGlobalSetup(function.getName(true))) {
            signature = "// Runs once when the game starts: sets up this file's globals.\nvoid " + function.getName().replace("[abi:cxx11]", "") + "()";
        }

        body = withoutWarnings(body);
        body = types(body);
        body = foldedOrAsIs(body, function.getName(true));
        body = operators(body);
        // In a function returning a class by value, the object is the parameter after the result.
        if (byValue.getOrDefault(function.getName(true), false)) {
            body = body.replaceAll("\\bself\\b", "this");
        }

        body = virtualCalls(body, ownerClass(function));
        // Ghidra reads a virtual call made as the function's last jump as a switch it couldn't rebuild; once the call
        // has its name, it was never a switch.
        body = body.replaceAll("(?m)^[ \\t]*// a switch Ghidra couldn't rebuild\\n(?![^\\n]*code \\*)", "");
        body = methodCalls(body, ownerClass(function));
        body = nameLocals(body, renames);
        body = renameAll(body, renames);

        // GCC's [abi:cxx11] tag is on any function whose result names std::string, a const std::string& too: one whose
        // every return hands back an address returns a reference.
        Matcher returned = Pattern.compile("(?m)^\\s*return (.+);$").matcher(body);
        boolean allAddresses = returned.find();

        returned.reset();

        while (allAddresses && returned.find()) {
            allAddresses = returned.group(1).startsWith("&");
        }

        if (allAddresses && signature.startsWith("std::string ")) {
            signature = "const std::string& " + signature.substring("std::string ".length());
            body = body.replaceAll("(?m)^(\\s*return )&", "$1");
            signatures.computeIfPresent(function, (key, declared) -> "const std::string& " + declared.substring("std::string ".length()));
        }

        return comment + signature + "\n" + body.replaceAll("\n{3,}", "\n\n");
    }

    // Folding reads patterns in the text; a function it trips over is written as the decompiler gave it.
    private String foldedOrAsIs(String body, String functionName) {
        try {
            return MT2Fold.fold(body, functionName);
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

    // The comments Ghidra puts before the function: the mappings' notes. Its demangled name (the signature says the
    // same) and its own warnings are left out.
    private String plateComment(String prelude) {
        StringBuilder comment = new StringBuilder();
        Matcher block = Pattern.compile("/\\*(.*?)\\*/", Pattern.DOTALL).matcher(prelude);

        while (block.find()) {
            String inside = block.group(1).trim();

            if (inside.isEmpty() || inside.startsWith("WARNING") || inside.matches("[\\w:~<>, *&\\[\\]]+\\(.*\\)( const)?( \\[clone [^\\]]*\\])*")) {
                continue;
            }

            for (String line : inside.split("\n")) {
                comment.append("// ").append(line.trim()).append("\n");
            }
        }

        return comment.toString();
    }

    // Ghidra's notes about its own work, inside the body. A switch it couldn't rebuild keeps a short mark, since the
    // call through a pointer that it shows instead is the switch.
    private String withoutWarnings(String body) {
        return body.replaceAll("(?m)^([ \\t]*)/\\* WARNING: Could not recover jumptable[^\\n]*\\*/$", "$1// a switch Ghidra couldn't rebuild")
            .replaceAll("(?m)^[ \\t]*/\\* WARNING[^\\n]*\\*/\\n", "");
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
        // Ghidra wraps a long call after its name: GetItemConst\n        ((vsArrayStore<...> *)&this->costumePart, part).
        Matcher name = Pattern.compile("([A-Za-z_~][\\w]*(?:<[^()]*?>)?(?:::[A-Za-z_~][\\w]*(?:<[^()]*?>)?)*)\\s*\\(").matcher(text);

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
                Matcher addressOf = Pattern.compile("^\\(?&([\\w:.\\[\\]>\\-]+(?:<[\\w:<>, *]+>[\\w:.]*)?)\\)?$").matcher(target);

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

    // A call through an object's vtable, (**(code **)(*(long long *)this + 0x160))(this, color), when the object's class
    // is known (this, or a local declared as one): that class's vtable names the slot, so it's Pulse(color). Its deleting
    // destructor is delete. Left as it is for an object whose class isn't known: the same slot means other functions in
    // other classes.
    private String virtualCalls(String text, String owner) {
        Map<String, String> declared = new HashMap<>();
        Matcher declaration = Pattern.compile("(?m)^\\s+([\\w:<>, ]+?)\\s*\\*\\s*(\\w+);$").matcher(text);

        while (declaration.find()) {
            declared.put(declaration.group(2), declaration.group(1).trim());
        }

        if (owner != null) {
            declared.put("this", owner);
        }

        Matcher call = Pattern.compile("\\(\\*\\*\\(code \\*\\*\\)\\((?:\\*\\(long long \\*\\)(\\w+(?:->\\w+)*)|\\(long long\\)(\\w+(?:->\\w+)*)->vtable) \\+ (0x[0-9a-f]+|\\d+)\\)\\)\\(")
            .matcher(text);
        StringBuilder result = new StringBuilder();
        int position = 0;

        while (call.find(position)) {
            String object = call.group(1) != null ? call.group(1) : call.group(2);
            int close = matchingParenthesis(text, call.end() - 1);
            Function target = close < 0 ? null : slotFunction(classOf(object, declared), number(call.group(3)));
            List<String> arguments = close < 0 ? List.of() : splitArguments(text.substring(call.end(), close));

            if (target == null || arguments.isEmpty() || !stripCast(arguments.get(0)).equals(object)) {
                result.append(text, position, call.end());
                position = call.end();
                continue;
            }

            String name = target.getName().replace("[abi:cxx11]", "");
            String rest = String.join(", ", arguments.subList(1, arguments.size()));
            String variant = variantNote(target);

            result.append(text, position, call.start());

            if (name.startsWith("~") && variant != null && variant.startsWith("deleting")) {
                result.append("delete ").append(object);
            } else {
                result.append(object.equals("this") ? "" : object + "->").append(name).append("(").append(rest).append(")");
            }

            position = close + 1;
        }

        result.append(text.substring(position));

        return result.toString();
    }

    // The class an object is: a local's declared class, or, for this->toon, the class toon points at by its field's
    // type (from the game, the mappings, or a base class's). Null when a step isn't a known pointer to a class.
    private String classOf(String object, Map<String, String> declared) {
        int arrow = object.lastIndexOf("->");

        if (arrow < 0) {
            return declared.get(object);
        }

        String type = fieldType(classOf(object.substring(0, arrow), declared), object.substring(arrow + 2));

        if (type == null || !type.matches("[\\w:<>, ]+\\s*\\*")) {
            return null;
        }

        String pointed = type.substring(0, type.length() - 1).trim();

        return classes.containsKey(pointed) ? pointed : null;
    }

    private String fieldType(String className, String fieldName) {
        ClassInfo info = className != null ? classes.get(className) : null;

        if (info == null) {
            return null;
        }

        for (String[] field : info.fields.values()) {
            if (field[1].equals(fieldName)) {
                return field[0];
            }
        }

        for (String base : info.bases) {
            String type = fieldType(base, fieldName);

            if (type != null) {
                return type;
            }
        }

        return null;
    }

    // The function in a class's vtable at this byte offset, or null.
    private Function slotFunction(String className, long offset) {
        ClassInfo info = className != null ? classes.get(className) : null;

        if (info == null || offset % 8 != 0) {
            return null;
        }

        for (long[] entry : info.vtable) {
            if (entry[1] == 0 && entry[2] == offset / 8) {
                return getFunctionAt(toAddr(entry[0]));
            }
        }

        return null;
    }

    private long number(String text) {
        return text.startsWith("0x") ? Long.parseLong(text.substring(2), 16) : Long.parseLong(text);
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
        int templates = 0;
        int start = 0;
        boolean inString = false;

        for (int index = 0; index < list.length(); index++) {
            char character = list.charAt(index);
            // Ghidra spaces its comparisons (a < b); a < straight after a name opens template arguments.
            boolean afterName = index > 0 && (Character.isLetterOrDigit(list.charAt(index - 1)) || list.charAt(index - 1) == '_');

            if (character == '"' && (index == 0 || list.charAt(index - 1) != '\\')) {
                inString = !inString;
            } else if (inString) {
                continue;
            } else if (character == '(' || character == '[' || character == '{') {
                depth++;
            } else if (character == ')' || character == ']' || character == '}') {
                depth--;
            } else if (character == '<' && afterName) {
                templates++;
            } else if (character == '>' && templates > 0 && list.charAt(index - 1) != '-') {
                templates--;
            } else if (character == ',' && depth == 0 && templates == 0) {
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

            if ("item".equals(chosen) && nameForType(local.getValue(), null) != null) {
                chosen = nameForType(local.getValue(), null);
            }

            if (chosen == null && isCounter(body, name, local.getValue())) {
                chosen = "index";
            }

            if (chosen == null) {
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

        // Ghidra gives one variable to values that share a register (waterHeight, then waterSpread, then 1.0): a field
        // names it only when it's all the variable ever holds.
        if (!call.find()) {
            String first = assignment.group(1).trim();

            while (assignment.find()) {
                if (!assignment.group(1).trim().equals(first)) {
                    return null;
                }
            }

            return nameFromField(first);
        }

        String method = call.group(1);
        String expression = assignment.group(1);

        if (method.equals("Instance") || method.equals("GetInstance")) {
            Matcher owner = Pattern.compile("(\\w+)>?::(?:Get)?Instance\\(").matcher(expression);

            return owner.find() ? nameForType(owner.group(1), null) : null;
        }

        // An item from a container: its class names it better when it's known (nameLocals).
        if (method.matches("GetItem(?:Const)?")) {
            return "item";
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

    // region = this->m_region; capacity = (int)zone->capacity. A field nobody named (field_0x4c) says nothing.
    private String nameFromField(String expression) {
        Matcher field = Pattern.compile("^(?:\\([\\w:<>, *]+\\)\\s*)?\\(?[\\w\\[\\]().>*&-]*?(?:->|\\.)(\\w+)\\)?$").matcher(expression);

        if (!field.matches() || field.group(1).matches("field\\d*_0x[0-9a-f]+|_\\d+_\\d+_|vtable")) {
            return null;
        }

        String name = field.group(1).replaceFirst("^m_(?=[a-zA-Z])", "");

        return name.length() > 1 ? lowerFirst(name) : null;
    }

    // A whole number that goes up by one, in a loop: for (iVar3 = 0; ...; iVar3 = iVar3 + 1), or uVar6 = uVar6 + 1 in a
    // do { } while.
    private boolean isCounter(String body, String local, String type) {
        if (!type.matches("(?:unsigned )?(?:int|long|long long|short)|u?int\\d+_t")) {
            return false;
        }

        String quoted = Pattern.quote(local);

        return Pattern.compile("\\b" + quoted + "(?:\\+\\+| \\+= 1\\b| = " + quoted + " \\+ 1\\b)").matcher(body).find();
    }

    // mmoZone* is zone, std::string text, a function pointer (code*) method, vsArray<mmoItem*> and
    // vector<vsLocArg> items and locArgs. Plain numbers and raw bytes say nothing: null, or the fallback.
    private String nameForType(String type, String fallback) {
        String base = type.replace("const", "").replace("*", "").replace("&", "").trim();
        Matcher container = Pattern.compile("^(?:std::)?(?:vector|vsArray|vsArrayStore|vsObjectArray|vsVolatileArray)<([^,<>]+)[,>]").matcher(base);

        if (container.find()) {
            String item = nameForType(container.group(1), null);

            return item == null ? fallback : MT2Apply.plural(item);
        }

        base = base.replaceAll("<.*>", "");
        base = base.contains("::") ? base.substring(base.lastIndexOf("::") + 2) : base;

        switch (base) {
            case "string": case "basic_string": return "text";
            case "code": return "method";
            case "locale": case "ios_base": case "ostream": case "ostringstream": case "stringbuf": case "streambuf": return fallback;
            case "bool": return fallback != null ? "flag" : null;
            case "float": case "double": return fallback != null ? "amount" : null;
            case "int": case "long": case "short": case "unsigned int": case "long long": case "unsigned long long":
                return fallback != null ? "number" : null;
            // A char* is as often bytes of something unknown as text.
            case "char": return fallback != null && !type.contains("*") ? "character" : fallback;
            case "void": return fallback;
            default: break;
        }

        if (!base.matches("[A-Za-z_]\\w*") || base.matches("u?int\\d*(?:_t)?|s?size_t|ptrdiff_t|wchar_t|char\\d*_t|float\\d*|unk\\w*|undefined\\d*")) {
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
