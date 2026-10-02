#include "class_sizes.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../third_party/minhook/src/hde/hde64.h"
#include "../common/pe_image.h"
#include "hooks.h"
#include "symbols.h"

// operator new's result goes to the constructor within a few instructions (mov rcx, rax; maybe a spill or two).
#define MAX_STEPS_AFTER_NEW 12
#define MAX_CONSTRUCTORS 16
#define MAX_DISTINCT_SIZES 8
#define MAX_REMEMBERED 32
#define REGISTER_COUNT 16
#define REGISTER_RAX 0
#define REGISTER_RCX 1
#define OPCODE_CALL 0xe8
#define OPCODE_MOV_ECX_IMM32 0xb9
#define OPCODE_MOV_REGISTER_IMM 0xb8
#define OPCODE_MOV_TO_RM 0x89
#define OPCODE_MOV_FROM_RM 0x8b
#define OPCODE_LEA 0x8d
#define OPCODE_XOR_TO_RM 0x31
#define OPCODE_XOR_FROM_RM 0x33
#define OPCODE_INDIRECT 0xff
#define INDIRECT_CALL 2
#define MODRM_REGISTER 3

typedef struct Search {
    const uint8_t* game_base;
    uint32_t operator_new;
    uint32_t constructors[MAX_CONSTRUCTORS];
    int constructor_count;
    const char* prefix;
    uint32_t sizes[MAX_DISTINCT_SIZES];
    int counts[MAX_DISTINCT_SIZES];
    int distinct;
} Search;

// What a call leaves behind: after operator new, which registers hold the new object, and the size it was asked for.
typedef struct Walk {
    bool holds_new[REGISTER_COUNT];
    uint32_t size_argument;
    uint32_t asked_size;
    int steps_since_new;
} Walk;

typedef struct Remembered {
    char class_name[128];
    size_t size;
} Remembered;

static SRWLOCK lock = SRWLOCK_INIT;
static Remembered remembered[MAX_REMEMBERED];
static int remembered_count = 0;


static void collect_constructor(size_t index, const char* readable, void* opaque) {
    Search* search = opaque;

    if (strncmp(readable, search->prefix, strlen(search->prefix)) == 0 && search->constructor_count < MAX_CONSTRUCTORS) {
        search->constructors[search->constructor_count++] = symbols_rva_of(index);
    }
}

static bool is_constructor(const Search* search, uint32_t rva) {
    for (int index = 0; index < search->constructor_count; index++) {
        if (search->constructors[index] == rva) {
            return true;
        }
    }

    return false;
}

static void count_size(Search* search, uint32_t size) {
    for (int index = 0; index < search->distinct; index++) {
        if (search->sizes[index] == size) {
            search->counts[index]++;

            return;
        }
    }

    if (search->distinct < MAX_DISTINCT_SIZES) {
        search->sizes[search->distinct] = size;
        search->counts[search->distinct++] = 1;
    }
}

static bool is_volatile(int register_number) {
    return register_number <= 2 || (register_number >= 8 && register_number <= 11);
}

// Copies of the new object from register to register; anything else written to a register replaces it.
static void follow_registers(const hde64s* instruction, Walk* walk) {
    int reg = instruction->modrm_reg | (instruction->rex_r << 3);
    int rm = instruction->modrm_rm | (instruction->rex_b << 3);
    bool register_operands = instruction->modrm_mod == MODRM_REGISTER;

    switch (instruction->opcode) {
        case OPCODE_MOV_TO_RM:
            if (register_operands) {
                walk->holds_new[rm] = instruction->rex_w && walk->holds_new[reg];
            }
            break;

        case OPCODE_MOV_FROM_RM:
            walk->holds_new[reg] = register_operands && instruction->rex_w && walk->holds_new[rm];
            break;

        case OPCODE_LEA:
        case OPCODE_XOR_FROM_RM:
            walk->holds_new[reg] = false;
            break;

        case OPCODE_XOR_TO_RM:
            if (register_operands) {
                walk->holds_new[rm] = false;
            }
            break;

        default:
            if ((instruction->opcode & 0xf8) == OPCODE_MOV_REGISTER_IMM) {
                walk->holds_new[(instruction->opcode & 7) | (instruction->rex_b << 3)] = false;
            }
            break;
    }
}

// target is where the call goes, or NULL when the code works it out as it runs.
static void after_call(Search* search, const uint8_t* target, Walk* walk) {
    uint32_t target_rva = target != NULL ? (uint32_t)(target - search->game_base) : 0;
    bool is_new = target != NULL && target_rva == search->operator_new;

    if (target != NULL && is_constructor(search, target_rva) && walk->steps_since_new <= MAX_STEPS_AFTER_NEW
        && walk->holds_new[REGISTER_RCX] && walk->asked_size > 0) {
        count_size(search, walk->asked_size);
    }

    for (int index = 0; index < REGISTER_COUNT; index++) {
        walk->holds_new[index] = walk->holds_new[index] && !is_volatile(index);
    }

    walk->holds_new[REGISTER_RAX] = is_new;
    walk->asked_size = is_new ? walk->size_argument : 0;
    walk->steps_since_new = is_new ? 0 : MAX_STEPS_AFTER_NEW + 1;
    walk->size_argument = 0;
}

// A call another plugin hooked still counts as a call to the game's function.
static const uint8_t* call_target(const uint8_t* call, const hde64s* instruction) {
    void* destination = NULL;

    if (hooks_call_destination(call, &destination)) {
        return destination;
    }

    return call + instruction->len + (int32_t)instruction->imm.imm32;
}

static void walk_function(Search* search, const uint8_t* start, const uint8_t* end) {
    Walk walk = { .steps_since_new = MAX_STEPS_AFTER_NEW + 1 };
    hde64s instruction;

    for (const uint8_t* position = start; position < end; position += instruction.len) {
        hde64_disasm(position, &instruction);

        if ((instruction.flags & F_ERROR) || instruction.len == 0) {
            return;
        }

        walk.steps_since_new++;

        if (instruction.opcode == OPCODE_MOV_ECX_IMM32 && !instruction.rex_b && !instruction.p_66) {
            walk.size_argument = instruction.imm.imm32;
        }

        if (instruction.opcode == OPCODE_CALL) {
            after_call(search, call_target(position, &instruction), &walk);
        } else if (instruction.opcode == OPCODE_INDIRECT && instruction.modrm_reg == INDIRECT_CALL) {
            after_call(search, NULL, &walk);
        } else {
            follow_registers(&instruction, &walk);
        }
    }
}

static size_t search_code(HMODULE game, const char* class_name) {
    Search search = { .game_base = (const uint8_t*)game };
    char prefix[300];
    SymbolMatch operator_new = symbols_find("_Znwy");
    IMAGE_SECTION_HEADER* code = pe_find_section(game, ".text");

    snprintf(prefix, sizeof prefix, "%s::%s(", class_name, class_name);
    search.prefix = prefix;
    symbols_each_containing(prefix, collect_constructor, &search);

    if (operator_new.result != SYMBOL_FOUND || code == NULL || search.constructor_count == 0) {
        return 0;
    }

    search.operator_new = operator_new.rva;
    uint32_t code_start = code->VirtualAddress;
    uint32_t code_end = code->VirtualAddress + code->Misc.VirtualSize;
    size_t count = symbols_count();

    for (size_t index = 0; index < count; index++) {
        uint32_t start = symbols_rva_of(index);
        uint32_t end = index + 1 < count ? symbols_rva_of(index + 1) : code_end;

        if (start >= code_start && start < end && end <= code_end) {
            walk_function(&search, search.game_base + start, search.game_base + end);
        }
    }

    int most = -1;

    for (int index = 0; index < search.distinct; index++) {
        most = most < 0 || search.counts[index] > search.counts[most] ? index : most;
    }

    return most >= 0 ? search.sizes[most] : 0;
}


size_t class_size_of(HMODULE game, const char* class_name) {
    AcquireSRWLockExclusive(&lock);

    for (int index = 0; index < remembered_count; index++) {
        if (strcmp(remembered[index].class_name, class_name) == 0) {
            size_t size = remembered[index].size;
            ReleaseSRWLockExclusive(&lock);

            return size;
        }
    }

    size_t size = search_code(game, class_name);

    if (remembered_count < MAX_REMEMBERED && strlen(class_name) < sizeof remembered[0].class_name) {
        Remembered* entry = &remembered[remembered_count++];
        snprintf(entry->class_name, sizeof entry->class_name, "%s", class_name);
        entry->size = size;
    }

    ReleaseSRWLockExclusive(&lock);

    return size;
}
