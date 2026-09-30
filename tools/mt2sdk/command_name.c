#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "commands.h"
#include "disassembly.h"
#include "game_exe.h"
#include "game_image.h"
#include "mappings.h"
#include "places.h"

#define PATH_CAPACITY 1024
#define LINE_CAPACITY 4096
#define NAME_CAPACITY 512
#define MAX_USERS 32
#define MAX_FILE_LINES 20000

// What unnamed_fields.tsv says about one field: the functions that use it, and the file that uses it most.
typedef struct Users {
    char functions[MAX_USERS][NAME_CAPACITY];
    int count;
    char busiest_file[PATH_CAPACITY];
    int busiest_uses;
} Users;


// vsArrayStore<T> in the mappings is every vsArrayStore<...> in the code.
static bool same_class(const char* written, const char* in_code) {
    const char* template_start = strchr(written, '<');

    if (template_start == NULL) {
        return strcmp(written, in_code) == 0;
    }

    size_t base = (size_t)(template_start - written);

    return strncmp(written, in_code, base) == 0 && in_code[base] == '<';
}

static void add_user(Users* users, const char* function) {
    for (int index = 0; index < users->count; index++) {
        if (strcmp(users->functions[index], function) == 0) {
            return;
        }
    }

    if (users->count < MAX_USERS) {
        snprintf(users->functions[users->count++], NAME_CAPACITY, "%s", function);
    }
}

// A line: class, offset, type, file, uses, functions, the first few functions (tabs between, ; between functions).
static bool read_users(const wchar_t* workspace, const char* owner, uint32_t offset, Users* users) {
    wchar_t full[PATH_CAPACITY];
    wchar_t path[PATH_CAPACITY];
    char line[LINE_CAPACITY];

    GetFullPathNameW(workspace, PATH_CAPACITY, full, NULL);
    swprintf(path, PATH_CAPACITY, L"%ls\\source\\unnamed_fields.tsv", full);

    FILE* file = _wfopen(path, L"rb");

    if (file == NULL) {
        fwprintf(stderr, L"No %ls: it's written by mt2sdk decompile.\n", path);

        return false;
    }

    while (fgets(line, sizeof line, file) != NULL) {
        char* columns[7];
        int count = 0;

        line[strcspn(line, "\r\n")] = '\0';

        for (char* column = strtok(line, "\t"); column != NULL && count < 7; column = strtok(NULL, "\t")) {
            columns[count++] = column;
        }

        if (count < 7 || !same_class(owner, columns[0]) || strtoul(columns[1], NULL, 16) != offset) {
            continue;
        }

        if (atoi(columns[4]) > users->busiest_uses) {
            users->busiest_uses = atoi(columns[4]);
            snprintf(users->busiest_file, sizeof users->busiest_file, "%s", columns[3]);
        }

        for (char* function = strtok(columns[6], ";"); function != NULL; function = strtok(NULL, ";")) {
            add_user(users, function);
        }
    }

    fclose(file);

    return true;
}

// [rsi+0x6c], or [rcx] for 0: a field of an object a register points at. The stack and rip-relative globals aren't.
static bool reaches_offset(const char* text, uint32_t offset) {
    const char* open = strchr(text, '[');
    const char* close = open != NULL ? strchr(open, ']') : NULL;
    char inside[64];
    char wanted[24];

    if (close == NULL || close - open >= (long long)sizeof inside) {
        return false;
    }

    snprintf(inside, sizeof inside, "%.*s", (int)(close - open - 1), open + 1);

    if (strstr(inside, "rsp") != NULL || strstr(inside, "rbp") != NULL || strstr(inside, "rip") != NULL) {
        return false;
    }

    if (offset == 0) {
        return strchr(inside, '+') == NULL && strchr(inside, '-') == NULL;
    }

    snprintf(wanted, sizeof wanted, "+0x%x", offset);

    size_t length = strlen(inside);
    size_t wanted_length = strlen(wanted);

    return length > wanted_length && _stricmp(inside + length - wanted_length, wanted) == 0;
}

// The first instruction in one of the users that reads or writes the field: the seen line. Empty when none does
// directly (the field is reached through a copy, or in code inlined from elsewhere).
static void find_seen(const wchar_t* exe, const Users* users, uint32_t offset, char* seen, size_t capacity) {
    wchar_t exe_path[GAME_PATH_CAPACITY];
    GameImage image;

    seen[0] = '\0';

    if (!game_exe_load_symbols(exe, exe_path) || !game_image_load(exe_path, &image)) {
        return;
    }

    for (int user = 0; user < users->count && seen[0] == '\0'; user++) {
        uint32_t rva = 0;
        Function function;
        Instruction instruction;

        if (!place_find(users->functions[user], &rva) || !place_function_at(rva, &function)) {
            continue;
        }

        for (uint32_t at = function.start; at < function.end; at += instruction.length) {
            if (!disassembly_decode(&image, at, &instruction)) {
                break;
            }

            if (reaches_offset(instruction.text, offset)) {
                place_name(at, seen, capacity);
                break;
            }
        }
    }

    game_image_free(&image);
}

// engine/ for VectorStorm's vs classes, std/ for the standard library's, game/ for the rest; named after the class
// without its template arguments.
static void new_mapping_path(const wchar_t* folder, const char* owner, wchar_t* path) {
    const char* name = strncmp(owner, "std::", 5) == 0 ? owner + 5 : owner;
    const char* place = strncmp(owner, "std::", 5) == 0 ? "std" : strncmp(owner, "vs", 2) == 0 ? "engine" : "game";
    wchar_t wide[NAME_CAPACITY];
    char base[NAME_CAPACITY];

    snprintf(base, sizeof base, "%.*s", (int)strcspn(name, "<"), name);
    MultiByteToWideChar(CP_UTF8, 0, base, -1, wide, NAME_CAPACITY);
    swprintf(path, PATH_CAPACITY, L"%ls\\%hs\\%ls.mapping", folder, place, wide);
}

static char* read_text(const wchar_t* path) {
    FILE* file = _wfopen(path, L"rb");

    if (file == NULL) {
        return calloc(1, 1);
    }

    fseek(file, 0, SEEK_END);

    long size = ftell(file);
    char* text = malloc((size_t)size + 1);

    fseek(file, 0, SEEK_SET);
    text[fread(text, 1, (size_t)size, file)] = '\0';
    fclose(file);

    return text;
}

// Puts the field in its class's block, before the first field further in, so the file reads in offset order.
static bool insert_field(const wchar_t* path, const char* owner, const char* addition) {
    char* text = read_text(path);
    char** lines = malloc(MAX_FILE_LINES * sizeof *lines);
    int count = 0;
    char class_line[NAME_CAPACITY + 8];

    for (char* line = text; *line != '\0' && count < MAX_FILE_LINES;) {
        char* end = strchr(line, '\n');
        char* next = end != NULL ? end + 1 : line + strlen(line);

        if (end != NULL) {
            *end = '\0';
        }

        line[strcspn(line, "\r")] = '\0';
        lines[count++] = line;
        line = next;
    }

    snprintf(class_line, sizeof class_line, "class %s", owner);

    int start = -1;

    for (int index = 0; index < count && start < 0; index++) {
        if (strcmp(lines[index], class_line) == 0) {
            start = index;
        }
    }

    int insert_at = count;
    uint32_t offset = (uint32_t)strtoul(addition + strlen("    field "), NULL, 16);

    if (start >= 0) {
        insert_at = start + 1;

        for (int index = start + 1; index < count && (lines[index][0] == ' ' || lines[index][0] == '\0'); index++) {
            if (strncmp(lines[index], "    field ", 10) == 0 && strtoul(lines[index] + 10, NULL, 16) > offset) {
                break;
            }

            if (lines[index][0] != '\0') {
                insert_at = index + 1;
            }
        }
    }

    FILE* file = _wfopen(path, L"wb");

    if (file == NULL) {
        fwprintf(stderr, L"Couldn't write %ls\n", path);
        free(lines);
        free(text);

        return false;
    }

    for (int index = 0; index < insert_at; index++) {
        fprintf(file, "%s\n", lines[index]);
    }

    if (start < 0) {
        fprintf(file, "%s%s\n", count > 0 ? "\n" : "", class_line);
    }

    fputs(addition, file);

    for (int index = insert_at; index < count; index++) {
        fprintf(file, "%s\n", lines[index]);
    }

    fclose(file);
    free(lines);
    free(text);

    return true;
}

// The file the class is in, or where a new one goes. False when the class already has a field there or by that name.
static bool mapping_file_for(const wchar_t* folder, const char* owner, uint32_t offset, const char* name, wchar_t* path) {
    Mappings mappings;
    bool free_place = true;

    mappings_load(folder, &mappings);
    new_mapping_path(folder, owner, path);

    for (int index = 0; index < mappings.entry_count; index++) {
        const Entry* entry = &mappings.entries[index];

        if (entry->kind != ENTRY_CLASS || strcmp(entry->name, owner) != 0) {
            continue;
        }

        swprintf(path, PATH_CAPACITY, L"%ls\\%hs", folder, entry->file);

        for (int member = 0; member < entry->member_count; member++) {
            const Entry* field = &entry->members[member];

            if (field->kind == ENTRY_FIELD && (field->offset == offset || strcmp(field->name, name) == 0)) {
                fprintf(stderr, "%s already has %s at 0x%x (%s line %d)\n", owner, field->name, field->offset, entry->file, field->line);
                free_place = false;
            }
        }
    }

    mappings_free(&mappings);

    return free_place;
}


int command_mappings_name(const wchar_t* exe, const wchar_t* workspace, const wchar_t* folder, const wchar_t* ghidra,
    const wchar_t* written_class, const wchar_t* written_offset, const wchar_t* written_name, const wchar_t* written_type) {
    char owner[NAME_CAPACITY];
    char name[NAME_CAPACITY];
    char type[NAME_CAPACITY];
    char seen[PLACE_CAPACITY];
    char addition[NAME_CAPACITY * 2 + PLACE_CAPACITY];
    wchar_t path[PATH_CAPACITY];
    wchar_t only_file[PATH_CAPACITY];
    Users users = { 0 };

    WideCharToMultiByte(CP_UTF8, 0, written_class, -1, owner, sizeof owner, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, written_name, -1, name, sizeof name, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, written_type, -1, type, sizeof type, NULL, NULL);

    uint32_t offset = (uint32_t)wcstoul(written_offset, NULL, 16);

    if (!read_users(workspace, owner, offset, &users) || !mapping_file_for(folder, owner, offset, name, path)) {
        return 1;
    }

    find_seen(exe, &users, offset, seen, sizeof seen);

    int length = snprintf(addition, sizeof addition, "    field 0x%x %s %s\n", offset, name, type);

    if (seen[0] != '\0') {
        snprintf(addition + length, sizeof addition - (size_t)length, "        seen %s\n", seen);
    }

    if (!insert_field(path, owner, addition)) {
        return 1;
    }

    wprintf(L"Added to %ls:\n%hs", path, addition);

    if (seen[0] == '\0') {
        printf("No seen line: none of the functions using it reaches +0x%x directly. Add one from mt2sdk dis.\n", offset);
    }

    printf("Add a doc line under it: what it holds, in a sentence.\n\n");

    if (command_mappings_check(exe, folder) != 0) {
        return 1;
    }

    if (users.busiest_file[0] == '\0') {
        return 0;
    }

    MultiByteToWideChar(CP_UTF8, 0, users.busiest_file, -1, only_file, PATH_CAPACITY);
    wprintf(L"\nRebuilding %ls, where it's used most:\n", only_file);

    return command_decompile(exe, workspace, ghidra, folder, false, only_file);
}
