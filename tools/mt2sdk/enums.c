#include "enums.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/core/symbols.h"
#include "disassembly.h"
#include "places.h"
#include "register_walk.h"

#define CONVERT_PREFIX "std::string ConvertToString<"
// Each word is a std::string, 32 bytes, one after another.
#define WORD_SIZE 0x20
#define LONGEST_CONVERTER 0x400

typedef struct Collector {
    GameEnums* enums;
    int capacity;
} Collector;

typedef struct SetupSearch {
    const GameEnums* enums;
    uint32_t* functions;
    int count;
    int capacity;
} SetupSearch;


static bool grow(void** items, int* capacity, int needed, size_t item_size) {
    if (needed <= *capacity) {
        return true;
    }

    int grown = *capacity == 0 ? 128 : *capacity * 2;
    void* resized = realloc(*items, (size_t)grown * item_size);

    if (resized == NULL) {
        return false;
    }

    *items = resized;
    *capacity = grown;

    return true;
}

// The converter's first reference to data at a symbol's start is the word list it indexes.
static uint32_t word_list_of(const GameImage* image, uint32_t converter) {
    Function function;
    Instruction instruction;

    if (!place_function_at(converter, &function)) {
        return 0;
    }

    uint32_t end = function.end - function.start > LONGEST_CONVERTER ? function.start + LONGEST_CONVERTER : function.end;

    for (uint32_t rva = function.start; rva < end && disassembly_decode(image, rva, &instruction); rva += instruction.length) {
        for (int index = 0; index < instruction.reference_count; index++) {
            const Reference* reference = &instruction.references[index];
            uint32_t start = 0;
            uint32_t size = 0;
            bool data_symbol = !game_image_is_code(image, reference->target) && symbols_extent_at(reference->target, &start, &size)
                && start == reference->target && size >= WORD_SIZE;

            if ((reference->kind == REFERENCE_ADDRESS || reference->kind == REFERENCE_READ) && data_symbol) {
                return reference->target;
            }
        }
    }

    return 0;
}

static void collect_converter(size_t index, const char* readable, void* opaque) {
    Collector* collector = opaque;
    const char* name = readable + strlen(CONVERT_PREFIX);
    const char* end = strstr(name, ">(");

    if (strncmp(readable, CONVERT_PREFIX, strlen(CONVERT_PREFIX)) != 0 || end == NULL) {
        return;
    }

    GameEnums* enums = collector->enums;

    for (int existing = 0; existing < enums->count; existing++) {
        if (strncmp(enums->items[existing].name, name, (size_t)(end - name)) == 0 && enums->items[existing].name[end - name] == '\0') {
            return;
        }
    }

    if (!grow((void**)&enums->items, &collector->capacity, enums->count + 1, sizeof(GameEnum))) {
        return;
    }

    GameEnum* item = &enums->items[enums->count++];
    memset(item, 0, sizeof *item);
    snprintf(item->name, sizeof item->name, "%.*s", (int)(end - name), name);
    item->words = symbols_rva_of(index);
}

static GameEnum* enum_with_word(const GameEnums* enums, uint32_t rva, int* index) {
    for (int item = 0; item < enums->count; item++) {
        GameEnum* game_enum = &enums->items[item];
        uint32_t start = game_enum->words;

        if (start != 0 && rva >= start && rva < start + game_enum->word_capacity * WORD_SIZE && (rva - start) % WORD_SIZE == 0) {
            *index = (int)((rva - start) / WORD_SIZE);

            return game_enum;
        }
    }

    return NULL;
}

static void collect_setup(uint32_t from, const Reference* reference, void* opaque) {
    SetupSearch* search = opaque;
    int index = 0;
    Function function;

    if (reference->kind != REFERENCE_ADDRESS || enum_with_word(search->enums, reference->target, &index) == NULL
        || !place_function_at(from, &function)) {
        return;
    }

    if (search->count > 0 && search->functions[search->count - 1] == function.start) {
        return;
    }

    if (grow((void**)&search->functions, &search->capacity, search->count + 1, sizeof(uint32_t))) {
        search->functions[search->count++] = function.start;
    }
}

// Each word is made by a call like std::string(rcx = &words[i], rdx = "GoToRegion").
static void walk_setup(const GameImage* image, uint32_t start, const GameEnums* enums) {
    Function function;
    Instruction instruction;
    RegisterWalk* walk = malloc(sizeof *walk);

    if (walk == NULL || !place_function_at(start, &function)) {
        free(walk);

        return;
    }

    register_walk_start(walk);

    for (uint32_t rva = function.start; rva < function.end && disassembly_decode(image, rva, &instruction); rva += instruction.length) {
        uint32_t word = 0;
        uint32_t text = 0;
        int index = 0;
        char value[ENUM_VALUE_CAPACITY];

        if (instruction.operation == OPERATION_CALL && register_walk_address(walk, disassembly_register_rcx(), &word)
            && register_walk_address(walk, disassembly_register_rdx(), &text)) {
            GameEnum* game_enum = enum_with_word(enums, word, &index);

            if (game_enum != NULL && game_image_text_at(image, text, value, sizeof value)) {
                snprintf(game_enum->values[index], sizeof game_enum->values[index], "%s", value);
                game_enum->count = index + 1 > game_enum->count ? index + 1 : game_enum->count;
            }
        }

        register_walk_step(walk, image, &instruction);
    }

    free(walk);
}


bool enums_read(const GameImage* image, GameEnums* enums) {
    memset(enums, 0, sizeof *enums);

    Collector collector = { enums, 0 };
    symbols_each_containing("ConvertToString<", collect_converter, &collector);

    for (int index = 0; index < enums->count; index++) {
        GameEnum* game_enum = &enums->items[index];
        uint32_t start = 0;
        uint32_t size = 0;

        game_enum->words = word_list_of(image, game_enum->words);

        if (game_enum->words != 0 && symbols_extent_at(game_enum->words, &start, &size)) {
            uint32_t capacity = size / WORD_SIZE;
            game_enum->word_capacity = capacity < ENUM_MAX_VALUES ? capacity : ENUM_MAX_VALUES;
        }
    }

    SetupSearch search = { .enums = enums };
    disassembly_each_reference(image, collect_setup, &search);

    for (int index = 0; index < search.count; index++) {
        walk_setup(image, search.functions[index], enums);
    }

    free(search.functions);

    // ConvertToString<int>, <float>, <vsColor> format values instead of looking words up: they aren't enums.
    int kept = 0;

    for (int index = 0; index < enums->count; index++) {
        if (enums->items[index].count > 0) {
            enums->items[kept++] = enums->items[index];
        }
    }

    enums->count = kept;

    return enums->count > 0;
}

void enums_free(GameEnums* enums) {
    free(enums->items);
    memset(enums, 0, sizeof *enums);
}

const GameEnum* enums_find(const GameEnums* enums, const char* name) {
    for (int index = 0; index < enums->count; index++) {
        if (strcmp(enums->items[index].name, name) == 0) {
            return &enums->items[index];
        }
    }

    return NULL;
}
