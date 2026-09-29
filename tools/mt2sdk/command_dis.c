#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "commands.h"
#include "disassembly.h"
#include "game_exe.h"
#include "game_image.h"
#include "places.h"

// A function without a next symbol (the last in the exe) is read this far at most.
#define LONGEST_FUNCTION 0x10000
#define MAX_LISTED 400

typedef enum UseGroup {
    USE_CALL,
    USE_TEXT,
    USE_GLOBAL,
    USE_NUMBER,
    USE_GROUP_COUNT,
} UseGroup;

typedef struct Use {
    char name[PLACE_CAPACITY];
    unsigned kinds;
} Use;

typedef struct UseList {
    Use items[MAX_LISTED];
    int count;
} UseList;

static const char* const GROUP_TITLES[USE_GROUP_COUNT] = { "calls", "texts", "globals", "numbers" };


static bool open_function(const wchar_t* exe, const wchar_t* written, GameImage* image, Function* function) {
    wchar_t path[GAME_PATH_CAPACITY];
    char name[PLACE_CAPACITY];
    uint32_t rva = 0;

    WideCharToMultiByte(CP_UTF8, 0, written, -1, name, sizeof name, NULL, NULL);

    if (!game_exe_load_symbols(exe, path) || !place_find(name, &rva) || !game_image_load(path, image)) {
        return false;
    }

    if (!place_function_at(rva, function)) {
        fprintf(stderr, "No symbol holds 0x%llx\n", PREFERRED_BASE + rva);
        game_image_free(image);

        return false;
    }

    if (function->end <= function->start || function->end - function->start > LONGEST_FUNCTION) {
        function->end = function->start + LONGEST_FUNCTION;
    }

    if (!game_image_is_code(image, function->start)) {
        fprintf(stderr, "%s isn't code: it's data. See who uses it with: mt2sdk refs \"%s\"\n", function->name, function->name);
        game_image_free(image);

        return false;
    }

    return true;
}

static void print_heading(const Function* function) {
    printf("%s    0x%llx, %u bytes\n", function->name, PREFERRED_BASE + function->start, function->end - function->start);
}

static void note_of(const GameImage* image, const Instruction* instruction, const Function* function, char* note, size_t capacity) {
    size_t used = 0;
    note[0] = '\0';

    for (int index = 0; index < instruction->reference_count; index++) {
        char described[PLACE_CAPACITY];
        place_describe(image, &instruction->references[index], function, described, sizeof described);

        if (described[0] != '\0' && used < capacity) {
            used += (size_t)snprintf(note + used, capacity - used, "%s%s", used > 0 ? ", " : "", described);
        }
    }

    if (instruction->inline_text[0] != '\0' && used < capacity) {
        snprintf(note + used, capacity - used, "%s'%s'", used > 0 ? ", " : "", instruction->inline_text);
    }
}

static void add_use(UseList* list, const char* name, ReferenceKind kind) {
    for (int index = 0; index < list->count; index++) {
        if (strcmp(list->items[index].name, name) == 0) {
            list->items[index].kinds |= 1u << kind;

            return;
        }
    }

    if (list->count < MAX_LISTED) {
        Use* use = &list->items[list->count++];
        snprintf(use->name, sizeof use->name, "%s", name);
        use->kinds = 1u << kind;
    }
}

static UseGroup group_of(const GameImage* image, const Reference* reference, const char* described) {
    if (game_image_is_code(image, reference->target)) {
        return USE_CALL;
    }

    if (described[0] == '"') {
        return USE_TEXT;
    }

    return reference->float_size != 0 ? USE_NUMBER : USE_GLOBAL;
}

static void collect_uses(const GameImage* image, const Instruction* instruction, const Function* function, UseList* groups) {
    for (int index = 0; index < instruction->reference_count; index++) {
        const Reference* reference = &instruction->references[index];
        char described[PLACE_CAPACITY];

        place_describe(image, reference, function, described, sizeof described);

        if (described[0] != '\0' && described[0] != '+') {
            add_use(&groups[group_of(image, reference, described)], described, reference->kind);
        }
    }

    if (instruction->inline_text[0] != '\0') {
        char quoted[32];
        snprintf(quoted, sizeof quoted, "'%s'", instruction->inline_text);
        add_use(&groups[USE_TEXT], quoted, REFERENCE_READ);
    }
}

static void print_kinds(unsigned kinds) {
    const ReferenceKind shown[] = { REFERENCE_READ, REFERENCE_WRITE, REFERENCE_ADDRESS };
    bool first = true;

    for (size_t index = 0; index < sizeof shown / sizeof shown[0]; index++) {
        if (kinds & (1u << shown[index])) {
            printf("%s%s", first ? "    (" : ", ", reference_kind_word(shown[index]));
            first = false;
        }
    }

    printf("%s\n", first ? "" : ")");
}

static void print_uses(const UseList* groups) {
    int listed = 0;

    for (int group = 0; group < USE_GROUP_COUNT; group++) {
        if (groups[group].count == 0) {
            continue;
        }

        printf("%s\n", GROUP_TITLES[group]);
        listed += groups[group].count;

        for (int index = 0; index < groups[group].count; index++) {
            const Use* use = &groups[group].items[index];
            printf("    %s", use->name);

            if (group == USE_GLOBAL) {
                print_kinds(use->kinds);
            } else {
                printf("\n");
            }
        }
    }

    if (listed == 0) {
        printf("It calls nothing and uses no texts, globals or numbers. Its code: mt2sdk dis\n");
    }
}


int command_dis(const wchar_t* exe, const wchar_t* written) {
    GameImage image;
    Function function;

    if (!open_function(exe, written, &image, &function)) {
        return 1;
    }

    print_heading(&function);

    Instruction instruction;
    char note[PLACE_CAPACITY * 2];

    for (uint32_t rva = function.start; rva < function.end; rva += instruction.length) {
        if (!disassembly_decode(&image, rva, &instruction)) {
            printf("  +0x%-5x 0x%llx   (not an instruction)\n", rva - function.start, PREFERRED_BASE + rva);
            instruction.length = 1;
            continue;
        }

        note_of(&image, &instruction, &function, note, sizeof note);

        if (note[0] == '\0') {
            printf("  +0x%-5x 0x%llx   %s\n", rva - function.start, PREFERRED_BASE + rva, instruction.text);
        } else {
            printf("  +0x%-5x 0x%llx   %-40s ; %s\n", rva - function.start, PREFERRED_BASE + rva, instruction.text, note);
        }
    }

    game_image_free(&image);

    return 0;
}

int command_uses(const wchar_t* exe, const wchar_t* written) {
    GameImage image;
    Function function;

    if (!open_function(exe, written, &image, &function)) {
        return 1;
    }

    UseList* groups = calloc(USE_GROUP_COUNT, sizeof *groups);
    Instruction instruction;

    for (uint32_t rva = function.start; rva < function.end && groups != NULL; rva += instruction.length) {
        if (!disassembly_decode(&image, rva, &instruction)) {
            instruction.length = 1;
            continue;
        }

        collect_uses(&image, &instruction, &function, groups);
    }

    print_heading(&function);

    if (groups != NULL) {
        print_uses(groups);
    }

    free(groups);
    game_image_free(&image);

    return 0;
}
