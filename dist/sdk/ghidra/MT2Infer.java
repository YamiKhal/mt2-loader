// Works out what the code itself says about the fields nobody has named yet, and about locals, so the export reads
// this->m_scene instead of *(long long *)&this->field_0x10:
// - a field's name from an assert that checks it (vsFailedAssert("m_scene", ...) guarding this->field_0x10) or from a
//   small getter or setter (GetScene() returns it, SetScene(scene) stores it);
// - a field's type from how the code uses it: passed where a vsScene* is expected, its address passed as a
//   vsBox3D's "this", a vsArray's vtable written there by the constructor, what a function returning mmoZone* put there;
// - a class's smallest size from the furthest its own methods reach, for classes whose size no code gives;
// - a local's type: a local whose address is passed as a vsBox3D's "this" is a vsBox3D, what __dynamic_cast returns
//   is the class it casts to, what operator new returns before a constructor runs on it is that class;
// - what a function returns, when the decompiler sees it return a class pointer.
// Writes inferred.json, which MT2Apply reads under the game's own facts and mt2-mappings. Each run builds on the last:
// once a field's type is known, what's read through it is known too.
// Arguments: program.json, the inferred.json to write, and optionally a text a file's path must have (MMO_Blueprint).
// @category MT2

import com.google.gson.GsonBuilder;
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
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.GhidraClass;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.VariableUtilities;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.HighSymbol;
import ghidra.program.model.pcode.HighVariable;
import ghidra.program.model.pcode.PcodeBlock;
import ghidra.program.model.pcode.PcodeBlockBasic;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.PcodeOpAST;
import ghidra.program.model.pcode.Varnode;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.Symbol;
import ghidra.util.task.TaskMonitor;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class MT2Infer extends GhidraScript {

    private static final int TIMEOUT_SECONDS = 60;
    private static final String[] ARGUMENT_REGISTERS = { "RCX", "RDX", "R8", "R9" };
    private static final Pattern MEMBER = Pattern.compile("(?<![\\w.>])m_\\w+");
    private static final Pattern GETTER = Pattern.compile("^Get([A-Z]\\w*)$");
    private static final Pattern SETTER = Pattern.compile("^Set([A-Z]\\w*)$");
    private static final Pattern GHIDRA_LOCAL = Pattern.compile("^(?:[a-z]{1,4}Var\\d+|local_[0-9a-f]+|[a-z]{1,3}Stack_[0-9a-f]+)$");
    private static final Pattern DYNAMIC_CAST = Pattern.compile(
        "\\b(\\w+) =\\s*(?:\\([^()]*\\))?\\s*__dynamic_cast\\([^,]+,\\s*&([\\w:<>, *]+?)::typeinfo,\\s*&([\\w:<>, *]+?)::typeinfo");
    private static final Pattern NEW_OBJECT = Pattern.compile("\\b(\\w+) =\\s*(?:\\([^()]*\\))?\\s*operator_new\\((?:0x)?[0-9a-f]+\\);");

    // Trusted: reached from "this" or a parameter, whose types come from the exe's names rather than Ghidra's guesses.
    private record Location(String className, long offset, boolean trusted) {}

    private record FieldFact(String className, long offset, String what, String value, int size, boolean trusted, String evidence) {}

    private record LocalFact(String name, String type) {}

    private static class FunctionFacts {
        Function function;
        List<FieldFact> fields = new ArrayList<>();
        List<JsonObject> locals = new ArrayList<>();
        // Locals that live only in the decompiler's view (no stack slot or register of their own): typed right here.
        List<String[]> decompilerLocals = new ArrayList<>();
        Map<String, Long> reach = new HashMap<>();
        String returns;
        // Returns a class by value, through an address passed before "this"; the class, when the code shows it.
        boolean byValue;
        String resultClass;
    }

    private static class Votes {
        Map<String, Integer> names = new HashMap<>();
        Map<String, Integer> types = new HashMap<>();
        Map<String, String> evidence = new HashMap<>();
        int size;
    }

    private static class ClassInfo {
        String name;
        int size = -1;
        boolean hasVtable;
        List<String[]> bases = new ArrayList<>();
        Set<Long> named = new HashSet<>();
    }

    private final Map<String, ClassInfo> classes = new HashMap<>();
    private final Map<String, String> classOfStructure = new HashMap<>();
    private final Map<String, String> classOfShortName = new HashMap<>();
    private final Map<String, Integer> structureSize = new HashMap<>();
    private final Map<String, Long> reachedSize = new HashMap<>();
    private final Map<String, String> spelledByGhidra = new HashMap<>();
    private final Map<String, Structure> structureOf = new HashMap<>();
    private final Map<String, String> sourceOfFunction = new HashMap<>();
    private final Map<Long, String> pathOfFunction = new HashMap<>();
    private final Set<Address> failedAsserts = new HashSet<>();
    private String onlyPathsWith;

    @Override
    public void run() throws Exception {
        String[] arguments = getScriptArgs();
        File programFile = arguments.length > 0 ? new File(arguments[0]) : askFile("program.json from mt2sdk program", "Use");
        File output = arguments.length > 1 ? new File(arguments[1]) : askFile("inferred.json to write", "Write");
        onlyPathsWith = arguments.length > 2 ? arguments[2] : null;

        readProgram(JsonParser.parseString(Files.readString(programFile.toPath(), StandardCharsets.UTF_8)).getAsJsonObject());
        mapStructures();
        findFailedAsserts();

        List<Function> chosen = choose();
        println("MT2: reading " + chosen.size() + " functions for field names and types");

        List<FunctionFacts> results = decompile(chosen);
        JsonObject inferred = gather(results);
        int committed = commitDecompilerLocals(results);

        println("MT2: " + committed + " locals typed in the decompiler's own variables");

        Files.writeString(output.toPath(), new GsonBuilder().setPrettyPrinting().create().toJson(inferred), StandardCharsets.UTF_8);
    }

    private void readProgram(JsonObject program) {
        JsonArray files = program.getAsJsonArray("files");

        for (JsonElement element : program.getAsJsonArray("functions")) {
            JsonArray pair = element.getAsJsonArray();
            JsonObject file = files.get(pair.get(1).getAsInt()).getAsJsonObject();
            String path = file.get("path").getAsString();

            pathOfFunction.put(pair.get(0).getAsLong(), path.isEmpty() ? "" : path + "|" + file.get("name").getAsString());
        }

        for (JsonElement element : program.getAsJsonArray("classes")) {
            JsonObject item = element.getAsJsonObject();
            ClassInfo info = new ClassInfo();
            info.name = item.get("name").getAsString();
            info.size = item.has("size") ? item.get("size").getAsInt() : -1;
            info.hasVtable = item.getAsJsonArray("vtable").size() > 0;

            for (JsonElement base : item.getAsJsonArray("bases")) {
                JsonObject baseItem = base.getAsJsonObject();
                info.bases.add(new String[] { baseItem.get("name").getAsString(), baseItem.get("offset").getAsString() });
            }

            for (JsonElement field : item.getAsJsonArray("fields")) {
                info.named.add(field.getAsJsonObject().get("offset").getAsLong());
            }

            classes.put(info.name, info);
            spelledByGhidra.put(info.name.replace(" ", "_"), info.name);
        }
    }

    // The structures Ghidra uses for classes, so a pointer's type says which class it points at. The demangler makes
    // its own placeholder structures for parameter types, found by their name.
    private void mapStructures() {
        DataTypeManager manager = currentProgram.getDataTypeManager();
        Map<String, Integer> shortNames = new HashMap<>();

        for (String name : classes.keySet()) {
            String shortName = name.contains("::") ? name.substring(name.lastIndexOf("::") + 2) : name;
            shortNames.merge(shortName, 1, Integer::sum);
            classOfShortName.put(shortName, name);

            Namespace namespace = findNamespace(name);

            if (namespace instanceof GhidraClass ghidraClass) {
                Structure structure = VariableUtilities.findExistingClassStruct(ghidraClass, manager);

                if (structure != null) {
                    classOfStructure.put(structure.getPathName(), name);
                    structureOf.put(name, structure);

                    if (!structure.isZeroLength()) {
                        structureSize.put(name, structure.getLength());
                    }
                }
            }
        }

        shortNames.forEach((shortName, count) -> {
            if (count > 1) {
                classOfShortName.remove(shortName);
            }
        });
    }

    private Namespace findNamespace(String name) {
        try {
            List<Namespace> found = NamespaceUtils.getNamespaceByPath(currentProgram, null, name);

            // Ghidra spells template arguments without spaces: vsArray<mmoProp_const*>.
            if (found.isEmpty() && name.contains(" ")) {
                found = NamespaceUtils.getNamespaceByPath(currentProgram, null, name.replace(" ", "_"));
            }

            return found.isEmpty() ? null : found.get(0);
        } catch (Exception problem) {
            return null;
        }
    }

    private void findFailedAsserts() {
        for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
            if (function.getName().equals("vsFailedAssert")) {
                failedAsserts.add(function.getEntryPoint());
            }
        }
    }

    private List<Function> choose() {
        List<Function> chosen = new ArrayList<>();

        for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
            String path = pathOfFunction.get(function.getEntryPoint().getOffset());

            if (function.isThunk() || function.isExternal() || path == null || path.isEmpty()) {
                continue;
            }

            if (onlyPathsWith == null || path.contains(onlyPathsWith)) {
                chosen.add(function);
            }
        }

        return chosen;
    }

    private List<FunctionFacts> decompile(List<Function> functions) throws Exception {
        DecompilerCallback<FunctionFacts> callback = new DecompilerCallback<>(currentProgram, new Configurer()) {
            @Override
            public FunctionFacts process(DecompileResults results, TaskMonitor monitor) {
                FunctionFacts facts = new FunctionFacts();
                facts.function = results.getFunction();

                if (results.decompileCompleted() && results.getHighFunction() != null) {
                    try {
                        read(results.getHighFunction(), results.getDecompiledFunction().getC(), facts);
                    } catch (RuntimeException problem) {
                        facts.fields.clear();
                        facts.locals.clear();
                    }
                }

                return facts;
            }
        };
        callback.setTimeout(TIMEOUT_SECONDS);

        try {
            return ParallelDecompiler.decompileFunctions(callback, functions, monitor);
        } finally {
            callback.dispose();
        }
    }

    private void read(HighFunction high, String code, FunctionFacts facts) {
        Function function = high.getFunction();
        List<PcodeOpAST> operations = new ArrayList<>();

        for (Iterator<PcodeOpAST> iterator = high.getPcodeOps(); iterator.hasNext();) {
            operations.add(iterator.next());
        }

        // A function that returns a class by value gets the address for it before "this" (GCC's way): Ghidra takes
        // that address for "this", so what it says about "this" is about something else.
        int expected = registersFromName(function);
        Varnode result = expected >= 0 ? firstArgument(operations) : null;

        String resultClass = result != null && !isStructor(function) && writesThrough(operations, result)
            && readsRegisterAfterParameters(operations, expected) ? classOfResult(operations, result) : "";

        // A constructor writes its vtable into "this" and runs its bases' constructors on it, which looks the same.
        if (!"".equals(resultClass) && (resultClass == null || !isSelfOrBase(ownerOf(function), resultClass))) {
            facts.byValue = true;
            facts.resultClass = resultClass;
            readLocals(high, operations, code, facts);

            return;
        }

        for (PcodeOpAST operation : operations) {
            switch (operation.getOpcode()) {
                case PcodeOp.LOAD -> readLoad(function, operation, facts);
                case PcodeOp.STORE -> readStore(function, operation, facts);
                case PcodeOp.CALL -> readCall(function, operation, facts);
                default -> { }
            }
        }

        readAccessor(function, operations, facts);
        readLocals(high, operations, code, facts);
        readReturn(high, facts);
    }

    // How many argument registers the function takes by its mangled name: its parameters, and "this" for a method.
    // -1 when the name doesn't say (no mangled name, or a variable argument list).
    private int registersFromName(Function function) {
        for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(function.getEntryPoint())) {
            String name = symbol.getName().replaceAll("\\.(part|isra|constprop|cold)\\.?\\d*.*$", "");

            if (!name.startsWith("_Z")) {
                continue;
            }

            try {
                List<ghidra.app.util.demangler.DemangledObject> found =
                    ghidra.app.util.demangler.DemanglerUtil.demangle(currentProgram, name, function.getEntryPoint());

                if (found == null || found.isEmpty() || !(found.get(0) instanceof ghidra.app.util.demangler.DemangledFunction demangled)) {
                    return -1;
                }

                int count = 0;

                for (ghidra.app.util.demangler.DemangledParameter parameter : demangled.getParameters()) {
                    String type = parameter.getType().toString();

                    if (type.equals("...")) {
                        return -1;
                    }

                    count += type.equals("void") ? 0 : 1;
                }

                boolean isMethod = classes.containsKey(ownerOf(function) == null ? "" : ownerOf(function));

                return count + (isMethod ? 1 : 0);
            } catch (Exception problem) {
                return -1;
            }
        }

        return -1;
    }

    private boolean isStructor(Function function) {
        String owner = ownerOf(function);
        String name = function.getName();

        return owner != null && (name.startsWith("~") || owner.equals(name) || owner.endsWith("::" + name)
            || owner.startsWith(name + "<") || owner.contains("::" + name + "<"));
    }

    private boolean isSelfOrBase(String className, String candidate) {
        if (className == null) {
            return false;
        }

        if (className.equals(candidate) || realBase(className).equals(candidate)) {
            return true;
        }

        ClassInfo info = classes.get(className);

        if (info == null) {
            return false;
        }

        for (String[] base : info.bases) {
            if (isSelfOrBase(base[0], candidate) || isSelfOrBase(realBase(base[0]), candidate)) {
                return true;
            }
        }

        return false;
    }

    // The value the first argument register (RCX) came in with, whether Ghidra made it a parameter or not.
    private Varnode firstArgument(List<PcodeOpAST> operations) {
        long first = currentProgram.getRegister(ARGUMENT_REGISTERS[0]).getAddress().getOffset();

        for (PcodeOp operation : operations) {
            for (Varnode input : operation.getInputs()) {
                if (input != null && input.isInput() && input.isRegister() && input.getAddress().getOffset() == first && input.getSize() == 8) {
                    return input;
                }
            }
        }

        return null;
    }

    private boolean writesThrough(List<PcodeOpAST> operations, Varnode pointer) {
        for (PcodeOp operation : operations) {
            if (operation.getOpcode() != PcodeOp.STORE) {
                continue;
            }

            Varnode address = operation.getInput(1);

            for (int step = 0; step < 6 && address != null; step++) {
                if (address.equals(pointer)) {
                    return true;
                }

                PcodeOp definition = address.getDef();
                address = definition != null && (definition.getOpcode() == PcodeOp.CAST || definition.getOpcode() == PcodeOp.COPY
                    || definition.getOpcode() == PcodeOp.PTRSUB || definition.getOpcode() == PcodeOp.INT_ADD
                    || definition.getOpcode() == PcodeOp.PTRADD) ? definition.getInput(0) : null;
            }
        }

        return false;
    }

    // A function returning a class by value takes the address for it in the first register, before "this": every
    // parameter moves one register along, so the code reads a register past the ones its mangled name gives it.
    private boolean readsRegisterAfterParameters(List<PcodeOpAST> operations, int parameterCount) {
        if (parameterCount < 0 || parameterCount >= ARGUMENT_REGISTERS.length) {
            return false;
        }

        long next = currentProgram.getRegister(ARGUMENT_REGISTERS[parameterCount]).getAddress().getOffset();

        for (PcodeOp operation : operations) {
            for (Varnode input : operation.getInputs()) {
                if (input != null && input.isInput() && input.isRegister() && input.getAddress().getOffset() == next) {
                    return true;
                }
            }
        }

        return false;
    }

    // What class the result is: a vtable written at its start, a constructor run on it, or a method called on it.
    private String classOfResult(List<PcodeOpAST> operations, Varnode result) {
        for (PcodeOp operation : operations) {
            if (operation.getOpcode() == PcodeOp.STORE && throughCasts(operation.getInput(1)).equals(result)) {
                Long constant = constantAddress(operation.getInput(2));
                String vtableClass = constant != null ? vtableClassAt(constant) : null;

                if (vtableClass != null) {
                    return vtableClass;
                }
            }

            Function called = operation.getOpcode() == PcodeOp.CALL ? calledFunction(operation) : null;

            if (called == null || operation.getNumInputs() < 2 || !throughCasts(operation.getInput(1)).equals(result)) {
                continue;
            }

            String owner = ownerOf(called);
            Parameter[] parameters = called.getParameters();

            if (owner != null && (owner.endsWith(called.getName()) || parameters.length > 0 && parameters[0].getName().equals("this"))) {
                return owner;
            }
        }

        return null;
    }

    // Where an address points: which class's object, and how far into it. this->field_0x10 is PTRSUB(this, 0x10).
    private Location locate(Function function, Varnode address) {
        long offset = 0;
        Varnode current = address;

        for (int step = 0; step < 10; step++) {
            PcodeOp definition = current.getDef();

            if (definition == null) {
                break;
            }

            int opcode = definition.getOpcode();

            if (opcode == PcodeOp.CAST || opcode == PcodeOp.COPY) {
                current = definition.getInput(0);
            } else if ((opcode == PcodeOp.PTRSUB || opcode == PcodeOp.INT_ADD) && definition.getInput(1).isConstant()) {
                offset += definition.getInput(1).getOffset();
                current = definition.getInput(0);
            } else if (opcode == PcodeOp.PTRADD && definition.getInput(1).isConstant() && definition.getInput(2).isConstant()) {
                offset += definition.getInput(1).getOffset() * definition.getInput(2).getOffset();
                current = definition.getInput(0);
            } else {
                break;
            }
        }

        String className = isThis(current) ? ownerOf(function) : classOfPointer(current.getHigh());
        HighSymbol symbol = current.getHigh() != null ? current.getHigh().getSymbol() : null;
        boolean trusted = current.isInput() && symbol != null && symbol.isParameter();

        return className == null || offset < 0 || offset > 0x100000 ? null : new Location(className, offset, trusted);
    }

    private boolean isThis(Varnode varnode) {
        HighVariable high = varnode.getHigh();
        HighSymbol symbol = high != null ? high.getSymbol() : null;

        // "self" is "this" in a function returning a class by value, once MT2Apply has fixed its parameters.
        return varnode.isInput() && symbol != null && symbol.isParameter() && (symbol.getName().equals("this") || symbol.getName().equals("self"));
    }

    private String ownerOf(Function function) {
        Namespace namespace = function.getParentNamespace();

        return namespace == null || namespace.isGlobal() ? null : canonical(namespace.getName(true));
    }

    // Ghidra spells template arguments without spaces (vsArray<mmoProp_const*>); the exe's names have them.
    private String canonical(String ghidraName) {
        return classes.containsKey(ghidraName) ? ghidraName : spelledByGhidra.getOrDefault(ghidraName, ghidraName);
    }

    private String classOfPointer(HighVariable high) {
        if (high == null) {
            return null;
        }

        DataType type = unwrap(high.getDataType());

        return type instanceof Pointer pointer ? classOf(unwrap(pointer.getDataType())) : null;
    }

    private DataType unwrap(DataType type) {
        return type instanceof TypeDef typeDef ? typeDef.getBaseDataType() : type;
    }

    private String classOf(DataType type) {
        if (!(type instanceof Structure)) {
            return null;
        }

        String known = classOfStructure.get(type.getPathName());

        return known != null ? known : classOfShortName.get(type.getName());
    }

    // What a value's type is, spelled as the mappings do: vsScene*, vsBox3D, mmoNPC::State, float.
    private String spell(DataType given) {
        DataType type = unwrap(given);

        if (type instanceof Pointer pointer) {
            DataType target = unwrap(pointer.getDataType());
            String className = classOf(target);

            if (className != null) {
                return className + "*";
            }

            String primitive = target != null ? primitive(target) : null;

            return primitive != null && !primitive.equals("bool") ? primitive + "*" : null;
        }

        if (type instanceof Structure) {
            return classOf(type);
        }

        if (type instanceof ghidra.program.model.data.Enum && type.getCategoryPath().getPath().startsWith("/MT2")) {
            String owner = type.getCategoryPath().getPath().substring("/MT2".length()).replace("/", "::");

            return (owner.startsWith("::") ? owner.substring(2) + "::" : "") + type.getName();
        }

        return primitive(type);
    }

    private String primitive(DataType type) {
        return switch (type.getName()) {
            case "bool" -> "bool";
            case "float" -> "float";
            case "double" -> "double";
            case "int" -> "int";
            case "uint" -> "unsigned int";
            case "short" -> "short";
            case "ushort" -> "unsigned short";
            case "longlong" -> "long long";
            case "ulonglong" -> "unsigned long long";
            case "char" -> "char";
            default -> null;
        };
    }

    private void addType(FunctionFacts facts, Location location, String type, int accessSize, String evidence) {
        if (type == null || location.offset() == 0 && hasVtable(location.className())) {
            return;
        }

        if (accessSize > 0 && sizeOf(type) > 0 && sizeOf(type) != accessSize) {
            return;
        }

        facts.fields.add(new FieldFact(location.className(), location.offset(), "type", type, accessSize, location.trusted(), evidence));
    }

    private boolean hasVtable(String className) {
        ClassInfo info = classes.get(className);

        return info != null && info.hasVtable;
    }

    private int sizeOf(String type) {
        if (type.endsWith("*")) {
            return 8;
        }

        return switch (type) {
            case "bool", "char" -> 1;
            case "short", "unsigned short" -> 2;
            case "float", "int", "unsigned int" -> 4;
            case "double", "long long", "unsigned long long" -> 8;
            default -> -1;
        };
    }

    private void reach(FunctionFacts facts, Location location, int size) {
        if (location.className().equals(ownerOf(facts.function))) {
            facts.reach.merge(location.className(), location.offset() + size, Math::max);
        }
    }

    // A field read, then passed on: its type is what the called function expects there.
    private void readLoad(Function function, PcodeOp load, FunctionFacts facts) {
        Location location = locate(function, load.getInput(1));

        if (location == null || load.getOutput() == null) {
            return;
        }

        reach(facts, location, load.getOutput().getSize());

        for (PcodeOp use : usesThroughCasts(load.getOutput())) {
            if (use.getOpcode() != PcodeOp.CALL) {
                continue;
            }

            Function called = calledFunction(use);

            for (int slot = 1; called != null && slot < use.getNumInputs(); slot++) {
                if (reaches(use.getInput(slot), load.getOutput())) {
                    addType(facts, location, parameterType(called, slot - 1), load.getOutput().getSize(),
                        "passed to " + called.getName(true));
                }
            }
        }
    }

    private List<PcodeOp> usesThroughCasts(Varnode value) {
        List<PcodeOp> uses = new ArrayList<>();
        List<Varnode> pending = new ArrayList<>(List.of(value));

        for (int index = 0; index < pending.size() && index < 16; index++) {
            for (Iterator<PcodeOp> iterator = pending.get(index).getDescendants(); iterator.hasNext();) {
                PcodeOp use = iterator.next();

                if ((use.getOpcode() == PcodeOp.CAST || use.getOpcode() == PcodeOp.COPY) && use.getOutput() != null) {
                    pending.add(use.getOutput());
                } else {
                    uses.add(use);
                }
            }
        }

        return uses;
    }

    private boolean reaches(Varnode argument, Varnode value) {
        Varnode current = argument;

        for (int step = 0; step < 6 && current != null; step++) {
            if (current.equals(value)) {
                return true;
            }

            PcodeOp definition = current.getDef();
            current = definition != null && (definition.getOpcode() == PcodeOp.CAST || definition.getOpcode() == PcodeOp.COPY)
                ? definition.getInput(0) : null;
        }

        return false;
    }

    private Function calledFunction(PcodeOp call) {
        Varnode target = call.getInput(0);

        return target.isAddress() ? currentProgram.getFunctionManager().getFunctionAt(target.getAddress()) : null;
    }

    private String parameterType(Function function, int index) {
        Parameter[] parameters = function.getParameters();

        return index < parameters.length ? spell(parameters[index].getDataType()) : null;
    }

    // A field written: a vtable written there makes it an object of that class; a value a function returned, or a
    // parameter, has that function's type.
    private void readStore(Function function, PcodeOp store, FunctionFacts facts) {
        Location location = locate(function, store.getInput(1));
        Varnode value = store.getInput(2);

        if (location == null) {
            return;
        }

        reach(facts, location, value.getSize());

        Varnode source = throughCasts(value);
        Long constant = constantAddress(value);
        String vtableClass = constant != null ? vtableClassAt(constant) : null;

        if (vtableClass != null) {
            if (location.offset() > 0 && !isBaseAt(location.className(), location.offset())) {
                addType(facts, location, vtableClass, 0, "its vtable is written by " + function.getName(true));
            }

            return;
        }

        PcodeOp definition = source.getDef();
        Function called = definition != null && definition.getOpcode() == PcodeOp.CALL ? calledFunction(definition) : null;

        if (called != null) {
            addType(facts, location, spell(called.getReturnType()), value.getSize(), "returned by " + called.getName(true));
        }

        HighVariable high = source.getHigh();
        HighSymbol symbol = high != null ? high.getSymbol() : null;

        if (source.isInput() && symbol != null && symbol.isParameter() && !symbol.getName().equals("this")) {
            addType(facts, location, spell(symbol.getDataType()), value.getSize(), "a parameter of " + function.getName(true));
        }
    }

    private Varnode throughCasts(Varnode value) {
        Varnode current = value;

        for (int step = 0; step < 6; step++) {
            PcodeOp definition = current.getDef();

            if (definition == null || (definition.getOpcode() != PcodeOp.CAST && definition.getOpcode() != PcodeOp.COPY)) {
                break;
            }

            current = definition.getInput(0);
        }

        return current;
    }

    // GCC writes a vtable pointer 16 bytes into "vtable for X"; its first slot is X's destructor.
    private String vtableClassAt(long offset) {
        try {
            Address address = toAddr(offset);

            if (currentProgram.getMemory().getBlock(address) == null || currentProgram.getMemory().getBlock(address).isExecute()) {
                return null;
            }

            for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(address.subtract(16))) {
                if (symbol.getName().equals("vtable") && !symbol.getParentNamespace().isGlobal()) {
                    return canonical(symbol.getParentNamespace().getName(true));
                }
            }

            Function first = currentProgram.getFunctionManager().getFunctionAt(toAddr(currentProgram.getMemory().getLong(address)));

            return first != null && first.getName().startsWith("~") ? ownerOf(first) : null;
        } catch (Exception problem) {
            return null;
        }
    }

    private boolean isBaseAt(String className, long offset) {
        ClassInfo info = classes.get(className);

        if (info == null) {
            return false;
        }

        for (String[] base : info.bases) {
            long baseOffset = Long.parseLong(base[1]);

            if (baseOffset == offset || offset > baseOffset && isBaseAt(base[0], offset - baseOffset)) {
                return true;
            }
        }

        return false;
    }

    // A field's address passed to a function: the field is what that function takes a pointer to. A call on
    // this->field_0x18 as a vsArray<mmoProp*>'s "this" makes the field a vsArray<mmoProp*>.
    private void readCall(Function function, PcodeOp call, FunctionFacts facts) {
        Function called = calledFunction(call);

        if (called == null) {
            return;
        }

        if (failedAsserts.contains(called.getEntryPoint())) {
            readAssert(function, call, facts);

            return;
        }

        for (int slot = 1; slot < call.getNumInputs(); slot++) {
            Location location = locate(function, call.getInput(slot));
            String type = parameterType(called, slot - 1);

            if (location == null || location.offset() == 0 || type == null || !type.endsWith("*")) {
                continue;
            }

            String pointed = type.substring(0, type.length() - 1);

            if (!isBaseAt(location.className(), location.offset()) && classes.containsKey(pointed)) {
                addType(facts, location, pointed, 0, "its address is passed to " + called.getName(true));
            }
        }
    }

    // vsFailedAssert("m_scene", "Trying to build ...", file, line) runs when the condition before it fails. When that
    // condition reads one field and the assert's text names one member, that's the field's name. Only asserts written
    // in the function's own source file count: an inlined vsArray's assert names the vsArray's members.
    private void readAssert(Function function, PcodeOp call, FunctionFacts facts) {
        if (call.getNumInputs() < 4) {
            return;
        }

        String condition = readText(call.getInput(1));
        String file = readText(call.getInput(3));
        String ownPath = pathOfFunction.get(function.getEntryPoint().getOffset());

        if (condition == null || file == null || ownPath == null || !sameSourceFile(file, ownPath)) {
            return;
        }

        Set<String> members = new HashSet<>();
        Matcher member = MEMBER.matcher(condition);

        while (member.find()) {
            members.add(member.group());
        }

        if (members.size() != 1 || condition.contains("->") || condition.contains("(")) {
            return;
        }

        Map<Location, Integer> read = new HashMap<>();

        for (Varnode test : guardingConditions(call)) {
            collectFieldReads(function, test, read, 0);
        }

        read.keySet().removeIf(location -> !location.className().equals(ownerOf(function)));

        if (read.size() == 1) {
            Location location = read.keySet().iterator().next();
            facts.fields.add(new FieldFact(location.className(), location.offset(), "name", members.iterator().next(),
                read.get(location), true, "an assert in " + function.getName(true)));
        }
    }

    // The assert's path is "../code/Games/MMORPG/MapObjects/MMO_Blueprint.cpp"; the function's is "path|name".
    private boolean sameSourceFile(String assertPath, String ownPath) {
        String name = ownPath.substring(ownPath.indexOf('|') + 1);
        String normalized = assertPath.replace('\\', '/');

        return normalized.endsWith("/" + name) || normalized.equals(name);
    }

    private List<Varnode> guardingConditions(PcodeOp call) {
        List<Varnode> conditions = new ArrayList<>();
        PcodeBlockBasic block = call.getParent();

        for (int index = 0; index < block.getInSize(); index++) {
            PcodeBlock before = block.getIn(index);

            if (before instanceof PcodeBlockBasic basic) {
                PcodeOp last = lastOperation(basic);

                if (last != null && last.getOpcode() == PcodeOp.CBRANCH) {
                    conditions.add(last.getInput(1));
                }
            }
        }

        return conditions;
    }

    private PcodeOp lastOperation(PcodeBlockBasic block) {
        PcodeOp last = null;

        for (Iterator<PcodeOp> iterator = block.getIterator(); iterator.hasNext();) {
            last = iterator.next();
        }

        return last;
    }

    private void collectFieldReads(Function function, Varnode value, Map<Location, Integer> read, int depth) {
        PcodeOp definition = value.getDef();

        if (definition == null || depth > 6) {
            return;
        }

        if (definition.getOpcode() == PcodeOp.LOAD) {
            Location location = locate(function, definition.getInput(1));

            if (location != null) {
                read.put(location, definition.getOutput().getSize());
            }

            return;
        }

        if (definition.getOpcode() == PcodeOp.CALL || definition.getOpcode() == PcodeOp.MULTIEQUAL) {
            return;
        }

        for (int index = 0; index < definition.getNumInputs(); index++) {
            collectFieldReads(function, definition.getInput(index), read, depth + 1);
        }
    }

    // An address written in the code: a constant, or PTRSUB(0, address) as the decompiler puts a global's address.
    private Long constantAddress(Varnode value) {
        Varnode source = throughCasts(value);
        PcodeOp definition = source.getDef();

        if (source.isConstant() || source.isAddress()) {
            return source.getOffset();
        }

        if (definition != null && definition.getOpcode() == PcodeOp.PTRSUB && definition.getInput(0).isConstant()
            && definition.getInput(1).isConstant()) {
            return definition.getInput(0).getOffset() + definition.getInput(1).getOffset();
        }

        return null;
    }

    private String readText(Varnode argument) {
        Long offset = constantAddress(argument);

        if (offset == null) {
            return null;
        }

        try {
            Address address = toAddr(offset);
            StringBuilder text = new StringBuilder();

            for (int index = 0; index < 400; index++) {
                byte character = currentProgram.getMemory().getByte(address.add(index));

                if (character == 0) {
                    return text.toString();
                }

                text.append((char) (character & 0xff));
            }
        } catch (Exception problem) {
            return null;
        }

        return null;
    }

    // GetScene() that only returns this->field_0x10 names it m_scene; SetScene(scene) that only stores its parameter
    // there does too. A getter returning the field's address (a reference) names an object held there.
    private void readAccessor(Function function, List<PcodeOpAST> operations, FunctionFacts facts) {
        Matcher getter = GETTER.matcher(function.getName());
        Matcher setter = SETTER.matcher(function.getName());
        boolean isGetter = getter.matches();

        if (!isGetter && !setter.matches() || ownerOf(function) == null) {
            return;
        }

        int work = 0;
        PcodeOp only = null;

        for (PcodeOp operation : operations) {
            int opcode = operation.getOpcode();

            if (opcode == PcodeOp.CALL || opcode == PcodeOp.CALLIND || opcode == PcodeOp.CBRANCH) {
                return;
            }

            if (opcode == PcodeOp.LOAD || opcode == PcodeOp.STORE) {
                work++;
                only = operation;
            }
        }

        String member = "m_" + lowerFirst((isGetter ? getter : setter).group(1));

        if (isGetter) {
            readGetter(function, operations, work, only, member, facts);
        } else if (work == 1 && only.getOpcode() == PcodeOp.STORE) {
            Location location = locate(function, only.getInput(1));
            Varnode value = throughCasts(only.getInput(2));
            HighSymbol symbol = value.getHigh() != null ? value.getHigh().getSymbol() : null;

            if (location != null && location.className().equals(ownerOf(function)) && value.isInput() && symbol != null
                && symbol.isParameter() && !symbol.getName().equals("this")) {
                facts.fields.add(new FieldFact(location.className(), location.offset(), "name", member, only.getInput(2).getSize(), true,
                    function.getName() + "()"));
            }
        }
    }

    private void readGetter(Function function, List<PcodeOpAST> operations, int work, PcodeOp only, String member,
        FunctionFacts facts) {
        for (PcodeOp operation : operations) {
            if (operation.getOpcode() != PcodeOp.RETURN || operation.getNumInputs() < 2) {
                continue;
            }

            Varnode returned = throughCasts(operation.getInput(1));
            PcodeOp definition = returned.getDef();
            Location location = null;

            if (work == 1 && only.getOpcode() == PcodeOp.LOAD && definition == only) {
                location = locate(function, only.getInput(1));
            } else if (work == 0) {
                location = locate(function, returned);
            }

            if (location != null && location.className().equals(ownerOf(function))
                && (location.offset() > 0 || !hasVtable(location.className()))) {
                int size = work == 1 ? only.getOutput().getSize() : 0;
                facts.fields.add(new FieldFact(location.className(), location.offset(), "name", member, size, true, function.getName() + "()"));
            }

            return;
        }
    }

    private String lowerFirst(String text) {
        return text.isEmpty() ? text : Character.toLowerCase(text.charAt(0)) + text.substring(1);
    }

    // Locals by what's done with them, read from the decompiled text, then found in the function's own variables.
    private void readLocals(HighFunction high, List<PcodeOpAST> operations, String code, FunctionFacts facts) {
        Map<String, String> types = new LinkedHashMap<>();
        Set<String> constructed = new HashSet<>();

        // A local's address passed where a function takes a vsBox3D* (as its "this" or otherwise): it's a vsBox3D.
        // One a constructor runs on is surely that class.
        for (PcodeOp operation : operations) {
            Function called = operation.getOpcode() == PcodeOp.CALL ? calledFunction(operation) : null;

            for (int slot = 1; called != null && slot < operation.getNumInputs(); slot++) {
                String type = parameterType(called, slot - 1);
                String name = type != null && type.endsWith("*") ? stackLocalAt(high, operation.getInput(slot)) : null;
                String pointed = name != null ? type.substring(0, type.length() - 1) : null;

                if (pointed == null || !GHIDRA_LOCAL.matcher(name).matches() || !structureSize.containsKey(pointed)) {
                    continue;
                }

                if (slot == 1 && isStructor(called) && pointed.equals(ownerOf(called)) && !called.getName().startsWith("~")) {
                    types.put(name, pointed);
                    constructed.add(name);
                } else if (!constructed.contains(name)) {
                    types.putIfAbsent(name, pointed);
                }
            }
        }

        Matcher cast = DYNAMIC_CAST.matcher(code);

        while (cast.find()) {
            if (classes.containsKey(cast.group(3))) {
                types.put(cast.group(1), cast.group(3) + "*");
            }
        }

        Matcher created = NEW_OBJECT.matcher(code);

        while (created.find()) {
            Matcher constructor = Pattern.compile("\\b([\\w:<>, ]+?)::(\\w+)\\(\\s*(?:\\([^()]*\\))?\\s*" + created.group(1) + "\\s*[,)]")
                .matcher(code.substring(created.end(), Math.min(code.length(), created.end() + 600)));

            if (constructor.find() && constructor.group(1).endsWith(constructor.group(2)) && classes.containsKey(constructor.group(1))) {
                types.put(created.group(1), constructor.group(1) + "*");
            }
        }

        for (Map.Entry<String, String> local : types.entrySet()) {
            JsonObject item = describeLocal(high, local.getKey(), local.getValue());

            if (item != null && constructed.contains(local.getKey())) {
                item.addProperty("constructed", true);
            }

            if (item != null && item.has("decompilerOnly")) {
                facts.decompilerLocals.add(new String[] { local.getKey(), local.getValue() });
            } else if (item != null) {
                facts.locals.add(item);
            }
        }
    }

    // &local_268 is the stack pointer plus the local's offset.
    private String stackLocalAt(HighFunction high, Varnode address) {
        Varnode current = throughCasts(address);
        PcodeOp definition = current.getDef();

        if (definition == null || definition.getOpcode() != PcodeOp.PTRSUB && definition.getOpcode() != PcodeOp.INT_ADD
            || !definition.getInput(1).isConstant()) {
            return null;
        }

        Varnode base = definition.getInput(0);
        boolean isStackPointer = base.isRegister()
            && base.getAddress().equals(currentProgram.getCompilerSpec().getStackPointer().getAddress());

        if (!isStackPointer) {
            return null;
        }

        int stackOffset = (int) definition.getInput(1).getOffset();

        for (Iterator<HighSymbol> iterator = high.getLocalSymbolMap().getSymbols(); iterator.hasNext();) {
            HighSymbol symbol = iterator.next();

            if (!symbol.isParameter() && symbol.getStorage().isStackStorage() && symbol.getStorage().getStackOffset() == stackOffset) {
                return symbol.getName();
            }
        }

        return null;
    }

    private JsonObject describeLocal(HighFunction high, String name, String type) {
        for (Iterator<HighSymbol> iterator = high.getLocalSymbolMap().getSymbols(); iterator.hasNext();) {
            HighSymbol symbol = iterator.next();

            if (!symbol.getName().equals(name) || symbol.isParameter()) {
                continue;
            }

            DataType current = unwrap(symbol.getDataType());

            // A local that already has another class stays as it is; one with this class (from an earlier round) is
            // listed again, since MT2Apply retypes each round's locals afresh.
            String already = spell(current);
            boolean hasClass = classOf(current) != null || current instanceof Pointer pointer && classOf(unwrap(pointer.getDataType())) != null;

            if (hasClass && !type.equals(already)) {
                return null;
            }

            JsonObject item = new JsonObject();
            item.addProperty("function", "0x" + high.getFunction().getEntryPoint());
            item.addProperty("name", name);
            item.addProperty("type", type);

            if (symbol.getStorage().isStackStorage()) {
                item.addProperty("stack", symbol.getStorage().getStackOffset());
            } else if (symbol.getStorage().isRegisterStorage() && symbol.getPCAddress() != null) {
                item.addProperty("register", symbol.getStorage().getRegister().getName());
                item.addProperty("firstUse", symbol.getPCAddress().subtract(high.getFunction().getEntryPoint()));
            } else {
                item.addProperty("decompilerOnly", true);
            }

            return item;
        }

        return null;
    }

    private void readReturn(HighFunction high, FunctionFacts facts) {
        DataType current = unwrap(high.getFunction().getReturnType());

        // A return type set on a function whose parameters Ghidra only guessed would lock that guess in.
        if (high.getFunction().getSignatureSource() == ghidra.program.model.symbol.SourceType.DEFAULT) {
            return;
        }

        if (current instanceof Pointer pointer && classOf(unwrap(pointer.getDataType())) != null) {
            return;
        }

        String returns = spell(high.getFunctionPrototype().getReturnType());

        if (returns != null && returns.endsWith("*") && classes.containsKey(returns.substring(0, returns.length() - 1))) {
            facts.returns = returns;
        }
    }

    // A variable the decompiler made up (what __dynamic_cast returns, kept in no register of its own) can only be typed
    // through the decompiler: decompiled again, one function at a time, and committed.
    private int commitDecompilerLocals(List<FunctionFacts> results) {
        // Set up like the parallel pass, so the decompiler names the variables the same way.
        DecompInterface decompiler = new DecompInterface();
        new Configurer().configure(decompiler);
        decompiler.openProgram(currentProgram);
        int committed = 0;

        try {
            for (FunctionFacts facts : results) {
                if (facts.decompilerLocals.isEmpty()) {
                    continue;
                }

                DecompileResults decompiled = decompiler.decompileFunction(facts.function, TIMEOUT_SECONDS, monitor);
                HighFunction high = decompiled.getHighFunction();

                for (String[] local : high != null ? facts.decompilerLocals : List.<String[]>of()) {
                    committed += commitLocal(high, local[0], local[1]) ? 1 : 0;
                }
            }
        } finally {
            decompiler.dispose();
        }

        return committed;
    }

    private boolean commitLocal(HighFunction high, String name, String type) {
        boolean pointer = type.endsWith("*");
        Structure structure = structureOf.get(pointer ? type.substring(0, type.length() - 1) : type);

        if (structure == null) {
            return false;
        }

        DataType dataType = pointer ? new PointerDataType(structure, currentProgram.getDataTypeManager()) : structure;

        for (Iterator<HighSymbol> iterator = high.getLocalSymbolMap().getSymbols(); iterator.hasNext();) {
            HighSymbol symbol = iterator.next();

            if (symbol.getName().equals(name) && !symbol.isParameter()) {
                try {
                    ghidra.program.model.pcode.HighFunctionDBUtil.updateDBVariable(symbol, null, dataType,
                        ghidra.program.model.symbol.SourceType.ANALYSIS);

                    return true;
                } catch (Exception problem) {
                    return false;
                }
            }
        }

        return false;
    }

    // Everything the functions said, voted on: a field's name and type are the ones most of the code agrees on, moved
    // to the base class the offset lies in.
    private JsonObject gather(List<FunctionFacts> results) {
        Map<String, TreeMap<Long, Votes>> votes = new TreeMap<>();
        Map<String, Long> reach = new HashMap<>();
        JsonArray locals = new JsonArray();
        JsonArray returns = new JsonArray();
        JsonArray byValue = new JsonArray();

        for (FunctionFacts facts : results) {
            facts.reach.forEach((className, end) -> reach.merge(className, end, Math::max));
        }

        reachedSize.putAll(reach);

        for (FunctionFacts facts : results) {
            Set<String> counted = new HashSet<>();

            for (FieldFact fact : facts.fields) {
                Location location = owningClass(new Location(fact.className(), fact.offset(), fact.trusted()));
                ClassInfo info = classes.get(location.className());

                long size = sizeOfClass(location.className());

                if (info != null && info.named.contains(location.offset()) || size > 0 && location.offset() >= size) {
                    continue;
                }

                // Each function has one say per fact; one reached through a type Ghidra guessed has half a say.
                if (!counted.add(location.className() + "|" + location.offset() + "|" + fact.what() + "|" + fact.value())) {
                    continue;
                }

                Votes vote = votes.computeIfAbsent(location.className(), key -> new TreeMap<>())
                    .computeIfAbsent(location.offset(), key -> new Votes());
                Map<String, Integer> tally = fact.what().equals("name") ? vote.names : vote.types;

                tally.merge(fact.value(), fact.trusted() ? 2 : 1, Integer::sum);
                vote.size = Math.max(vote.size, fact.size());
                vote.evidence.putIfAbsent(fact.what() + fact.value(), fact.evidence());
            }

            facts.locals.forEach(locals::add);

            if (facts.byValue) {
                JsonObject item = new JsonObject();
                item.addProperty("function", "0x" + facts.function.getEntryPoint());

                if (facts.resultClass != null) {
                    item.addProperty("class", facts.resultClass);
                }

                byValue.add(item);
            }

            if (facts.returns != null) {
                JsonObject item = new JsonObject();
                item.addProperty("function", "0x" + facts.function.getEntryPoint());
                item.addProperty("returns", facts.returns);
                returns.add(item);
            }
        }

        JsonArray classList = new JsonArray();
        int names = 0;
        int types = 0;

        for (String className : new java.util.TreeSet<>(union(votes.keySet(), reach.keySet()))) {
            JsonObject item = new JsonObject();
            JsonArray fields = new JsonArray();
            Map<String, Long> nameAt = new HashMap<>();
            TreeMap<Long, Votes> classVotes = votes.getOrDefault(className, new TreeMap<>());

            item.addProperty("name", className);

            if (classes.containsKey(className) && classes.get(className).size <= 0 && reach.containsKey(className)) {
                item.addProperty("sizeAtLeast", reach.get(className));
            }

            // A name the code gives two offsets is kept for the one with more votes.
            for (Map.Entry<Long, Votes> entry : classVotes.entrySet()) {
                String name = winner(entry.getValue().names);

                if (name != null && (!nameAt.containsKey(name)
                    || entry.getValue().names.get(name) > classVotes.get(nameAt.get(name)).names.get(name))) {
                    nameAt.put(name, entry.getKey());
                }
            }

            TreeMap<Long, Long> objects = embeddedObjects(classVotes, reach);

            for (Map.Entry<Long, Votes> entry : classVotes.entrySet()) {
                Votes vote = entry.getValue();
                String name = winner(vote.names);
                String type = winner(vote.types);

                if (name != null && nameAt.get(name) != entry.getKey().longValue()) {
                    name = null;
                }

                // Inside an object held in this one (a vsArray's count, read by an inlined ItemCount()), a fact is
                // about that object's own field.
                Map.Entry<Long, Long> holder = objects.lowerEntry(entry.getKey());

                if (name == null && type == null || holder != null && entry.getKey() < holder.getKey() + holder.getValue()) {
                    continue;
                }

                JsonObject field = new JsonObject();
                field.addProperty("offset", entry.getKey());

                if (name != null) {
                    field.addProperty("name", name);
                    field.addProperty("nameFrom", vote.evidence.get("name" + name));
                    names++;
                }

                if (type == null && vote.size > 0) {
                    field.addProperty("size", vote.size);
                }

                if (type != null) {
                    field.addProperty("type", type);
                    field.addProperty("typeFrom", vote.evidence.get("type" + type));
                    types++;
                }

                fields.add(field);
            }

            item.add("fields", fields);

            if (fields.size() > 0 || item.has("sizeAtLeast")) {
                classList.add(item);
            }
        }

        println("MT2: " + names + " field names, " + types + " field types, " + locals.size() + " locals and "
            + returns.size() + " return types worked out; " + byValue.size() + " functions return a class by value");

        JsonObject inferred = new JsonObject();
        inferred.add("classes", classList);
        inferred.add("locals", locals);
        inferred.add("returns", returns);
        inferred.add("byValue", byValue);

        return inferred;
    }

    // A class's size: from the code that makes one, else the furthest its own methods reach. A std::string is 32 bytes.
    private long sizeOfClass(String className) {
        ClassInfo info = classes.get(className);

        if (className.equals("std::string") || className.startsWith("std::__cxx11::basic_string<char")) {
            return 32;
        }

        return info != null && info.size > 0 ? info.size : reachedSize.getOrDefault(className, 0L);
    }

    // Where objects held by value sit, and how big they are: offset to size.
    private TreeMap<Long, Long> embeddedObjects(TreeMap<Long, Votes> classVotes, Map<String, Long> reach) {
        TreeMap<Long, Long> objects = new TreeMap<>();

        for (Map.Entry<Long, Votes> entry : classVotes.entrySet()) {
            String type = winner(entry.getValue().types);
            long size = type != null ? sizeOfClass(type) : 0;

            if (size > 0) {
                objects.put(entry.getKey(), size);
            }
        }

        return objects;
    }

    private Set<String> union(Set<String> first, Set<String> second) {
        Set<String> all = new HashSet<>(first);
        all.addAll(second);

        return all;
    }

    // Most of the votes, and clearly more than any other answer.
    private String winner(Map<String, Integer> tally) {
        String best = null;
        int bestCount = 0;
        int total = 0;

        for (Map.Entry<String, Integer> entry : tally.entrySet()) {
            total += entry.getValue();

            if (entry.getValue() > bestCount) {
                best = entry.getKey();
                bestCount = entry.getValue();
            }
        }

        return best != null && bestCount >= 2 && bestCount * 3 >= total * 2 ? best : null;
    }

    // An offset inside a base class (whose size is known) belongs to that base: mmoNPC's 0x170 is mmoCharacter's.
    private Location owningClass(Location location) {
        ClassInfo info = classes.get(location.className());

        if (info == null) {
            return location;
        }

        for (String[] base : info.bases) {
            String baseName = realBase(base[0]);
            long baseSize = sizeOfClass(baseName);
            long baseOffset = Long.parseLong(base[1]);

            if (baseSize > 0 && location.offset() >= baseOffset && location.offset() < baseOffset + baseSize) {
                return owningClass(new Location(baseName, location.offset() - baseOffset, location.trusted()));
            }
        }

        return location;
    }

    // vsObject<mmoNPC, mmoCharacter> stands for mmoCharacter.
    private String realBase(String name) {
        if (!name.startsWith("vsObject<") && !name.startsWith("vsAbstractObject<")) {
            return name;
        }

        int comma = name.lastIndexOf(',');

        return comma < 0 ? name : name.substring(comma + 1, name.length() - 1).trim();
    }

    private static class Configurer implements DecompileConfigurer {
        @Override
        public void configure(DecompInterface decompiler) {
            decompiler.setOptions(new DecompileOptions());
            decompiler.toggleCCode(true);
            decompiler.toggleSyntaxTree(true);
            decompiler.setSimplificationStyle("decompile");
        }
    }
}
