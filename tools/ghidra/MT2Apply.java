// Teaches Ghidra what's known about MT2.exe: the game's own facts from program.json (mt2sdk program: each class's
// fields by their real names, its base classes, size and enums) and, on top, mt2-mappings from mappings.json (mt2sdk
// mappings json). Each class's structure gets its fields and its bases' fields, so the decompiler writes
// this->weaponIlvl; enums become Ghidra enums, so it writes Combat instead of 1; functions get return types,
// parameter names and their notes as comments. Under both go the facts MT2Infer worked out from the code (inferred.json):
// fields no one named, locals' types and functions' return types.
// Arguments: program.json, then mappings.json or "-", then inferred.json (both optional).
// @category MT2

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.app.util.demangler.DemangledFunction;
import ghidra.app.util.demangler.DemangledObject;
import ghidra.app.util.demangler.DemangledParameter;
import ghidra.app.util.demangler.DemanglerUtil;
import ghidra.program.model.data.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.listing.GhidraClass;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.LocalVariableImpl;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.ReturnParameterImpl;
import ghidra.program.model.listing.Variable;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.listing.VariableUtilities;
import ghidra.program.model.symbol.FlowType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.StackReference;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class MT2Apply extends GhidraScript {

    private static final CategoryPath MT2_TYPES = new CategoryPath("/MT2");
    private static final String[] INTEGER_REGISTERS = { "RCX", "RDX", "R8", "R9" };
    private static final String INFERRED_LOCAL = "Typed by MT2Infer";
    private static final String[] FLOAT_REGISTERS = { "XMM0", "XMM1", "XMM2", "XMM3" };
    private static final String[] VOLATILE_REGISTERS = { "RAX", "RCX", "RDX", "R8", "R9", "R10", "R11" };
    private static final Pattern ARRAY = Pattern.compile("(.+)\\[(\\d+)\\]");

    private static class FieldModel {
        int offset;
        String name;
        String type;
        String comment = "";
        boolean inferred;
        int size;
    }

    private static class BaseModel {
        String name;
        int offset;
    }

    private static class ClassModel {
        String name;
        int size = -1;
        boolean hasVtable;
        // A size read from how far the class's own code reaches: known fields past it make it bigger.
        boolean sizeIsLowerBound;
        String description = "";
        List<FieldModel> fields = new ArrayList<>();
        List<BaseModel> bases = new ArrayList<>();
    }

    private final Map<String, ClassModel> models = new HashMap<>();
    private final Map<String, Structure> structures = new HashMap<>();
    private final Map<String, DataType> enums = new HashMap<>();
    private final Set<String> building = new HashSet<>();
    private final Set<String> built = new HashSet<>();
    private final List<String> notes = new ArrayList<>();
    private int fieldsPlaced;
    private int functionsDescribed;
    private int localsTyped;
    private int returnsTyped;
    private int byValueFixed;
    private int signaturesFixed;
    private final List<Long> gameFunctions = new ArrayList<>();
    // Functions that had "result, self" from an earlier run: their parameters come from their names again, whatever they hold now.
    private final Set<Long> unfixed = new HashSet<>();

    @Override
    public void run() throws Exception {
        String[] arguments = getScriptArgs();
        File programFile = arguments.length > 0 ? new File(arguments[0]) : askFile("program.json from mt2sdk program", "Apply");
        File mappingsFile = arguments.length > 1 && !arguments[1].equals("-") ? new File(arguments[1]) : null;
        File inferredFile = arguments.length > 2 && new File(arguments[2]).isFile() ? new File(arguments[2]) : null;
        JsonObject program = readJson(programFile);
        JsonObject mappings = mappingsFile != null ? readJson(mappingsFile) : null;
        JsonObject inferred = inferredFile != null ? readJson(inferredFile) : null;

        readProgram(program);
        readGameFunctions(program);

        if (mappings != null) {
            readMappings(mappings);
        }

        if (inferred != null) {
            readInferred(inferred);
        }

        makeEnums(program.getAsJsonArray("enums"));

        for (String name : new ArrayList<>(models.keySet())) {
            if (isWorthAStructure(models.get(name))) {
                build(name);
            }
        }

        if (mappings != null) {
            describeFunctions(mappings);
        }

        fixDynamicCast();

        JsonArray byValue = inferred != null ? returningFirstArgument(inferred.getAsJsonArray("byValue")) : null;

        if (byValue != null) {
            addUnlistedByValue(byValue);
        }

        if (inferred != null) {
            unfixByValue(byValue);
        }

        fixSignatures();
        typeContainerAccessors();

        if (inferred != null) {
            fixByValue(byValue);
            typeReturns(inferred.getAsJsonArray("returns"));
            typeLocals(inferred.getAsJsonArray("locals"));
        }

        println("MT2: " + built.size() + " classes with " + fieldsPlaced + " fields, " + enums.size() + " enums, "
            + functionsDescribed + " functions described, " + localsTyped + " locals, " + returnsTyped + " return types and " + byValueFixed
            + " functions returning a class by value from the code; " + signaturesFixed + " functions' parameters from their names");

        for (String note : notes.subList(0, Math.min(notes.size(), 40))) {
            println("  " + note);
        }

        if (notes.size() > 40) {
            println("  ... and " + (notes.size() - 40) + " more notes");
        }
    }

    private JsonObject readJson(File file) throws Exception {
        return JsonParser.parseString(Files.readString(file.toPath(), StandardCharsets.UTF_8)).getAsJsonObject();
    }

    // std::__cxx11::string and std::string are one class (GCC's new-ABI namespace), built once with what both say.
    private ClassModel model(String name) {
        return models.computeIfAbsent(name.replace("std::__cxx11::", "std::"), key -> {
            ClassModel created = new ClassModel();
            created.name = key;
            return created;
        });
    }

    private void readProgram(JsonObject program) {
        for (JsonElement element : program.getAsJsonArray("classes")) {
            JsonObject item = element.getAsJsonObject();
            ClassModel model = model(item.get("name").getAsString());

            if (item.has("size")) {
                model.size = item.get("size").getAsInt();
            }

            model.hasVtable = item.getAsJsonArray("vtable").size() > 0;

            for (JsonElement baseElement : item.getAsJsonArray("bases")) {
                JsonObject baseItem = baseElement.getAsJsonObject();
                BaseModel base = new BaseModel();
                base.name = baseItem.get("name").getAsString();
                base.offset = baseItem.get("offset").getAsInt();
                model.bases.add(base);
            }

            for (JsonElement fieldElement : item.getAsJsonArray("fields")) {
                JsonObject fieldItem = fieldElement.getAsJsonObject();
                FieldModel field = new FieldModel();
                field.offset = fieldItem.get("offset").getAsInt();
                field.name = fieldItem.get("name").getAsString();
                field.type = fieldItem.get("type").getAsString();
                field.comment = "Saved and loaded by this name";
                model.fields.add(field);
            }
        }
    }

    // A mapped field replaces the game's at the same offset (it has the same name when the game names it too, and a
    // doc); a mapped size wins over one read from the code.
    private void readMappings(JsonObject mappings) {
        for (JsonElement element : mappings.getAsJsonArray("classes")) {
            JsonObject item = element.getAsJsonObject();
            ClassModel model = model(item.get("name").getAsString());

            if (item.has("size")) {
                model.size = item.get("size").getAsInt();
            }

            model.description = joined(item, "doc", " ");

            for (JsonElement fieldElement : item.getAsJsonArray("fields")) {
                JsonObject fieldItem = fieldElement.getAsJsonObject();
                FieldModel field = new FieldModel();
                field.offset = fieldItem.get("offset").getAsInt();
                field.name = fieldItem.get("name").getAsString();
                field.type = fieldItem.get("type").getAsString();
                field.comment = comment(fieldItem);
                model.fields.removeIf(existing -> existing.offset == field.offset);
                model.fields.add(field);
            }
        }
    }

    // What MT2Infer read from the code fills only what the game and the mappings leave open. A field known only by its
    // type is named after it (a vsScene* is scene); one known only by its name gets room for what the code reads.
    private void readInferred(JsonObject inferred) {
        for (JsonElement element : inferred.getAsJsonArray("classes")) {
            JsonObject item = element.getAsJsonObject();
            ClassModel model = model(item.get("name").getAsString());

            if (model.size <= 0 && item.has("sizeAtLeast")) {
                model.size = (item.get("sizeAtLeast").getAsInt() + 3) & ~3;
                model.sizeIsLowerBound = true;
            }

            for (JsonElement fieldElement : item.getAsJsonArray("fields")) {
                JsonObject fieldItem = fieldElement.getAsJsonObject();
                int offset = fieldItem.get("offset").getAsInt();

                if (model.fields.stream().anyMatch(existing -> existing.offset == offset)) {
                    continue;
                }

                FieldModel field = new FieldModel();
                field.offset = offset;
                field.type = fieldItem.has("type") ? fieldItem.get("type").getAsString() : "";
                field.name = fieldItem.has("name") ? fieldItem.get("name").getAsString() : nameForType(field.type);
                field.size = fieldItem.has("size") ? fieldItem.get("size").getAsInt() : 0;
                field.inferred = true;
                field.comment = "From the code: " + (fieldItem.has("nameFrom") ? "name from " + fieldItem.get("nameFrom").getAsString() : "")
                    + (fieldItem.has("nameFrom") && fieldItem.has("typeFrom") ? "; " : "")
                    + (fieldItem.has("typeFrom") ? "type from " + fieldItem.get("typeFrom").getAsString() : "");
                model.fields.add(field);
            }
        }
    }

    // vsScene* is scene, vsArray<vsModelInstance*> modelInstances; plain numbers keep Ghidra's field_0x4c.
    private String nameForType(String type) {
        String base = type.replace("*", "").trim();
        boolean list = base.startsWith("vsArray<") || base.startsWith("vsArrayStore<") || base.startsWith("vsLinkedList<");

        if (list) {
            base = base.substring(base.indexOf('<') + 1, base.lastIndexOf('>')).replace("*", "").trim();
        }

        base = base.replaceAll("<.*>", "");
        base = base.contains("::") ? base.substring(base.lastIndexOf("::") + 2) : base;

        if (!base.matches("[A-Za-z_]\\w*") || primitive(base) != null || base.matches("unsigned.*|long.*")) {
            return null;
        }

        String stripped = base.replaceFirst("^(?:mmo|vs|MMO|VS)(?=[A-Z])", "");
        String name = Character.toLowerCase(stripped.charAt(0)) + stripped.substring(1);

        return list ? plural(name) : name;
    }

    // checkbox is checkboxes, category categories.
    static String plural(String name) {
        if (name.matches(".*(?:s|x|z|ch|sh)")) {
            return name + "es";
        }

        return name.matches(".*[^aeiou]y") ? name.substring(0, name.length() - 1) + "ies" : name + "s";
    }

    private boolean isWorthAStructure(ClassModel model) {
        return !model.fields.isEmpty() || model.size > 0 || model.hasVtable || !model.bases.isEmpty();
    }

    private void makeEnums(JsonArray list) {
        DataTypeManager manager = currentProgram.getDataTypeManager();

        for (JsonElement element : list) {
            JsonObject item = element.getAsJsonObject();
            String full = item.get("name").getAsString();
            int split = full.lastIndexOf("::");
            CategoryPath category = split < 0 ? MT2_TYPES : new CategoryPath(MT2_TYPES, full.substring(0, split).split("::"));
            EnumDataType created = new EnumDataType(category, split < 0 ? full : full.substring(split + 2), 4, manager);
            JsonArray values = item.getAsJsonArray("values");

            for (int value = 0; value < values.size(); value++) {
                String word = values.get(value).getAsString();

                if (!word.isEmpty() && !created.contains(word)) {
                    created.add(word, value);
                }
            }

            enums.put(full, manager.addDataType(created, DataTypeConflictHandler.REPLACE_HANDLER));
        }

        replaceDemangledEnums();
    }

    // The demangler meets an enum only as a name in a signature, so it makes a one-byte placeholder for it
    // (/Demangler/mmoPartyMember/State, a typedef of undefined): a parameter of that type gets no register, and the
    // body reads a made-up param_2, or a field0_0x0. Every use of the placeholder becomes the real enum; an enum the
    // game doesn't reflect (mmoGame::GameMode) becomes one without words, 4 bytes like every enum the game has.
    private void replaceDemangledEnums() {
        DataTypeManager manager = currentProgram.getDataTypeManager();
        List<DataType> placeholders = new ArrayList<>();
        Iterator<DataType> all = manager.getAllDataTypes();

        while (all.hasNext()) {
            DataType type = all.next();

            DataType base = type instanceof TypeDef typedef ? typedef.getBaseDataType() : null;
            boolean unknown = base == DataType.DEFAULT || base instanceof Undefined;

            if (unknown && type.getLength() == 1 && type.getCategoryPath().getPath().startsWith("/Demangler") && !isLibraryPlaceholder(type)) {
                placeholders.add(type);
            }
        }

        for (DataType placeholder : placeholders) {
            String folder = placeholder.getCategoryPath().getPath().substring("/Demangler".length());
            String full = (folder.isEmpty() ? "" : folder.substring(1).replace("/", "::") + "::") + placeholder.getName();

            if (models.containsKey(full)) {
                continue;
            }

            DataType replacement = enums.get(full);

            if (replacement == null) {
                CategoryPath category = folder.isEmpty() ? MT2_TYPES : new CategoryPath(MT2_TYPES, folder.substring(1).split("/"));
                replacement = manager.addDataType(new EnumDataType(category, placeholder.getName(), 4, manager), DataTypeConflictHandler.KEEP_HANDLER);
            }

            try {
                manager.replaceDataType(placeholder, replacement, false);
            } catch (DataTypeDependencyException problem) {
                notes.add(full + ": the demangler's placeholder couldn't be replaced: " + problem.getMessage());
            }
        }
    }

    // The C++ library's placeholders are its tags, functors and pairs, not enums.
    private boolean isLibraryPlaceholder(DataType type) {
        String path = type.getPathName();

        return path.startsWith("/Demangler/std/") || path.startsWith("/Demangler/__gnu_cxx/") || path.contains("<")
            || type.getName().equals("nullptr");
    }

    // The structure Ghidra uses for a class's "this" (so the decompiler shows its fields by name), or one in /MT2.
    private Structure structureFor(String name) throws Exception {
        if (structures.containsKey(name)) {
            return structures.get(name);
        }

        Structure structure = name.startsWith("std::") ? demangledStructure(name) : null;
        Namespace namespace = structure != null || name.contains("<T") ? null : findNamespace(name);

        if (namespace != null) {
            GhidraClass ghidraClass = namespace instanceof GhidraClass known ? known : NamespaceUtils.convertNamespaceToClass(namespace);
            structure = VariableUtilities.findOrCreateClassStruct(ghidraClass, currentProgram.getDataTypeManager());
        }

        if (structure == null) {
            structure = demangledStructure(name);
        }

        if (structure == null) {
            String typeName = name.replaceAll("[^A-Za-z0-9_]", "_");
            DataType existing = currentProgram.getDataTypeManager().getDataType(MT2_TYPES, typeName);
            structure = existing instanceof Structure known ? known
                : (Structure) currentProgram.getDataTypeManager().addDataType(new StructureDataType(MT2_TYPES, typeName, 0), null);
        }

        structures.put(name, structure);

        return structure;
    }

    // A type Ghidra made from the mangled names, with no functions of its own to make it a class: std::string is the
    // structure "string" in /Demangler/std/__cxx11, and that's the one the decompiler shows.
    private Structure demangledStructure(String name) {
        List<String> spellings = new ArrayList<>();

        // Where both exist, the decompiler uses the __cxx11 one.
        if (name.startsWith("std::")) {
            spellings.add("std::__cxx11::" + name.substring(5));
        }

        spellings.add(name);

        for (String spelling : spellings) {
            int last = spelling.lastIndexOf("::");
            String folder = "/Demangler" + (last < 0 ? "" : "/" + spelling.substring(0, last).replace("::", "/"));
            DataType found = currentProgram.getDataTypeManager().getDataType(new CategoryPath(folder), spelling.substring(last + 2));

            if (found instanceof Structure structure) {
                return structure;
            }
        }

        return null;
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

    // The class's own fields, then each base's (and theirs), moved to where that base sits in the object.
    private void collectFields(ClassModel model, int shift, List<FieldModel> into, Set<String> visited) {
        if (!visited.add(model.name)) {
            return;
        }

        for (FieldModel field : model.fields) {
            FieldModel moved = new FieldModel();
            moved.offset = field.offset + shift;
            moved.name = field.name;
            moved.type = field.type;
            moved.comment = shift == 0 && field.comment.isEmpty() ? "" : field.comment;
            moved.inferred = field.inferred;
            moved.size = field.size;
            into.add(moved);
        }

        // vsArray<mmoProp*> has what's known about every vsArray (vsArray<T> in the mappings).
        String template = model.name.contains("<") && !model.name.endsWith("<T>") ? model.name.substring(0, model.name.indexOf('<')) + "<T>" : null;

        if (template != null && models.containsKey(template)) {
            collectFields(models.get(template), shift, into, visited);
        }

        for (BaseModel base : model.bases) {
            ClassModel baseModel = models.get(templateOrSelf(base.name));

            if (baseModel != null) {
                collectFields(baseModel, shift + base.offset, into, visited);
            }

            // vsObject<mmoNPC, mmoCharacter> and vsAbstractObject<...> are the engine's way of saying "built on
            // mmoCharacter": the real base is the second template argument.
            String realBase = secondTemplateArgument(base.name);
            ClassModel realModel = realBase != null ? models.get(realBase) : null;

            if (realModel != null) {
                collectFields(realModel, shift + base.offset, into, visited);
            }
        }
    }

    private String templateOrSelf(String name) {
        if (models.containsKey(name) || !name.contains("<")) {
            return name;
        }

        return name.substring(0, name.indexOf('<')) + "<T>";
    }

    private String secondTemplateArgument(String name) {
        if (!name.startsWith("vsObject<") && !name.startsWith("vsAbstractObject<")) {
            return null;
        }

        int depth = 0;
        int comma = -1;

        for (int index = name.indexOf('<') + 1; index < name.length(); index++) {
            char character = name.charAt(index);

            if (character == '<') {
                depth++;
            } else if (character == '>') {
                if (depth == 0) {
                    return comma < 0 ? null : name.substring(comma + 1, index).trim();
                }

                depth--;
            } else if (character == ',' && depth == 0) {
                comma = index;
            }
        }

        return null;
    }

    private Structure build(String name) throws Exception {
        ClassModel model = models.get(name);
        Structure structure = structureFor(name);

        if (model == null || built.contains(name) || !building.add(name)) {
            return structure;
        }

        List<FieldModel> fields = new ArrayList<>();
        collectFields(model, 0, fields, new HashSet<>());
        fields.sort(Comparator.comparingInt(field -> field.offset));
        fields.removeIf(field -> field.inferred && model.size > 0 && field.offset >= model.size);
        fields.removeIf(field -> field.inferred && model.hasVtable && field.offset < 8);

        // Built afresh each run, so a field the mappings or the code no longer give doesn't linger.
        if (!structure.isZeroLength()) {
            structure.deleteAll();
        }

        int end = 0;

        for (FieldModel field : fields) {
            end = Math.max(end, field.offset + 1);
        }

        growTo(structure, Math.max(Math.max(model.size, end), model.hasVtable ? 8 : 0));

        if (model.hasVtable && fields.stream().noneMatch(field -> field.offset == 0)) {
            // Without the program's data type manager a pointer is 4 bytes, and the vtable's upper half looks like a field.
            DataType vtable = new PointerDataType(VoidDataType.dataType, currentProgram.getDataTypeManager());
            place(structure, name, 0, "vtable", vtable, "The class's virtual functions (mt2sdk vtable)");
        }

        Set<String> usedNames = new HashSet<>();

        // The game's and the mappings' fields go in first; what was inferred from the code only fills the gaps.
        for (boolean inferredPass : new boolean[] { false, true }) {
            placeFields(name, model, structure, fields, inferredPass, usedNames);
        }

        if (!model.description.isEmpty()) {
            structure.setDescription(model.description);
        }

        building.remove(name);
        built.add(name);

        return structure;
    }

    private void placeFields(String name, ClassModel model, Structure structure, List<FieldModel> fields, boolean inferredPass,
        Set<String> usedNames) throws Exception {
        for (int index = 0; index < fields.size(); index++) {
            FieldModel field = fields.get(index);

            if (field.inferred != inferredPass) {
                continue;
            }

            DataType type = field.type.isEmpty() ? null : resolve(field.type);

            // A field the code reads with a known width, whose type isn't known.
            if (type == null && field.inferred && field.size > 0) {
                type = Undefined.getUndefinedDataType(field.size);
            }

            // A value whose size isn't known takes the room up to the next field: its name then covers every access.
            if (type == null || type.getLength() <= 0) {
                int next = index + 1 < fields.size() ? fields.get(index + 1).offset : Math.max(model.size, field.offset + 1);
                int room = Math.max(1, next - field.offset);

                type = new ArrayDataType(Undefined1DataType.dataType, room, 1);
                field.comment = (field.comment.isEmpty() ? "" : field.comment + ". ")
                    + (field.type.isEmpty() ? "Its type" : field.type) + " (size not known yet)";
            }

            // A field known only by a plain type keeps Ghidra's own name (field_0x4c).
            String fieldName = field.name == null || usedNames.add(field.name) ? field.name : field.name + "_" + Integer.toHexString(field.offset);

            if (model.size > 0 && field.offset + type.getLength() > model.size && (field.inferred || !model.sizeIsLowerBound)) {
                notes.add(name + "::" + fieldName + " doesn't fit in the class's 0x" + Integer.toHexString(model.size) + " bytes");
                continue;
            }

            growTo(structure, field.offset + type.getLength());
            place(structure, name, field.offset, fieldName, type, field.comment);
        }
    }

    private void place(Structure structure, String className, int offset, String name, DataType type, String comment) {
        // A field already placed over any of these bytes (the game's, a base's, or the mappings') stays.
        for (int at = offset; at < offset + type.getLength(); at++) {
            DataTypeComponent existing = structure.getComponentContaining(at);

            if (existing != null && existing.getDataType() != DataType.DEFAULT && !(existing.getDataType() instanceof Undefined)) {
                return;
            }
        }

        try {
            structure.replaceAtOffset(offset, type, type.getLength(), name, comment);
            fieldsPlaced++;
        } catch (IllegalArgumentException problem) {
            notes.add(className + "::" + name + ": " + problem.getMessage());
        }
    }

    private void growTo(Structure structure, int size) {
        int current = structure.isZeroLength() ? 0 : structure.getLength();

        if (size > current) {
            structure.growStructure(size - current);
        }
    }

    // C++ types as the game and the mappings write them: int, char[16], mmoShard*, std::string, mmoNPC::State,
    // vsWeakObjectLink<mmoNPC>, vsColor const&.
    private DataType resolve(String text) throws Exception {
        String type = text.replace("const", "").replace("&", "*").trim();
        int pointers = 0;

        while (type.endsWith("*")) {
            pointers++;
            type = type.substring(0, type.length() - 1).trim();
        }

        DataType base = pointers > 0 ? resolveForPointer(type) : resolveValue(type);

        if (base == null) {
            return null;
        }

        for (int level = 0; level < pointers; level++) {
            base = new PointerDataType(base, currentProgram.getDataTypeManager());
        }

        return base;
    }

    // What a pointer points at needn't be complete: its structure is enough, built or not.
    private DataType resolveForPointer(String type) throws Exception {
        DataType primitive = primitive(type);

        if (primitive != null) {
            return primitive;
        }

        if (enums.containsKey(type)) {
            return enums.get(type);
        }

        String known = templateOrSelf(type);

        return models.containsKey(known) || findNamespace(type) != null ? structureFor(known) : Undefined1DataType.dataType;
    }

    private DataType resolveValue(String type) throws Exception {
        Matcher array = ARRAY.matcher(type);

        if (array.matches()) {
            DataType element = resolveValue(array.group(1).trim());

            return element == null ? null : new ArrayDataType(element, Integer.parseInt(array.group(2)), element.getLength());
        }

        DataType primitive = primitive(type);

        if (primitive != null) {
            return primitive;
        }

        if (enums.containsKey(type)) {
            return enums.get(type);
        }

        String known = templateOrSelf(type);

        if (!models.containsKey(known) || building.contains(known)) {
            return null;
        }

        Structure structure = build(known);

        return structure.isZeroLength() ? null : structure;
    }

    private DataType primitive(String type) {
        switch (type) {
            case "void": return VoidDataType.dataType;
            case "bool": return BooleanDataType.dataType;
            case "char": return CharDataType.dataType;
            case "unsigned char": return UnsignedCharDataType.dataType;
            case "short": return ShortDataType.dataType;
            case "unsigned short": return UnsignedShortDataType.dataType;
            case "int": return IntegerDataType.dataType;
            case "unsigned int": return UnsignedIntegerDataType.dataType;
            case "long": return IntegerDataType.dataType;
            case "long long": return LongLongDataType.dataType;
            case "unsigned long long": return UnsignedLongLongDataType.dataType;
            case "float": return FloatDataType.dataType;
            case "double": return DoubleDataType.dataType;
            default: return null;
        }
    }

    // The game's and engine's own functions (those with a source file).
    private void readGameFunctions(JsonObject program) {
        JsonArray files = program.getAsJsonArray("files");

        for (JsonElement element : program.getAsJsonArray("functions")) {
            JsonArray pair = element.getAsJsonArray();

            if (!files.get(pair.get(1).getAsInt()).getAsJsonObject().get("path").getAsString().isEmpty()) {
                gameFunctions.add(pair.get(0).getAsLong());
            }
        }
    }

    // A function an earlier run took for one returning a class by value, that this run's MT2Infer no longer lists, gets
    // its parameters from its name again (fixSignatures does that for any function without "result" first).
    private void unfixByValue(JsonArray list) throws Exception {
        Set<Long> listed = new HashSet<>();

        for (JsonElement element : list) {
            listed.add(toAddr(element.getAsJsonObject().get("function").getAsString()).getOffset());
        }

        for (long entry : gameFunctions) {
            Function function = getFunctionAt(toAddr(entry));
            Parameter[] parameters = function != null ? function.getParameters() : new Parameter[0];

            if (parameters.length > 0 && parameters[0].getName().equals("result") && !listed.contains(entry)) {
                parameters[0].setName("param_1", SourceType.ANALYSIS);
                function.setSignatureSource(SourceType.DEFAULT);
                unfixed.add(entry);
            }
        }
    }

    // Windows x64 hands a class returned by value back by its address in RAX: the one it was given in RCX. A function
    // MT2Infer lists that never copies RCX into RAX only looked like one (a register read Ghidra made up).
    private JsonArray returningFirstArgument(JsonArray list) {
        JsonArray kept = new JsonArray();

        if (list == null) {
            return kept;
        }

        for (JsonElement element : list) {
            Function function = getFunctionAt(toAddr(element.getAsJsonObject().get("function").getAsString()));

            if (function != null && traceFirstArgument(function).returned()) {
                kept.add(element);
            }
        }

        if (kept.size() < list.size()) {
            println("MT2: " + (list.size() - kept.size()) + " functions MT2Infer took for returning a class by value don't return its address");
        }

        return kept;
    }

    // Methods returning a class by value that MT2Infer can't see (it counts only integer registers): their parameters
    // sit two registers past what their names give (the result's address, then the object), and they return RCX.
    // mmoDistrict::_MakeRectangle(float, float) reads XMM2 and XMM3 and builds an mmoCrossSection in RCX.
    private void addUnlistedByValue(JsonArray byValue) {
        Set<String> listed = new HashSet<>();
        int added = 0;

        for (JsonElement element : byValue) {
            listed.add(element.getAsJsonObject().get("function").getAsString());
        }

        for (long entry : gameFunctions) {
            Function function = getFunctionAt(toAddr(entry));
            String address = "0x" + Long.toHexString(entry);
            List<DataType> types = function != null && isMethod(function) && !listed.contains(address) ? mangledParameterTypes(function) : null;

            if (types == null || !readsRegisterPastName(function, types, 1)) {
                continue;
            }

            FirstArgument traced = traceFirstArgument(function);

            if (!traced.returned()) {
                continue;
            }

            JsonObject item = new JsonObject();
            item.addProperty("function", address);

            if (traced.classOfResult() != null) {
                item.addProperty("class", traced.classOfResult());
            }

            byValue.add(item);
            added++;
        }

        println("MT2: " + added + " more methods return a class by value (the code reads past their parameters and returns RCX)");
    }

    private record FirstArgument(boolean returned, String classOfResult) {}

    // Follows RCX through 64-bit copies (mov rbx, rcx ... mov rax, rbx, or through a stack slot: mov [rsp+0x350], rcx
    // ... mov rax, [rsp+0x350]) in address order, dropping a place once something else is written to it. Whether it
    // ends up in RAX, and the class of the first constructor or method called on it. A tail call (jmp to another
    // function) with RCX still holding it passes it on, and the function it jumps to returns it.
    private FirstArgument traceFirstArgument(Function function) {
        Register result = currentProgram.getRegister("RAX").getBaseRegister();
        Register first = currentProgram.getRegister("RCX").getBaseRegister();
        Set<Register> holders = new HashSet<>();
        Set<String> stackHolders = new HashSet<>();
        String classOfResult = null;

        holders.add(first);

        for (Instruction instruction : currentProgram.getListing().getInstructions(function.getBody(), true)) {
            FlowType flow = instruction.getFlowType();
            Address[] flows = instruction.getFlows();

            if (flow.isCall()) {
                Function called = flows.length == 1 ? getFunctionAt(flows[0]) : null;
                called = called != null && called.isThunk() ? called.getThunkedFunction(true) : called;

                if (classOfResult == null && called != null && holders.contains(first) && isMethod(called)) {
                    classOfResult = called.getParentNamespace().getName(true);
                }

                for (String volatileRegister : VOLATILE_REGISTERS) {
                    holders.remove(currentProgram.getRegister(volatileRegister).getBaseRegister());
                }

                continue;
            }

            boolean isMove = instruction.getMnemonicString().equalsIgnoreCase("MOV") && instruction.getNumOperands() == 2;
            Register destination = isMove ? instruction.getRegister(0) : null;
            Register source = isMove ? instruction.getRegister(1) : null;
            String stackDestination = isMove && destination == null ? stackSlot(instruction, 0) : null;
            String stackSource = isMove && source == null ? stackSlot(instruction, 1) : null;
            boolean sourceHolds = source != null && source.getBitLength() == 64 && holders.contains(source.getBaseRegister())
                || stackSource != null && stackHolders.contains(stackSource);
            boolean isCopy = sourceHolds && (destination != null && destination.getBitLength() == 64 || stackDestination != null);

            for (Object written : instruction.getResultObjects()) {
                if (written instanceof Register register && !isCopy) {
                    holders.remove(register.getBaseRegister());
                }
            }

            if (stackDestination != null && !isCopy) {
                stackHolders.remove(stackDestination);
            }

            if (isCopy && destination != null) {
                holders.add(destination.getBaseRegister());
            } else if (isCopy) {
                stackHolders.add(stackDestination);
            }

            boolean tailCall = flow.isJump() && !flow.isConditional() && flows.length == 1 && !function.getBody().contains(flows[0]);

            if (holders.contains(result) || tailCall && holders.contains(first)) {
                return new FirstArgument(true, classOfResult);
            }
        }

        return new FirstArgument(false, classOfResult);
    }

    // "qword ptr [RSP + 0x350]" for an 8-byte place on the stack, or null for anything else.
    private String stackSlot(Instruction instruction, int operand) {
        String text = instruction.getDefaultOperandRepresentation(operand);

        return text.startsWith("qword ptr [RSP") || text.startsWith("qword ptr [RBP") ? text : null;
    }

    // __dynamic_cast(object, &From::typeinfo, &To::typeinfo, hint), so casts read as such everywhere.
    private void fixDynamicCast() throws Exception {
        DataType pointer = new PointerDataType(VoidDataType.dataType, currentProgram.getDataTypeManager());

        for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
            if (!function.getName().equals("__dynamic_cast") || function.isThunk()) {
                continue;
            }

            List<Parameter> parameters = List.of(new ParameterImpl("object", pointer, currentProgram),
                new ParameterImpl("from", pointer, currentProgram), new ParameterImpl("to", pointer, currentProgram),
                new ParameterImpl("hint", LongLongDataType.dataType, currentProgram));

            function.updateFunction("__fastcall", new ReturnParameterImpl(pointer, currentProgram), parameters,
                FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED);
        }
    }

    // Ghidra's own analysis leaves some functions with parameters it guessed and locked (GetProps(void), with no
    // "this"); the mangled name says exactly what they take.
    private void fixSignatures() {
        for (long entry : gameFunctions) {
            Function function = getFunctionAt(toAddr(entry));
            Parameter[] current = function != null ? function.getParameters() : null;

            if (current == null || current.length > 0 && current[0].getName().equals("result")) {
                continue;
            }

            List<DataType> types = mangledParameterTypes(function);

            if (types == null) {
                continue;
            }

            // A mangled name doesn't say whether a method is static, except that only non-static ones can be const
            // (_ZNK...). Otherwise a function is fixed only when Ghidra gave it fewer parameters than its name has,
            // keeping whether Ghidra saw a "this".
            boolean isConst = mangledName(function).startsWith("_ZNK");
            boolean isMethod = isMethod(function)
                && (isConst || "__thiscall".equals(function.getCallingConventionName()) || readsRegisterPastName(function, types, 0));
            boolean matches = current.length == types.size() + (isMethod ? 1 : 0) && function.getSignatureSource() != SourceType.DEFAULT;
            boolean missing = current.length < types.size() + (isMethod ? 1 : 0) || unfixed.contains(entry);

            if (matches || !missing) {
                continue;
            }

            try {
                List<Parameter> parameters = new ArrayList<>();

                // For __thiscall, Ghidra takes the first parameter given for "this".
                if (isMethod) {
                    DataType self = new PointerDataType(structureFor(function.getParentNamespace().getName(true)), currentProgram.getDataTypeManager());
                    parameters.add(new ParameterImpl("this", self, currentProgram));
                }

                for (DataType type : types) {
                    parameters.add(new ParameterImpl("param_" + (parameters.size() + (isMethod ? 0 : 1)), passable(type), currentProgram));
                }

                function.updateFunction(isMethod ? "__thiscall" : "__fastcall", new ReturnParameterImpl(function.getReturnType(), currentProgram),
                    parameters, FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.IMPORTED);
                signaturesFixed++;
            } catch (Exception problem) {
                notes.add(function.getName(true) + ": its parameters couldn't be set from its name: " + problem.getMessage());
            }
        }
    }

    // A method's object comes in the first register, so its parameters sit one register later than its name alone
    // says. A function that reads the register after its last named parameter before writing it has a "this"; one
    // that reads "further" registers past it also takes the address for a class it returns by value. From the fifth
    // on, parameters are on the stack (Stack[0x28] is the fifth): mmoCrossSection::GenerateXZScaled(Type, float, float,
    // float) reads a fifth, so it has a "this".
    private boolean readsRegisterPastName(Function function, List<DataType> types, int further) {
        int position = types.size() + further;

        if (position >= INTEGER_REGISTERS.length) {
            return readsStackParameter(function, 8 + 8 * position);
        }

        DataType last = types.isEmpty() ? null : types.get(types.size() - 1);

        return readsBeforeWriting(function, registerFor(last, position));
    }

    private boolean readsStackParameter(Function function, int offset) {
        for (Instruction instruction : currentProgram.getListing().getInstructions(function.getBody(), true)) {
            for (Reference reference : instruction.getReferencesFrom()) {
                if (reference instanceof StackReference stack && stack.getStackOffset() == offset && reference.getReferenceType().isRead()) {
                    return true;
                }
            }
        }

        return false;
    }

    private Register registerFor(DataType type, int slot) {
        boolean isFloat = type instanceof FloatDataType || type instanceof DoubleDataType;

        return currentProgram.getRegister(isFloat ? FLOAT_REGISTERS[slot] : INTEGER_REGISTERS[slot]).getBaseRegister();
    }

    private boolean readsBeforeWriting(Function function, Register wanted) {
        Instruction instruction = getInstructionAt(function.getEntryPoint());

        for (int count = 0; instruction != null && count < 64; count++) {
            if (!clearsItself(instruction) && usesRegister(instruction.getInputObjects(), wanted)) {
                return true;
            }

            FlowType flow = instruction.getFlowType();

            if (usesRegister(instruction.getResultObjects(), wanted) || flow.isCall() || flow.isTerminal()) {
                return false;
            }

            Address next = flow.isJump() && !flow.isConditional() && instruction.getFlows().length == 1
                ? instruction.getFlows()[0] : instruction.getFallThrough();

            instruction = next != null && function.getBody().contains(next) ? getInstructionAt(next) : null;
        }

        return false;
    }

    // xor ecx, ecx reads ecx only on paper.
    private boolean clearsItself(Instruction instruction) {
        String mnemonic = instruction.getMnemonicString().toUpperCase();
        boolean clearing = mnemonic.equals("XOR") || mnemonic.equals("SUB") || mnemonic.equals("PXOR") || mnemonic.startsWith("XORP");

        return clearing && instruction.getNumOperands() == 2 && instruction.getRegister(0) != null
            && instruction.getRegister(0).equals(instruction.getRegister(1));
    }

    private boolean usesRegister(Object[] objects, Register wanted) {
        for (Object object : objects) {
            if (object instanceof Register register && register.getBaseRegister().equals(wanted)) {
                return true;
            }
        }

        return false;
    }

    private boolean isMethod(Function function) {
        Namespace namespace = function.getParentNamespace();

        return namespace != null && !namespace.isGlobal() && (models.containsKey(namespace.getName(true)) || namespace instanceof GhidraClass);
    }

    // A class passed by value goes by its address (Windows x64 passes anything but 1, 2, 4 or 8 bytes that way).
    private DataType passable(DataType type) {
        int length = type.getLength();

        return length == 1 || length == 2 || length == 4 || length == 8 ? type : new PointerDataType(type, currentProgram.getDataTypeManager());
    }

    // The function's mangled name, without GCC's suffixes for split and specialized copies; "" when it has none.
    private String mangledName(Function function) {
        for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(function.getEntryPoint())) {
            String name = symbol.getName().replaceAll("\\.(part|isra|constprop|cold)\\.?\\d*.*$", "");

            if (name.startsWith("_Z")) {
                return name;
            }
        }

        return "";
    }

    // The parameter types the mangled name gives, or null when there's no mangled name or it takes "...".
    private List<DataType> mangledParameterTypes(Function function) {
        String name = mangledName(function);

        if (!name.isEmpty()) {
            try {
                List<DemangledObject> found = DemanglerUtil.demangle(currentProgram, name, function.getEntryPoint());

                if (found == null || found.isEmpty() || !(found.get(0) instanceof DemangledFunction demangled)) {
                    return null;
                }

                List<DataType> types = new ArrayList<>();

                for (DemangledParameter parameter : demangled.getParameters()) {
                    String spelled = parameter.getType().toString();

                    if (spelled.equals("...")) {
                        return null;
                    }

                    if (!spelled.equals("void")) {
                        DataType type = parameter.getType().getDataType(currentProgram.getDataTypeManager());
                        types.add(type != null ? type : Undefined8DataType.dataType);
                    }
                }

                return types;
            } catch (Exception problem) {
                return null;
            }
        }

        return null;
    }

    // GCC passes the address for a class returned by value first, before "this"; Ghidra expects it after. Such a
    // function gets its parameters as GCC passes them: result, then self (the object), then the rest, one register
    // along. Callers then read result = object->Method(...), and the export turns self back into this.
    private void fixByValue(JsonArray list) throws Exception {
        if (list == null) {
            return;
        }

        for (JsonElement element : list) {
            JsonObject item = element.getAsJsonObject();
            Function function = getFunctionAt(toAddr(item.get("function").getAsString()));

            if (function == null) {
                continue;
            }

            DataType resultClass = item.has("class") ? resolveForPointer(item.get("class").getAsString()) : null;
            DataType resultType = new PointerDataType(resultClass != null ? resultClass : VoidDataType.dataType,
                currentProgram.getDataTypeManager());
            List<DataType> types = mangledParameterTypes(function);

            if (types == null) {
                continue;
            }

            try {
                List<Parameter> parameters = new ArrayList<>();
                parameters.add(new ParameterImpl("result", resultType, argumentStorage(0, resultType), currentProgram));

                if (isMethod(function)) {
                    DataType self = new PointerDataType(structureFor(function.getParentNamespace().getName(true)), currentProgram.getDataTypeManager());
                    parameters.add(new ParameterImpl("self", self, argumentStorage(1, self), currentProgram));
                }

                for (DataType given : types) {
                    DataType type = passable(given);
                    parameters.add(new ParameterImpl("param_" + (parameters.size() - 1), type, argumentStorage(parameters.size(), type), currentProgram));
                }

                ReturnParameterImpl returned = new ReturnParameterImpl(resultType,
                    new VariableStorage(currentProgram, currentProgram.getRegister("RAX")), currentProgram);

                function.updateFunction("__fastcall", returned, parameters, FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.ANALYSIS);
                byValueFixed++;
            } catch (Exception problem) {
                notes.add(function.getName(true) + " returns a class by value, but its parameters couldn't be fixed: " + problem.getMessage());
            }
        }
    }

    // Where Windows x64 passes argument number "position": the first four in registers (floats in XMM), then the stack.
    private VariableStorage argumentStorage(int position, DataType type) throws Exception {
        int size = Math.max(1, type.getLength());

        if (position >= INTEGER_REGISTERS.length) {
            return new VariableStorage(currentProgram, 0x28 + 8 * (position - INTEGER_REGISTERS.length), size);
        }

        boolean isFloat = type instanceof FloatDataType || type instanceof DoubleDataType;
        Register whole = currentProgram.getRegister(isFloat ? FLOAT_REGISTERS[position] : INTEGER_REGISTERS[position]);
        Address address = whole.getAddress();
        Register fitting = currentProgram.getLanguage().getRegister(address, size);

        return new VariableStorage(currentProgram, fitting != null ? fitting : whole);
    }

    // A function the decompiler sees return a class pointer says so in its signature, so its callers know it too.
    // The engine's containers hand back a reference to an item (VS_Array.h: T& GetItem(int); VS_ArrayStore.h: T*&
    // GetItem(int), const T* GetItemConst(int) const), which is its address in the machine code. Every copy's own T
    // says what it points at, so what's read through it is that class's field: part->boneName, not *(lVar1 + 0x18).
    private void typeContainerAccessors() throws Exception {
        Pattern accessor = Pattern.compile("^(vsArray|vsVolatileArray|vsArrayStore|vsVolatileArrayStore|vsObjectArray)<(.+)>::(GetItem|GetItemConst|operator\\[\\])$");

        for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
            Matcher name = accessor.matcher(function.getName(true));

            if (!name.matches() || function.getSignatureSource() == SourceType.DEFAULT) {
                continue;
            }

            boolean store = !name.group(1).endsWith("Array");
            boolean constant = name.group(3).equals("GetItemConst") || isConstMethod(function);
            DataType returns = resolve(name.group(2) + (store && !constant ? "**" : "*"));
            DataType current = function.getReturnType();

            // An item that's only numbers (a vsMatrix4x4, a vsColor) is copied whole, which the decompiler shows as
            // eight-byte moves; typed, it breaks each into its floats and the copy is unreadable.
            if (!store && returns instanceof Pointer pointer && isOnlyNumbers(pointer.getDataType())) {
                // The project keeps what an earlier run set.
                if (current.isEquivalent(returns)) {
                    function.setReturnType(Undefined8DataType.dataType, SourceType.ANALYSIS);
                }

                continue;
            }

            if (returns != null && (current instanceof Undefined || current == DataType.DEFAULT || current instanceof Pointer pointer && pointer.getDataType() == null)) {
                function.setReturnType(returns, SourceType.ANALYSIS);
                returnsTyped++;
            }
        }
    }

    private boolean isOnlyNumbers(DataType type) {
        if (type instanceof TypeDef typeDef) {
            return isOnlyNumbers(typeDef.getBaseDataType());
        }

        if (type instanceof Array array) {
            return isOnlyNumbers(array.getDataType());
        }

        if (!(type instanceof Structure structure)) {
            return type instanceof AbstractFloatDataType || type instanceof AbstractIntegerDataType;
        }

        for (DataTypeComponent component : structure.getDefinedComponents()) {
            if (!isOnlyNumbers(component.getDataType())) {
                return false;
            }
        }

        return structure.getNumDefinedComponents() > 0;
    }

    // _ZNK: a const method, by its mangled name.
    private boolean isConstMethod(Function function) {
        for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(function.getEntryPoint())) {
            if (symbol.getName().startsWith("_ZNK")) {
                return true;
            }
        }

        return false;
    }

    private void typeReturns(JsonArray list) throws Exception {
        for (JsonElement element : list) {
            JsonObject item = element.getAsJsonObject();
            Function function = getFunctionAt(toAddr(item.get("function").getAsString()));
            DataType returns = resolve(item.get("returns").getAsString());
            DataType current = function != null ? function.getReturnType() : null;

            // Only where the parameters come from the function's name: setting a return type locks the parameters too.
            if (function != null && function.getSignatureSource() == SourceType.DEFAULT) {
                continue;
            }

            if (returns != null && current != null && (current instanceof Undefined || current == DataType.DEFAULT
                || current instanceof Pointer pointer && pointer.getDataType() == null)) {
                function.setReturnType(returns, SourceType.ANALYSIS);
                returnsTyped++;
            }
        }
    }

    // A local's type, from what the code does with it: kept on the stack (a vsBox3D) or in a register from its first
    // use on (what __dynamic_cast returned). Whatever Ghidra had over the same bytes makes way.
    private void typeLocals(JsonArray list) throws Exception {
        clearInferredLocals();

        for (JsonElement element : list) {
            JsonObject item = element.getAsJsonObject();
            Function function = getFunctionAt(toAddr(item.get("function").getAsString()));
            DataType type = resolve(item.get("type").getAsString());
            boolean constructed = item.has("constructed") && item.get("constructed").getAsBoolean();

            // A big object on the stack only when a constructor runs on it: typed from a wrong guess, it would swallow
            // the locals around it.
            if (function == null || type == null || type.getLength() <= 0 || type.getLength() > 0x80 && !constructed) {
                continue;
            }

            try {
                if (item.has("stack")) {
                    typeStackLocal(function, item.get("stack").getAsInt(), type);
                } else {
                    typeRegisterLocal(function, item.get("register").getAsString(), item.get("firstUse").getAsInt(), type);
                }

                localsTyped++;
            } catch (Exception problem) {
                notes.add(function.getName(true) + ": " + item.get("name").getAsString() + " couldn't be typed: " + problem.getMessage());
            }
        }
    }

    // The locals an earlier run typed go first, so they follow what this run's MT2Infer says.
    private void clearInferredLocals() throws Exception {
        for (long entry : gameFunctions) {
            Function function = getFunctionAt(toAddr(entry));

            for (Variable variable : function != null ? function.getLocalVariables() : new Variable[0]) {
                if (INFERRED_LOCAL.equals(variable.getComment())) {
                    function.removeVariable(variable);
                }
            }
        }
    }

    private void typeStackLocal(Function function, int offset, DataType type) throws Exception {
        for (Variable variable : function.getLocalVariables()) {
            if (!variable.isStackVariable()) {
                continue;
            }

            int start = variable.getStackOffset();

            if (start < offset + type.getLength() && offset < start + variable.getLength()) {
                function.removeVariable(variable);
            }
        }

        function.addLocalVariable(new LocalVariableImpl(null, type, offset, currentProgram), SourceType.ANALYSIS).setComment(INFERRED_LOCAL);
    }

    private void typeRegisterLocal(Function function, String registerName, int firstUse, DataType type) throws Exception {
        ghidra.program.model.lang.Register register = currentProgram.getRegister(registerName);

        for (Variable variable : function.getLocalVariables()) {
            if (variable.isRegisterVariable() && register.equals(variable.getRegister()) && variable.getFirstUseOffset() == firstUse) {
                function.removeVariable(variable);
            }
        }

        function.addLocalVariable(new LocalVariableImpl(null, firstUse, type, register, currentProgram), SourceType.ANALYSIS).setComment(INFERRED_LOCAL);
    }

    private void describeFunctions(JsonObject mappings) throws Exception {
        for (JsonElement element : mappings.getAsJsonArray("classes")) {
            for (JsonElement method : element.getAsJsonObject().getAsJsonArray("methods")) {
                describeFunction(method.getAsJsonObject());
            }
        }

        for (JsonElement element : mappings.getAsJsonArray("functions")) {
            describeFunction(element.getAsJsonObject());
        }
    }

    private void describeFunction(JsonObject mapping) throws Exception {
        Function function = mapping.has("address") ? getFunctionAt(toAddr(mapping.get("address").getAsString())) : null;

        if (function == null) {
            notes.add(mapping.get("name").getAsString() + ": no function at its address");

            return;
        }

        function.setComment(comment(mapping));
        functionsDescribed++;

        if (mapping.has("returns")) {
            DataType returns = resolve(mapping.get("returns").getAsString());

            // A class returned by value comes back through a hidden pointer that GCC puts before "this"; Ghidra's own
            // model of that is left alone.
            if (returns != null && !(returns instanceof Structure)) {
                function.setReturnType(returns, SourceType.USER_DEFINED);
            }
        }

        for (JsonElement element : mapping.getAsJsonArray("params")) {
            JsonObject param = element.getAsJsonObject();
            List<Parameter> visible = new ArrayList<>();

            for (Parameter parameter : function.getParameters()) {
                if (!parameter.isAutoParameter() && !"this".equals(parameter.getName())) {
                    visible.add(parameter);
                }
            }

            int position = param.get("index").getAsInt() - 1;

            if (position < visible.size()) {
                visible.get(position).setName(param.get("name").getAsString(), SourceType.USER_DEFINED);
            }
        }
    }

    private String comment(JsonObject mapping) {
        StringBuilder text = new StringBuilder(joined(mapping, "doc", "\n"));

        appendAll(text, mapping, "unsure", "Unsure: ");
        appendAll(text, mapping, "inlined", "Inlined into: ");
        appendAll(text, mapping, "seen", "Seen: ");

        return text.toString();
    }

    private void appendAll(StringBuilder text, JsonObject mapping, String key, String prefix) {
        if (!mapping.has(key)) {
            return;
        }

        for (JsonElement line : mapping.getAsJsonArray(key)) {
            text.append(text.length() > 0 ? "\n" : "").append(prefix).append(line.getAsString());
        }
    }

    private String joined(JsonObject mapping, String key, String separator) {
        List<String> lines = new ArrayList<>();

        if (mapping.has(key)) {
            for (JsonElement line : mapping.getAsJsonArray(key)) {
                lines.add(line.getAsString());
            }
        }

        return String.join(separator, lines);
    }
}
