#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../../src/common/pe_image.h"
#include "commands.h"
#include "game_exe.h"
#include "game_image.h"
#include "mappings.h"
#include "places.h"

#define COFF_ENTRY_SIZE 18
#define TYPE_FUNCTION 0x20

typedef struct NamedFunction {
    const char* name;
    uint32_t rva;
    uint32_t size;
    // Local functions of the same name in several files (__static_initialization_and_destruction_0) can't be told
    // apart by name, so they aren't compared.
    bool repeated;
} NamedFunction;

typedef struct FunctionTable {
    NamedFunction* items;
    int count;
} FunctionTable;

typedef struct DiffReport {
    FILE* details;
    int added;
    int removed;
    int resized;
} DiffReport;


static int compare_by_rva(const void* left, const void* right) {
    uint32_t left_rva = ((const NamedFunction*)left)->rva;
    uint32_t right_rva = ((const NamedFunction*)right)->rva;

    return left_rva < right_rva ? -1 : left_rva > right_rva;
}

static int compare_by_name(const void* left, const void* right) {
    return strcmp(((const NamedFunction*)left)->name, ((const NamedFunction*)right)->name);
}

// Every function's raw (mangled) name and size, from an exe's symbol table: names stay the same across builds, so
// they're what two builds are compared by.
static bool read_functions(const GameImage* image, FunctionTable* table) {
    IMAGE_NT_HEADERS64* headers = pe_nt_headers((HMODULE)image->file);
    int capacity = 0;

    memset(table, 0, sizeof *table);

    if (headers == NULL) {
        return false;
    }

    uint32_t symbols = headers->FileHeader.PointerToSymbolTable;
    uint32_t count = headers->FileHeader.NumberOfSymbols;
    uint32_t strings = symbols + count * COFF_ENTRY_SIZE;

    for (uint32_t index = 0; index < count && symbols + (index + 1) * COFF_ENTRY_SIZE <= image->file_size;) {
        const uint8_t* entry = image->file + symbols + index * COFF_ENTRY_SIZE;
        uint32_t zero = 0;
        uint32_t offset = 0;
        uint32_t value = 0;
        int16_t section = 0;
        uint16_t type = 0;
        uint8_t aux_count = entry[17];

        memcpy(&zero, entry, 4);
        memcpy(&offset, entry + 4, 4);
        memcpy(&value, entry + 8, 4);
        memcpy(&section, entry + 12, 2);
        memcpy(&type, entry + 14, 2);

        bool is_function = section > 0 && section <= image->section_count && image->sections[section - 1].is_code && (type & TYPE_FUNCTION);

        if (is_function && zero == 0 && strings + offset < image->file_size) {
            if (table->count == capacity) {
                capacity = capacity == 0 ? 65536 : capacity * 2;
                table->items = realloc(table->items, (size_t)capacity * sizeof *table->items);
            }

            table->items[table->count++] = (NamedFunction){ (const char*)image->file + strings + offset, image->sections[section - 1].rva + value, 0, false };
        }

        index += 1u + aux_count;
    }

    qsort(table->items, (size_t)table->count, sizeof *table->items, compare_by_rva);

    for (int index = 0; index + 1 < table->count; index++) {
        table->items[index].size = table->items[index + 1].rva - table->items[index].rva;
    }

    qsort(table->items, (size_t)table->count, sizeof *table->items, compare_by_name);

    for (int index = 0; index + 1 < table->count; index++) {
        if (strcmp(table->items[index].name, table->items[index + 1].name) == 0) {
            table->items[index].repeated = true;
            table->items[index + 1].repeated = true;
        }
    }

    return table->count > 0;
}

static const NamedFunction* find_function(const FunctionTable* table, const char* name) {
    NamedFunction key = { name, 0, 0, false };

    return bsearch(&key, table->items, (size_t)table->count, sizeof key, compare_by_name);
}

static void compare_functions(const FunctionTable* old_table, const FunctionTable* new_table, DiffReport* report) {
    fprintf(report->details, "Functions (raw names)\n");

    for (int index = 0; index < new_table->count; index++) {
        const NamedFunction* now = &new_table->items[index];
        const NamedFunction* before = find_function(old_table, now->name);

        if (now->repeated || (before != NULL && before->repeated)) {
            continue;
        }

        if (before == NULL) {
            fprintf(report->details, "  added    %s\n", now->name);
            report->added++;
        } else if (before->size != now->size) {
            fprintf(report->details, "  resized  %s (%u -> %u bytes)\n", now->name, before->size, now->size);
            report->resized++;
        }
    }

    for (int index = 0; index < old_table->count; index++) {
        if (!old_table->items[index].repeated && find_function(new_table, old_table->items[index].name) == NULL) {
            fprintf(report->details, "  removed  %s\n", old_table->items[index].name);
            report->removed++;
        }
    }
}

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

// program.json of a build, made by running this tool on that build's exe.
static cJSON* program_of(const wchar_t* exe, const wchar_t* output) {
    wchar_t self[MAX_PATH];
    wchar_t command[4 * MAX_PATH];
    STARTUPINFOW startup = { .cb = sizeof startup };
    PROCESS_INFORMATION process;

    GetModuleFileNameW(NULL, self, MAX_PATH);
    swprintf(command, 4 * MAX_PATH, L"\"%ls\" program \"%ls\" --exe \"%ls\"", self, output, exe);

    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        return NULL;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);

    char* text = read_whole(output);
    cJSON* program = text != NULL ? cJSON_Parse(text) : NULL;
    free(text);

    return program;
}

static const cJSON* by_name(const cJSON* list, const char* name) {
    const cJSON* item = NULL;

    cJSON_ArrayForEach(item, list) {
        const char* item_name = cJSON_GetStringValue(cJSON_GetObjectItem(item, "name"));

        if (item_name != NULL && strcmp(item_name, name) == 0) {
            return item;
        }
    }

    return NULL;
}

static int compare_fields(const cJSON* old_class, const cJSON* new_class, const char* class_name, FILE* details) {
    const cJSON* field = NULL;
    int moved = 0;

    cJSON_ArrayForEach(field, cJSON_GetObjectItem(new_class, "fields")) {
        const char* name = cJSON_GetStringValue(cJSON_GetObjectItem(field, "name"));
        const cJSON* before = by_name(cJSON_GetObjectItem(old_class, "fields"), name);
        int offset = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(field, "offset"));

        if (before == NULL) {
            fprintf(details, "  new field  %s::%s at 0x%x\n", class_name, name, offset);
        } else if ((int)cJSON_GetNumberValue(cJSON_GetObjectItem(before, "offset")) != offset) {
            fprintf(details, "  moved      %s::%s 0x%x -> 0x%x\n", class_name, name,
                (int)cJSON_GetNumberValue(cJSON_GetObjectItem(before, "offset")), offset);
            moved++;
        }
    }

    return moved;
}

// Classes whose size or named fields changed: offsets anyone relies on in them need a look.
static int compare_classes(const cJSON* old_program, const cJSON* new_program, FILE* details, char changed[][256], int capacity) {
    const cJSON* item = NULL;
    int count = 0;

    fprintf(details, "\nClasses and the fields the game names\n");

    cJSON_ArrayForEach(item, cJSON_GetObjectItem(new_program, "classes")) {
        const char* name = cJSON_GetStringValue(cJSON_GetObjectItem(item, "name"));
        const cJSON* before = by_name(cJSON_GetObjectItem(old_program, "classes"), name);
        int size = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(item, "size"));
        int old_size = before != NULL ? (int)cJSON_GetNumberValue(cJSON_GetObjectItem(before, "size")) : 0;

        if (before == NULL) {
            continue;
        }

        int moved = compare_fields(before, item, name, details);

        if (size != old_size && size > 0 && old_size > 0) {
            fprintf(details, "  resized    %s 0x%x -> 0x%x\n", name, old_size, size);
        }

        if ((moved > 0 || (size != old_size && size > 0 && old_size > 0)) && count < capacity) {
            snprintf(changed[count++], 256, "%s", name);
        }
    }

    return count;
}

static bool is_changed_class(char changed[][256], int count, const char* name) {
    for (int index = 0; index < count; index++) {
        if (strcmp(changed[index], name) == 0) {
            return true;
        }
    }

    return false;
}

static int flag_places(const Entry* entry, const char* place_start, FILE* details) {
    int flagged = 0;

    for (int note = 0; note < entry->note_count; note++) {
        const Note* place = &entry->notes[note];
        bool is_place = strcmp(place->keyword, "seen") == 0 || strcmp(place->keyword, "inlined") == 0;

        if (is_place && strncmp(place->value, place_start, strlen(place_start)) == 0) {
            fprintf(details, "  %s:%d  %s %s: the function changed\n", entry->file, place->line, place->keyword, place->value);
            flagged++;
        }
    }

    return flagged;
}

// Which mapped facts to look at again: seen places in functions that changed, and fields by offset in classes whose
// layout changed.
static int check_mappings(const Mappings* mappings, const FunctionTable* old_table, const FunctionTable* new_table,
    char changed[][256], int changed_count, FILE* details) {
    char raw_needed[PLACE_CAPACITY + 8];
    int flagged = 0;

    fprintf(details, "\nmt2-mappings to look at again\n");

    for (int index = 0; index < mappings->entry_count; index++) {
        const Entry* entry = &mappings->entries[index];

        for (int member = 0; entry->kind == ENTRY_CLASS && member < entry->member_count; member++) {
            const Entry* field = &entry->members[member];

            if (field->kind == ENTRY_FIELD && is_changed_class(changed, changed_count, entry->name)) {
                fprintf(details, "  %s:%d  %s::%s at 0x%x: the class's layout changed\n", field->file, field->line, entry->name, field->name, field->offset);
                flagged++;
            }
        }
    }

    // A seen place names a function; if its code changed size, the offset may point elsewhere now.
    for (int index = 0; index < new_table->count; index++) {
        const NamedFunction* now = &new_table->items[index];
        const NamedFunction* before = find_function(old_table, now->name);
        char readable[PLACE_CAPACITY];

        if (before == NULL || before->repeated || now->repeated || before->size == now->size) {
            continue;
        }

        place_name(now->rva, readable, sizeof readable);
        snprintf(raw_needed, sizeof raw_needed, "%s+0x", readable);

        for (int entry_index = 0; entry_index < mappings->entry_count; entry_index++) {
            const Entry* entry = &mappings->entries[entry_index];
            flagged += flag_places(entry, raw_needed, details);

            for (int member = 0; member < entry->member_count; member++) {
                flagged += flag_places(&entry->members[member], raw_needed, details);
            }
        }
    }

    return flagged;
}


int command_diff(const wchar_t* exe, const wchar_t* old_exe, const wchar_t* mappings_folder) {
    wchar_t path[GAME_PATH_CAPACITY];
    wchar_t old_json[MAX_PATH];
    wchar_t new_json[MAX_PATH];
    wchar_t temp[MAX_PATH];
    GameImage old_image;
    GameImage new_image;
    FunctionTable old_table;
    FunctionTable new_table;

    if (!game_exe_load_symbols(exe, path) || !game_image_load(old_exe, &old_image) || !game_image_load(path, &new_image)) {
        return 1;
    }

    read_functions(&old_image, &old_table);
    read_functions(&new_image, &new_table);

    GetTempPathW(MAX_PATH, temp);
    swprintf(old_json, MAX_PATH, L"%lsmt2sdk_old_program.json", temp);
    swprintf(new_json, MAX_PATH, L"%lsmt2sdk_new_program.json", temp);

    FILE* details = _wfopen(L"mt2_diff.txt", L"w");
    DiffReport report = { details, 0, 0, 0 };

    if (details == NULL) {
        fprintf(stderr, "mt2_diff.txt couldn't be written here\n");

        return 1;
    }

    compare_functions(&old_table, &new_table, &report);

    cJSON* old_program = program_of(old_exe, old_json);
    cJSON* new_program = program_of(path, new_json);
    static char changed[4096][256];
    int changed_count = old_program != NULL && new_program != NULL ? compare_classes(old_program, new_program, details, changed, 4096) : 0;
    int flagged = 0;
    Mappings mappings;

    if (mappings_folder != NULL && mappings_load(mappings_folder, &mappings)) {
        flagged = check_mappings(&mappings, &old_table, &new_table, changed, changed_count, details);
        mappings_free(&mappings);
    }

    fclose(details);

    printf("Functions: %d added, %d removed, %d changed size\n", report.added, report.removed, report.resized);
    printf("Classes whose size or named fields changed: %d\n", changed_count);

    if (mappings_folder != NULL) {
        printf("mt2-mappings facts to look at again: %d (then: mt2sdk mappings check)\n", flagged);
    }

    printf("Every change, by name: mt2_diff.txt\n");

    cJSON_Delete(old_program);
    cJSON_Delete(new_program);
    free(old_table.items);
    free(new_table.items);
    game_image_free(&old_image);
    game_image_free(&new_image);

    return 0;
}
