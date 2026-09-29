// Collects what someone named or wrote in Ghidra that mt2-mappings doesn't have yet: fields named in a class's
// structure, functions' return types and parameter names set by hand, and function comments. Writes it as JSON for
// mt2sdk mappings import, which adds it to the .mapping files.
// Arguments: the JSON file to write, program.json (mt2sdk program), and mappings.json (mt2sdk mappings json) or "-".
// @category MT2

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeComponent;
import ghidra.program.model.data.Structure;
import ghidra.program.model.data.Undefined;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.GhidraClass;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.VariableUtilities;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class MT2Import extends GhidraScript {

    private final Map<String, Set<Integer>> knownFields = new HashMap<>();
    private final Map<String, JsonObject> knownFunctions = new HashMap<>();

    @Override
    public void run() throws Exception {
        String[] arguments = getScriptArgs();
        File output = arguments.length > 0 ? new File(arguments[0]) : askFile("JSON file to write", "Write");
        File programFile = arguments.length > 1 ? new File(arguments[1]) : askFile("program.json from mt2sdk program", "Use");
        File mappingsFile = arguments.length > 2 && !arguments[2].equals("-") ? new File(arguments[2]) : null;
        JsonObject program = readJson(programFile);

        readKnownFields(program.getAsJsonArray("classes"));

        if (mappingsFile != null) {
            JsonObject mappings = readJson(mappingsFile);
            readKnownFields(mappings.getAsJsonArray("classes"));
            readKnownFunctions(mappings);
        }

        JsonObject found = new JsonObject();
        found.add("classes", newFields(program.getAsJsonArray("classes")));
        found.add("functions", newFunctionFacts());

        Gson gson = new GsonBuilder().setPrettyPrinting().create();
        Files.writeString(output.toPath(), gson.toJson(found), StandardCharsets.UTF_8);
        println("Found " + found.getAsJsonArray("classes").size() + " classes with new fields and "
            + found.getAsJsonArray("functions").size() + " functions with new facts. Next: mt2sdk mappings import");
    }

    private JsonObject readJson(File file) throws Exception {
        return JsonParser.parseString(Files.readString(file.toPath(), StandardCharsets.UTF_8)).getAsJsonObject();
    }

    private void readKnownFields(JsonArray classes) {
        for (JsonElement element : classes) {
            JsonObject item = element.getAsJsonObject();
            Set<Integer> offsets = knownFields.computeIfAbsent(item.get("name").getAsString(), key -> new HashSet<>());

            for (JsonElement field : item.getAsJsonArray("fields")) {
                offsets.add(field.getAsJsonObject().get("offset").getAsInt());
            }
        }
    }

    private void readKnownFunctions(JsonObject mappings) {
        for (JsonElement element : mappings.getAsJsonArray("classes")) {
            for (JsonElement method : element.getAsJsonObject().getAsJsonArray("methods")) {
                remember(method.getAsJsonObject());
            }
        }

        for (JsonElement function : mappings.getAsJsonArray("functions")) {
            remember(function.getAsJsonObject());
        }
    }

    private void remember(JsonObject mapping) {
        if (mapping.has("address")) {
            knownFunctions.put(mapping.get("address").getAsString().replace("0x", ""), mapping);
        }
    }

    // A field counts when it has a name of its own (not Ghidra's field_0x..) at an offset no one has described, and
    // belongs to this class (a base class's fields are copied into each class that's built on it).
    private JsonArray newFields(JsonArray classes) throws Exception {
        JsonArray result = new JsonArray();

        for (JsonElement element : classes) {
            String name = element.getAsJsonObject().get("name").getAsString();
            Structure structure = existingStructure(name);

            if (structure == null) {
                continue;
            }

            JsonArray fields = new JsonArray();
            Set<Integer> known = knownFields.getOrDefault(name, Set.of());

            for (DataTypeComponent component : structure.getDefinedComponents()) {
                String fieldName = component.getFieldName();
                DataType type = component.getDataType();

                if (fieldName == null || fieldName.startsWith("field_0x") || fieldName.equals("vtable") || type instanceof Undefined
                    || known.contains(component.getOffset()) || isFromBase(name, component.getOffset())) {
                    continue;
                }

                JsonObject field = new JsonObject();
                field.addProperty("offset", component.getOffset());
                field.addProperty("name", fieldName);
                field.addProperty("type", type.getDisplayName().replace(" *", "*"));
                field.addProperty("doc", component.getComment() == null ? "" : component.getComment());
                fields.add(field);
            }

            if (!fields.isEmpty()) {
                JsonObject item = new JsonObject();
                item.addProperty("name", name);
                item.add("fields", fields);
                result.add(item);
            }
        }

        return result;
    }

    private boolean isFromBase(String className, int offset) {
        for (Map.Entry<String, Set<Integer>> other : knownFields.entrySet()) {
            if (!other.getKey().equals(className) && other.getValue().contains(offset) && className.contains(other.getKey())) {
                return true;
            }
        }

        return false;
    }

    private Structure existingStructure(String name) {
        try {
            List<Namespace> found = NamespaceUtils.getNamespaceByPath(currentProgram, null, name);

            if (found.isEmpty() || !(found.get(0) instanceof GhidraClass ghidraClass)) {
                return null;
            }

            return VariableUtilities.findExistingClassStruct(ghidraClass, currentProgram.getDataTypeManager());
        } catch (Exception problem) {
            return null;
        }
    }

    // Return types and parameter names someone set by hand, and comments that aren't the ones MT2Apply wrote.
    private JsonArray newFunctionFacts() {
        JsonArray result = new JsonArray();

        for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
            String address = function.getEntryPoint().toString();
            JsonObject mapping = knownFunctions.get(address);
            JsonObject facts = new JsonObject();

            if (function.getSignatureSource() == SourceType.USER_DEFINED && (mapping == null || !mapping.has("returns"))) {
                facts.addProperty("returns", function.getReturnType().getDisplayName().replace(" *", "*"));
            }

            JsonArray params = new JsonArray();
            int index = 0;

            for (Parameter parameter : function.getParameters()) {
                if (parameter.isAutoParameter() || parameter.getName().equals("this")) {
                    continue;
                }

                index++;

                if (parameter.getSource() == SourceType.USER_DEFINED && !parameter.getName().startsWith("param_")
                    && (mapping == null || mapping.getAsJsonArray("params").isEmpty())) {
                    JsonObject param = new JsonObject();
                    param.addProperty("index", index);
                    param.addProperty("name", parameter.getName());
                    params.add(param);
                }
            }

            if (!params.isEmpty()) {
                facts.add("params", params);
            }

            String comment = function.getComment();

            if (comment != null && !comment.isBlank() && (mapping == null || !comment.startsWith(firstDoc(mapping)))) {
                facts.addProperty("doc", comment.replaceAll("(?m)^(Seen|Unsure|Inlined into): .*$", "").trim());
            }

            if (facts.size() > 0) {
                facts.addProperty("address", "0x" + address);
                result.add(facts);
            }
        }

        return result;
    }

    private String firstDoc(JsonObject mapping) {
        JsonArray doc = mapping.getAsJsonArray("doc");

        return doc == null || doc.isEmpty() ? "\u0000" : doc.get(0).getAsString();
    }
}
