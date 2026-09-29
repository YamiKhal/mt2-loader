#include "class_sizes.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/core/symbols.h"
#include "disassembly.h"
#include "places.h"

// operator new's result goes to the constructor within a few instructions (mov rcx, rax; maybe a spill or two).
#define MAX_STEPS_AFTER_NEW 12
#define MAX_DISTINCT_SIZES 8
#define MAX_TRACKED_REGISTERS 512
#define LOOKAHEAD 8

typedef struct Constructor {
    uint32_t rva;
    int class_index;
} Constructor;

typedef struct CallSite {
    uint32_t from;
    int class_index;
} CallSite;

typedef struct Collector {
    const char* prefix;
    int class_index;
    Constructor* constructors;
    int count;
    int capacity;
} Collector;

typedef struct SiteSearch {
    const Constructor* constructors;
    int constructor_count;
    CallSite* sites;
    int count;
    int capacity;
} SiteSearch;

typedef struct Tally {
    uint32_t sizes[MAX_DISTINCT_SIZES];
    int counts[MAX_DISTINCT_SIZES];
    int distinct;
} Tally;


static bool grow(void** items, int* capacity, int needed, size_t item_size) {
    if (needed <= *capacity) {
        return true;
    }

    int grown = *capacity == 0 ? 64 : *capacity * 2;
    void* resized = realloc(*items, (size_t)grown * item_size);

    if (resized == NULL) {
        return false;
    }

    *items = resized;
    *capacity = grown;

    return true;
}

static void collect_constructor(size_t index, const char* readable, void* opaque) {
    Collector* collector = opaque;

    if (strncmp(readable, collector->prefix, strlen(collector->prefix)) != 0) {
        return;
    }

    if (grow((void**)&collector->constructors, &collector->capacity, collector->count + 1, sizeof(Constructor))) {
        collector->constructors[collector->count++] = (Constructor){ symbols_rva_of(index), collector->class_index };
    }
}

static int compare_constructors(const void* left, const void* right) {
    uint32_t left_rva = ((const Constructor*)left)->rva;
    uint32_t right_rva = ((const Constructor*)right)->rva;

    return left_rva < right_rva ? -1 : left_rva > right_rva;
}

static const Constructor* constructor_at(const SiteSearch* search, uint32_t rva) {
    Constructor key = { rva, 0 };

    return bsearch(&key, search->constructors, (size_t)search->constructor_count, sizeof key, compare_constructors);
}

static void collect_site(uint32_t from, const Reference* reference, void* opaque) {
    SiteSearch* search = opaque;
    const Constructor* constructor = reference->kind == REFERENCE_CALL ? constructor_at(search, reference->target) : NULL;

    if (constructor != NULL && grow((void**)&search->sites, &search->capacity, search->count + 1, sizeof(CallSite))) {
        search->sites[search->count++] = (CallSite){ from, constructor->class_index };
    }
}

// Which registers hold the pointer operator new just returned (a copy of it, not a place inside it).
static void follow_new_object(const Instruction* instruction, bool* holds_new) {
    int destination = instruction->destination_register;

    if (destination <= 0 || destination >= MAX_TRACKED_REGISTERS) {
        return;
    }

    bool copies_register = instruction->operation == OPERATION_MOV && instruction->reference_count == 0
        && instruction->source_register > 0 && instruction->source_register < MAX_TRACKED_REGISTERS;
    holds_new[destination] = copies_register && holds_new[instruction->source_register];
}

// After the constructor call, two things show the new object is more than the class: a jump back to before the call
// (an array, built one element per turn), or a write into the object (its other members: the class is only its first
// member). holds_new says which registers still hold the object.
static bool object_is_bigger(const GameImage* image, uint32_t site, const Function* function, const bool* holds_new) {
    Instruction instruction;
    uint32_t rva = site;

    for (int step = 0; step < LOOKAHEAD && rva < function->end && disassembly_decode(image, rva, &instruction); step++) {
        for (int index = 0; index < instruction.reference_count; index++) {
            const Reference* reference = &instruction.references[index];

            if (reference->kind == REFERENCE_JUMP && reference->target <= site && reference->target >= function->start) {
                return true;
            }
        }

        int base = instruction.written_memory_base;

        if (step > 0 && base > 0 && base < MAX_TRACKED_REGISTERS && holds_new[base] && !disassembly_is_volatile_register(base)) {
            return true;
        }

        rva += instruction.length;
    }

    return false;
}

// Walks the function up to the call: "mov ecx, size; call operator new; mov rcx, rax; call constructor" gives the
// size. The constructor must get the new object itself: one called on a place inside it builds a member.
static uint32_t size_at_site(const GameImage* image, uint32_t site, uint32_t operator_new) {
    Function function;
    Instruction instruction;
    uint32_t argument = 0;
    uint32_t pending = 0;
    int steps_since_new = MAX_STEPS_AFTER_NEW + 1;
    bool holds_new[MAX_TRACKED_REGISTERS] = { false };

    if (!place_function_at(site, &function)) {
        return 0;
    }

    for (uint32_t rva = function.start; rva < site; rva += instruction.length) {
        if (!disassembly_decode(image, rva, &instruction)) {
            return 0;
        }

        steps_since_new++;

        if (instruction.sets_first_argument) {
            argument = instruction.first_argument;
        }

        if (instruction.operation != OPERATION_CALL) {
            follow_new_object(&instruction, holds_new);
            continue;
        }

        bool is_new = instruction.reference_count > 0 && instruction.references[0].target == operator_new;
        pending = is_new ? argument : 0;
        steps_since_new = is_new ? 0 : MAX_STEPS_AFTER_NEW + 1;

        for (int index = 0; index < MAX_TRACKED_REGISTERS; index++) {
            holds_new[index] = holds_new[index] && !disassembly_is_volatile_register(index);
        }

        holds_new[disassembly_register_rax()] = is_new;
    }

    bool gets_new_object = holds_new[disassembly_register_rcx()];

    return steps_since_new <= MAX_STEPS_AFTER_NEW && gets_new_object && !object_is_bigger(image, site, &function, holds_new) ? pending : 0;
}

static void count_size(Tally* tally, uint32_t size) {
    for (int index = 0; index < tally->distinct; index++) {
        if (tally->sizes[index] == size) {
            tally->counts[index]++;

            return;
        }
    }

    if (tally->distinct < MAX_DISTINCT_SIZES) {
        tally->sizes[tally->distinct] = size;
        tally->counts[tally->distinct++] = 1;
    }
}

static CodeSize most_common(const Tally* tally) {
    int best = -1;

    for (int index = 0; index < tally->distinct; index++) {
        if (best < 0 || tally->counts[index] > tally->counts[best]) {
            best = index;
        }
    }

    return best < 0 ? (CodeSize){ 0, 0 } : (CodeSize){ tally->sizes[best], tally->counts[best] };
}


void class_sizes_from_code(const GameImage* image, const char* const* names, int count, CodeSize* sizes) {
    SymbolMatch operator_new = symbols_find("operator new(unsigned long long)");
    Collector collector = { 0 };
    char prefix[PLACE_CAPACITY];

    memset(sizes, 0, (size_t)count * sizeof *sizes);

    if (operator_new.result != SYMBOL_FOUND) {
        return;
    }

    for (int index = 0; index < count; index++) {
        const char* last_part = strrchr(names[index], ':');
        last_part = last_part != NULL ? last_part + 1 : names[index];

        if (strchr(names[index], '<') != NULL) {
            continue;
        }

        snprintf(prefix, sizeof prefix, "%s::%s(", names[index], last_part);
        collector.prefix = prefix;
        collector.class_index = index;
        symbols_each_containing(prefix, collect_constructor, &collector);
    }

    qsort(collector.constructors, (size_t)collector.count, sizeof(Constructor), compare_constructors);

    SiteSearch search = { collector.constructors, collector.count, NULL, 0, 0 };

    if (collector.count > 0) {
        disassembly_each_reference(image, collect_site, &search);
    }

    Tally* tallies = calloc((size_t)count, sizeof *tallies);

    for (int index = 0; index < search.count && tallies != NULL; index++) {
        uint32_t size = size_at_site(image, search.sites[index].from, operator_new.rva);

        if (size > 0) {
            count_size(&tallies[search.sites[index].class_index], size);
        }
    }

    for (int index = 0; index < count && tallies != NULL; index++) {
        sizes[index] = most_common(&tallies[index]);
    }

    free(tallies);
    free(search.sites);
    free(collector.constructors);
}

CodeSize* class_sizes_for_mappings(const GameImage* image, const Mappings* mappings) {
    const char** names = calloc((size_t)mappings->entry_count + 1, sizeof *names);
    CodeSize* sizes = calloc((size_t)mappings->entry_count + 1, sizeof *sizes);

    if (names == NULL || sizes == NULL) {
        free(names);
        free(sizes);

        return NULL;
    }

    // What isn't a class gets a name no constructor has.
    for (int index = 0; index < mappings->entry_count; index++) {
        const Entry* entry = &mappings->entries[index];
        names[index] = entry->kind == ENTRY_CLASS ? entry->name : "<none>";
    }

    class_sizes_from_code(image, names, mappings->entry_count, sizes);
    free(names);

    return sizes;
}
