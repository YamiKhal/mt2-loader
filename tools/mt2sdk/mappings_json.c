#include "mappings_json.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/core/symbols.h"
#include "class_sizes.h"
#include "game_exe.h"
#include "game_image.h"
#include "places.h"


static cJSON* notes_json(const Entry* entry, const char* keyword) {
    cJSON* list = cJSON_CreateArray();

    for (int index = 0; index < entry->note_count; index++) {
        if (strcmp(entry->notes[index].keyword, keyword) == 0) {
            cJSON_AddItemToArray(list, cJSON_CreateString(entry->notes[index].value));
        }
    }

    return list;
}

static cJSON* params_json(const Entry* entry) {
    cJSON* list = cJSON_CreateArray();

    for (int index = 0; index < entry->note_count; index++) {
        const Note* note = &entry->notes[index];
        char name[256] = "";
        int number = 0;
        int used = 0;

        if (strcmp(note->keyword, "param") != 0 || sscanf(note->value, "%d %255s %n", &number, name, &used) < 2) {
            continue;
        }

        cJSON* param = cJSON_CreateObject();
        cJSON_AddNumberToObject(param, "index", number);
        cJSON_AddStringToObject(param, "name", name);

        if (used > 0 && note->value[used] != '\0') {
            cJSON_AddStringToObject(param, "type", note->value + used);
        }

        cJSON_AddItemToArray(list, param);
    }

    return list;
}

// Where the game has the entry, as disassemblers show it: what lets other tools (Ghidra) find it without guessing.
static void add_address(cJSON* object, const Entry* owner, const Entry* entry) {
    char name[PLACE_CAPACITY];
    char address[32];

    if (owner != NULL) {
        snprintf(name, sizeof name, "%s::%s", owner->name, entry->name);
    } else {
        snprintf(name, sizeof name, "%s", entry->name);
    }

    SymbolMatch match = symbols_find(name);

    if (match.result == SYMBOL_FOUND) {
        snprintf(address, sizeof address, "0x%llx", PREFERRED_BASE + match.rva);
        cJSON_AddStringToObject(object, "address", address);
    }
}

static cJSON* entry_json(const Entry* owner, const Entry* entry, uint32_t code_size) {
    cJSON* object = cJSON_CreateObject();
    const Note* returns = mappings_note(entry, "returns");
    const Note* type = mappings_note(entry, "type");
    const Note* size = mappings_note(entry, "size");

    cJSON_AddStringToObject(object, "name", entry->name);
    cJSON_AddStringToObject(object, "file", entry->file);
    cJSON_AddNumberToObject(object, "line", entry->line);

    if (entry->kind == ENTRY_FIELD) {
        cJSON_AddNumberToObject(object, "offset", entry->offset);
        cJSON_AddStringToObject(object, "type", entry->type);
    }

    if (size != NULL) {
        cJSON_AddNumberToObject(object, "size", (double)strtoul(size->value + 2, NULL, 16));
    } else if (code_size > 0) {
        cJSON_AddNumberToObject(object, "size", code_size);
        cJSON_AddStringToObject(object, "sizeFrom", "code");
    }

    if (returns != NULL) {
        cJSON_AddStringToObject(object, "returns", returns->value);
    }

    if (type != NULL) {
        cJSON_AddStringToObject(object, "type", type->value);
    }

    if (entry->kind == ENTRY_METHOD || entry->kind == ENTRY_FUNCTION || entry->kind == ENTRY_GLOBAL) {
        add_address(object, owner, entry);
    }

    if (entry->kind == ENTRY_METHOD || entry->kind == ENTRY_FUNCTION) {
        cJSON_AddItemToObject(object, "params", params_json(entry));
        cJSON_AddItemToObject(object, "inlined", notes_json(entry, "inlined"));
    }

    cJSON_AddItemToObject(object, "doc", notes_json(entry, "doc"));
    cJSON_AddItemToObject(object, "seen", notes_json(entry, "seen"));
    cJSON_AddItemToObject(object, "unsure", notes_json(entry, "unsure"));

    if (entry->kind == ENTRY_CLASS) {
        cJSON* fields = cJSON_AddArrayToObject(object, "fields");
        cJSON* methods = cJSON_AddArrayToObject(object, "methods");

        for (int index = 0; index < entry->member_count; index++) {
            const Entry* member = &entry->members[index];
            cJSON_AddItemToArray(member->kind == ENTRY_FIELD ? fields : methods, entry_json(entry, member, 0));
        }
    }

    return object;
}


// build.txt names the game build the mappings describe, like 0.30.7.
static bool read_build(const wchar_t* folder, char* build, size_t capacity) {
    wchar_t path[GAME_PATH_CAPACITY];
    swprintf(path, GAME_PATH_CAPACITY, L"%ls\\build.txt", folder);

    FILE* file = _wfopen(path, L"r");
    bool read = file != NULL && fgets(build, (int)capacity, file) != NULL;

    if (file != NULL) {
        fclose(file);
    }

    build[read ? strcspn(build, "\r\n") : 0] = '\0';

    return read && build[0] != '\0';
}


bool mappings_json_write(const Mappings* mappings, const GameImage* image, const wchar_t* folder, const wchar_t* output) {
    CodeSize* code_sizes = class_sizes_for_mappings(image, mappings);
    cJSON* root = cJSON_CreateObject();
    char build[64];

    if (read_build(folder, build, sizeof build)) {
        cJSON_AddStringToObject(root, "build", build);
    }

    cJSON* classes = cJSON_AddArrayToObject(root, "classes");
    cJSON* functions = cJSON_AddArrayToObject(root, "functions");
    cJSON* globals = cJSON_AddArrayToObject(root, "globals");

    for (int index = 0; index < mappings->entry_count; index++) {
        const Entry* entry = &mappings->entries[index];
        cJSON* list = entry->kind == ENTRY_CLASS ? classes : entry->kind == ENTRY_FUNCTION ? functions : globals;
        cJSON_AddItemToArray(list, entry_json(NULL, entry, code_sizes != NULL ? code_sizes[index].size : 0));
    }

    free(code_sizes);

    char* text = cJSON_Print(root);
    FILE* file = _wfopen(output, L"wb");
    bool written = file != NULL && text != NULL && fputs(text, file) >= 0;

    if (file != NULL) {
        fclose(file);
    }

    if (!written) {
        fwprintf(stderr, L"%ls couldn't be written\n", output);
    }

    cJSON_free(text);
    cJSON_Delete(root);

    return written;
}
