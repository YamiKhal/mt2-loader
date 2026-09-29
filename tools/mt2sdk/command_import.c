#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "commands.h"
#include "game_exe.h"
#include "game_image.h"
#include "mappings.h"
#include "places.h"

#define MAX_LINES 20000
#define LINE_CAPACITY 2048
#define UNSURE_FROM_GHIDRA "        unsure Named in Ghidra: add a seen line for an instruction that shows it"

typedef struct TextFile {
    char* lines[MAX_LINES];
    int count;
} TextFile;

typedef struct ImportCounts {
    int fields;
    int functions;
    int files;
} ImportCounts;


static char* read_whole(const wchar_t* path) {
    FILE* file = _wfopen(path, L"rb");

    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* text = malloc((size_t)size + 1);

    if (text != NULL && fread(text, 1, (size_t)size, file) == (size_t)size) {
        text[size] = '\0';
    } else {
        free(text);
        text = NULL;
    }

    fclose(file);

    return text;
}

static void load_lines(const wchar_t* path, TextFile* text) {
    char line[LINE_CAPACITY];
    FILE* file = _wfopen(path, L"rb");

    text->count = 0;

    while (file != NULL && text->count < MAX_LINES && fgets(line, sizeof line, file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        text->lines[text->count] = malloc(strlen(line) + 1);
        strcpy(text->lines[text->count++], line);
    }

    if (file != NULL) {
        fclose(file);
    }
}

static bool save_lines(const wchar_t* path, TextFile* text) {
    FILE* file = _wfopen(path, L"wb");

    for (int index = 0; file != NULL && index < text->count; index++) {
        fprintf(file, "%s\n", text->lines[index]);
    }

    if (file != NULL) {
        fclose(file);
    }

    for (int index = 0; index < text->count; index++) {
        free(text->lines[index]);
    }

    text->count = 0;

    return file != NULL;
}

static void insert_line(TextFile* text, int at, const char* line) {
    if (text->count >= MAX_LINES) {
        return;
    }

    memmove(&text->lines[at + 1], &text->lines[at], (size_t)(text->count - at) * sizeof text->lines[0]);
    text->lines[at] = malloc(strlen(line) + 1);
    strcpy(text->lines[at], line);
    text->count++;
}

static int indent_of(const char* line) {
    int spaces = 0;

    while (line[spaces] == ' ') {
        spaces++;
    }

    return line[spaces] == '\0' || line[spaces] == '#' ? -1 : spaces;
}

// Where a block that starts at line start (indented by indent) ends: the next line indented as much or less.
static int block_end(const TextFile* text, int start, int indent) {
    int end = start + 1;

    for (int index = start + 1; index < text->count; index++) {
        int spaces = indent_of(text->lines[index]);

        if (spaces >= 0 && spaces <= indent) {
            break;
        }

        if (spaces > indent) {
            end = index + 1;
        }
    }

    return end;
}

static int find_line(const TextFile* text, int from, int to, const char* wanted) {
    for (int index = from; index < to; index++) {
        if (strcmp(text->lines[index], wanted) == 0) {
            return index;
        }
    }

    return -1;
}

// game/ for the game's classes, engine/ for the engine's, std/ for the C++ library's, the class's own file otherwise.
static void file_for_class(const wchar_t* folder, const Mappings* mappings, const char* class_name, wchar_t* path) {
    for (int index = 0; index < mappings->entry_count; index++) {
        const Entry* entry = &mappings->entries[index];

        if (entry->kind == ENTRY_CLASS && strcmp(entry->name, class_name) == 0) {
            swprintf(path, GAME_PATH_CAPACITY, L"%ls\\%hs", folder, entry->file);

            return;
        }
    }

    char file_name[PLACE_CAPACITY];
    const char* subfolder = strncmp(class_name, "vs", 2) == 0 ? "engine" : strncmp(class_name, "std::", 5) == 0 ? "std" : "game";
    const char* short_name = strncmp(class_name, "std::", 5) == 0 ? class_name + 5 : class_name;
    int length = 0;

    for (const char* character = short_name; *character != '\0' && length < (int)sizeof file_name - 16; character++) {
        file_name[length++] = strchr("<>:\"/\\|?* ", *character) != NULL ? '_' : *character;
    }

    file_name[length] = '\0';
    swprintf(path, GAME_PATH_CAPACITY, L"%ls\\%hs", folder, subfolder);
    CreateDirectoryW(path, NULL);
    swprintf(path, GAME_PATH_CAPACITY, L"%ls\\%hs\\%hs.mapping", folder, subfolder, file_name);
}

// The class's block in the file, made at the end when there's none; returns where new members go.
static int class_block_end(TextFile* text, const char* class_name) {
    char header[PLACE_CAPACITY + 8];
    snprintf(header, sizeof header, "class %s", class_name);

    int start = find_line(text, 0, text->count, header);

    if (start < 0) {
        if (text->count > 0) {
            insert_line(text, text->count, "");
        }

        insert_line(text, text->count, header);
        start = text->count - 1;
    }

    return block_end(text, start, 0);
}

static void add_field(TextFile* text, const char* class_name, const cJSON* field, ImportCounts* counts) {
    char line[LINE_CAPACITY];
    const char* name = cJSON_GetStringValue(cJSON_GetObjectItem(field, "name"));
    const char* type = cJSON_GetStringValue(cJSON_GetObjectItem(field, "type"));
    const char* doc = cJSON_GetStringValue(cJSON_GetObjectItem(field, "doc"));
    int offset = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(field, "offset"));
    int at = class_block_end(text, class_name);

    snprintf(line, sizeof line, "    field 0x%x %s %s", offset, name, type);
    insert_line(text, at++, line);

    if (doc != NULL && doc[0] != '\0') {
        snprintf(line, sizeof line, "        doc %s", doc);
        insert_line(text, at++, line);
    }

    insert_line(text, at, UNSURE_FROM_GHIDRA);
    counts->fields++;
}

// "mmoCharacter::EquipWeaponModel(bool)" is class mmoCharacter, method EquipWeaponModel(bool).
static bool split_name(const char* readable, char* class_name, size_t class_capacity, const char** method) {
    const char* open = strchr(readable, '(');
    const char* split = NULL;

    for (const char* at = readable; open != NULL && at < open; at++) {
        if (at[0] == ':' && at[1] == ':') {
            split = at;
        }
    }

    if (split == NULL) {
        return false;
    }

    snprintf(class_name, class_capacity, "%.*s", (int)(split - readable), readable);
    *method = split + 2;

    return true;
}

static void add_function(TextFile* text, const char* class_name, const char* method, const cJSON* facts, ImportCounts* counts) {
    char line[LINE_CAPACITY];
    char header[LINE_CAPACITY];
    int at = class_block_end(text, class_name);
    int start = -1;

    snprintf(header, sizeof header, "    method %s", method);
    start = find_line(text, 0, text->count, header);

    if (start < 0) {
        insert_line(text, at, header);
        start = at;
    }

    at = block_end(text, start, 4);

    const char* returns = cJSON_GetStringValue(cJSON_GetObjectItem(facts, "returns"));
    const char* doc = cJSON_GetStringValue(cJSON_GetObjectItem(facts, "doc"));
    const cJSON* param = NULL;

    if (returns != NULL) {
        snprintf(line, sizeof line, "        returns %s", returns);
        insert_line(text, at++, line);
    }

    cJSON_ArrayForEach(param, cJSON_GetObjectItem(facts, "params")) {
        snprintf(line, sizeof line, "        param %d %s", (int)cJSON_GetNumberValue(cJSON_GetObjectItem(param, "index")),
            cJSON_GetStringValue(cJSON_GetObjectItem(param, "name")));
        insert_line(text, at++, line);
    }

    // A comment can have several lines: each becomes a doc line.
    for (const char* piece = doc; piece != NULL && *piece != '\0';) {
        size_t length = strcspn(piece, "\n");

        if (length > 0) {
            snprintf(line, sizeof line, "        doc %.*s", (int)length, piece);
            insert_line(text, at++, line);
        }

        piece += length + (piece[length] == '\n' ? 1 : 0);
    }

    insert_line(text, at, "        unsure From Ghidra: add a seen line for an instruction that shows it");
    counts->functions++;
}

static bool import_class_fields(const wchar_t* folder, const Mappings* mappings, const cJSON* item, ImportCounts* counts) {
    wchar_t path[GAME_PATH_CAPACITY];
    const char* class_name = cJSON_GetStringValue(cJSON_GetObjectItem(item, "name"));
    TextFile* text = calloc(1, sizeof *text);
    const cJSON* field = NULL;

    if (text == NULL || class_name == NULL) {
        free(text);

        return false;
    }

    file_for_class(folder, mappings, class_name, path);
    load_lines(path, text);

    cJSON_ArrayForEach(field, cJSON_GetObjectItem(item, "fields")) {
        add_field(text, class_name, field, counts);
    }

    bool saved = save_lines(path, text);
    counts->files += saved ? 1 : 0;
    free(text);

    return saved;
}

static bool import_function(const wchar_t* folder, const Mappings* mappings, const cJSON* facts, ImportCounts* counts) {
    wchar_t path[GAME_PATH_CAPACITY];
    char readable[PLACE_CAPACITY];
    char class_name[PLACE_CAPACITY];
    const char* method = NULL;
    uint32_t rva = 0;
    const char* address = cJSON_GetStringValue(cJSON_GetObjectItem(facts, "address"));

    if (address == NULL || !place_find(address, &rva)) {
        return false;
    }

    place_name(rva, readable, sizeof readable);

    if (!split_name(readable, class_name, sizeof class_name, &method)) {
        printf("  %s is outside any class: add it by hand to game/functions.mapping\n", readable);

        return false;
    }

    TextFile* text = calloc(1, sizeof *text);

    if (text == NULL) {
        return false;
    }

    file_for_class(folder, mappings, class_name, path);
    load_lines(path, text);
    add_function(text, class_name, method, facts, counts);

    bool saved = save_lines(path, text);
    counts->files += saved ? 1 : 0;
    free(text);

    return saved;
}


int command_mappings_import(const wchar_t* exe, const wchar_t* found_path, const wchar_t* folder) {
    wchar_t path[GAME_PATH_CAPACITY];
    Mappings mappings;
    ImportCounts counts = { 0 };
    char* text = read_whole(found_path);
    cJSON* found = text != NULL ? cJSON_Parse(text) : NULL;

    free(text);

    if (found == NULL) {
        fwprintf(stderr, L"%ls isn't a JSON file from MT2Import.java\n", found_path);

        return 1;
    }

    if (!game_exe_load_symbols(exe, path) || !mappings_load(folder, &mappings)) {
        cJSON_Delete(found);

        return 1;
    }

    const cJSON* item = NULL;

    cJSON_ArrayForEach(item, cJSON_GetObjectItem(found, "classes")) {
        import_class_fields(folder, &mappings, item, &counts);
    }

    cJSON_ArrayForEach(item, cJSON_GetObjectItem(found, "functions")) {
        import_function(folder, &mappings, item, &counts);
    }

    printf("%d fields and %d functions added (%d file writes). Add a seen line to each, then: mt2sdk mappings check\n",
        counts.fields, counts.functions, counts.files);

    mappings_free(&mappings);
    cJSON_Delete(found);

    return 0;
}
