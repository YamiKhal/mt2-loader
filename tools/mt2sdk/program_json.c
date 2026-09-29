#include "program_json.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/core/symbols.h"
#include "class_sizes.h"
#include "enums.h"
#include "places.h"
#include "reflection.h"
#include "rtti.h"
#include "source_files.h"
#include "vtables.h"

#define TYPEINFO_PREFIX "typeinfo for "

typedef struct ClassNames {
    char** names;
    int count;
    int capacity;
} ClassNames;


static void add_name(ClassNames* list, const char* name) {
    for (int index = list->count - 1; index >= 0 && index >= list->count - 8; index--) {
        if (strcmp(list->names[index], name) == 0) {
            return;
        }
    }

    if (list->count == list->capacity) {
        int grown = list->capacity == 0 ? 1024 : list->capacity * 2;
        char** resized = realloc(list->names, (size_t)grown * sizeof *resized);

        if (resized == NULL) {
            return;
        }

        list->names = resized;
        list->capacity = grown;
    }

    list->names[list->count] = malloc(strlen(name) + 1);

    if (list->names[list->count] != NULL) {
        strcpy(list->names[list->count++], name);
    }
}

static void collect_type_info(size_t index, const char* readable, void* opaque) {
    (void)index;

    if (strncmp(readable, TYPEINFO_PREFIX, strlen(TYPEINFO_PREFIX)) == 0 && strncmp(readable, "typeinfo for __", 15) != 0) {
        add_name(opaque, readable + strlen(TYPEINFO_PREFIX));
    }
}

static int compare_names(const void* left, const void* right) {
    return strcmp(*(char* const*)left, *(char* const*)right);
}

static void remove_repeats(ClassNames* list) {
    int kept = 0;

    qsort(list->names, (size_t)list->count, sizeof *list->names, compare_names);

    for (int index = 0; index < list->count; index++) {
        if (kept > 0 && strcmp(list->names[kept - 1], list->names[index]) == 0) {
            free(list->names[index]);
        } else {
            list->names[kept++] = list->names[index];
        }
    }

    list->count = kept;
}

static double address_of(uint32_t rva) {
    return (double)(PREFERRED_BASE + rva);
}

static cJSON* files_json(const SourceFiles* sources) {
    cJSON* files = cJSON_CreateArray();

    for (int index = 0; index < sources->file_count; index++) {
        const SourceFile* source = &sources->files[index];
        cJSON* file = cJSON_CreateObject();

        cJSON_AddStringToObject(file, "name", source->name);
        cJSON_AddStringToObject(file, "path", source->path);
        cJSON_AddBoolToObject(file, "pathGuessed", source->path_guessed);
        cJSON_AddItemToArray(files, file);
    }

    return files;
}

// [address, file] pairs: which source file each function was compiled from.
static cJSON* functions_json(const SourceFiles* sources) {
    cJSON* functions = cJSON_CreateArray();

    for (int index = 0; index < sources->function_count; index++) {
        cJSON* pair = cJSON_CreateArray();

        cJSON_AddItemToArray(pair, cJSON_CreateNumber(address_of(sources->functions[index].rva)));
        cJSON_AddItemToArray(pair, cJSON_CreateNumber(sources->functions[index].file));
        cJSON_AddItemToArray(functions, pair);
    }

    return functions;
}

static cJSON* bases_json(const GameImage* image, const char* name) {
    RttiBase bases[RTTI_MAX_BASES];
    int count = rtti_bases(image, name, bases, RTTI_MAX_BASES);
    cJSON* list = cJSON_CreateArray();

    for (int index = 0; index < count; index++) {
        cJSON* base = cJSON_CreateObject();

        cJSON_AddStringToObject(base, "name", bases[index].name);
        cJSON_AddNumberToObject(base, "offset", bases[index].offset);
        cJSON_AddBoolToObject(base, "virtual", bases[index].is_virtual);
        cJSON_AddItemToArray(list, base);
    }

    return list;
}

static cJSON* fields_json(const Reflection* reflection, const char* name) {
    cJSON* list = cJSON_CreateArray();

    for (int index = 0; index < reflection->count; index++) {
        const ReflectedField* field = &reflection->fields[index];

        if (!field->has_offset || strcmp(field->class_name, name) != 0) {
            continue;
        }

        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "offset", field->offset);
        cJSON_AddStringToObject(item, "name", field->name);
        cJSON_AddStringToObject(item, "type", field->type);
        cJSON_AddStringToObject(item, "kind", field->kind);
        cJSON_AddItemToArray(list, item);
    }

    return list;
}

// [address, table, slot] for each virtual function.
static cJSON* vtable_json(const GameImage* image, const char* name, VtableEntry* entries) {
    uint32_t table = 0;
    cJSON* list = cJSON_CreateArray();
    int count = place_class_symbol("_ZTV", name, &table) ? vtables_read(image, table, entries, VTABLE_MAX_ENTRIES) : 0;

    for (int index = 0; index < count; index++) {
        cJSON* entry = cJSON_CreateArray();

        cJSON_AddItemToArray(entry, cJSON_CreateNumber(address_of(entries[index].function)));
        cJSON_AddItemToArray(entry, cJSON_CreateNumber(entries[index].table));
        cJSON_AddItemToArray(entry, cJSON_CreateNumber(entries[index].slot));
        cJSON_AddItemToArray(list, entry);
    }

    return list;
}

static cJSON* classes_json(const GameImage* image, const Reflection* reflection, const ClassNames* names) {
    cJSON* classes = cJSON_CreateArray();
    CodeSize* sizes = calloc((size_t)names->count + 1, sizeof *sizes);
    VtableEntry* entries = malloc(VTABLE_MAX_ENTRIES * sizeof *entries);

    if (sizes != NULL) {
        class_sizes_from_code(image, (const char* const*)names->names, names->count, sizes);
    }

    for (int index = 0; index < names->count && entries != NULL; index++) {
        const char* name = names->names[index];
        cJSON* item = cJSON_CreateObject();

        cJSON_AddStringToObject(item, "name", name);

        if (sizes != NULL && sizes[index].size > 0) {
            cJSON_AddNumberToObject(item, "size", sizes[index].size);
            cJSON_AddNumberToObject(item, "sizePlaces", sizes[index].places);
        }

        cJSON_AddItemToObject(item, "bases", bases_json(image, name));
        cJSON_AddItemToObject(item, "fields", fields_json(reflection, name));
        cJSON_AddItemToObject(item, "vtable", vtable_json(image, name, entries));
        cJSON_AddItemToArray(classes, item);
    }

    free(entries);
    free(sizes);

    return classes;
}

static cJSON* enums_json(const GameEnums* enums) {
    cJSON* list = cJSON_CreateArray();

    for (int index = 0; index < enums->count; index++) {
        const GameEnum* game_enum = &enums->items[index];
        cJSON* item = cJSON_CreateObject();
        cJSON* values = cJSON_AddArrayToObject(item, "values");

        cJSON_AddStringToObject(item, "name", game_enum->name);

        for (int value = 0; value < game_enum->count; value++) {
            cJSON_AddItemToArray(values, cJSON_CreateString(game_enum->values[value]));
        }

        cJSON_AddItemToArray(list, item);
    }

    return list;
}

static bool write_text(cJSON* root, const wchar_t* output) {
    char* text = cJSON_PrintUnformatted(root);
    FILE* file = _wfopen(output, L"wb");
    bool written = file != NULL && text != NULL && fputs(text, file) >= 0;

    if (file != NULL) {
        fclose(file);
    }

    if (!written) {
        fwprintf(stderr, L"%ls couldn't be written\n", output);
    }

    cJSON_free(text);

    return written;
}


bool program_json_write(const GameImage* image, const char* build, const wchar_t* output) {
    SourceFiles sources;
    Reflection reflection;
    GameEnums enums;
    ClassNames names = { 0 };

    if (!source_files_read(image, &sources)) {
        fprintf(stderr, "The exe has no source file names in its symbol table\n");

        return false;
    }

    bool have_reflection = reflection_read(image, &reflection);
    bool have_enums = enums_read(image, &enums);

    symbols_each_containing(TYPEINFO_PREFIX, collect_type_info, &names);

    for (int index = 0; have_reflection && index < reflection.count; index++) {
        add_name(&names, reflection.fields[index].class_name);
    }

    remove_repeats(&names);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "build", build);
    cJSON_AddItemToObject(root, "files", files_json(&sources));
    cJSON_AddItemToObject(root, "functions", functions_json(&sources));
    cJSON_AddItemToObject(root, "classes", have_reflection ? classes_json(image, &reflection, &names) : cJSON_CreateArray());
    cJSON_AddItemToObject(root, "enums", have_enums ? enums_json(&enums) : cJSON_CreateArray());

    bool written = write_text(root, output);

    if (written) {
        wprintf(L"%d source files, %d functions, %d classes, %d enums written to %ls\n", sources.file_count, sources.function_count,
            names.count, have_enums ? enums.count : 0, output);
    }

    cJSON_Delete(root);

    for (int index = 0; index < names.count; index++) {
        free(names.names[index]);
    }

    free(names.names);

    if (have_enums) {
        enums_free(&enums);
    }

    if (have_reflection) {
        reflection_free(&reflection);
    }

    source_files_free(&sources);

    return written;
}
