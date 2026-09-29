#ifndef MT2SDK_DISASSEMBLY_H
#define MT2SDK_DISASSEMBLY_H

#include <stdbool.h>
#include <stdint.h>

#include "game_image.h"

#define DISASSEMBLY_MAX_REFERENCES 3

typedef enum ReferenceKind {
    REFERENCE_CALL,
    REFERENCE_JUMP,
    REFERENCE_READ,
    REFERENCE_WRITE,
    REFERENCE_ADDRESS,
    REFERENCE_TABLE,
} ReferenceKind;

typedef struct Reference {
    uint32_t target;
    ReferenceKind kind;
    // 4 or 8 when the instruction reads the target as a float or a double, else 0.
    uint8_t float_size;
} Reference;

typedef enum Operation {
    OPERATION_OTHER,
    OPERATION_MOV,
    OPERATION_LEA,
    OPERATION_ADD,
    OPERATION_CALL,
} Operation;

// Registers a call may change (Windows x64): what a walk has learned about them is gone after any call.
bool disassembly_is_volatile_register(int register_number);
int disassembly_register_rax(void);
int disassembly_register_rcx(void);
int disassembly_register_rdx(void);

typedef struct Instruction {
    uint32_t rva;
    uint32_t length;
    Operation operation;
    // The register it writes (its first operand) and the one it reads (its second), as 64-bit register numbers, or 0
    // when that operand isn't a register; and a number it holds (mov [x], 0x4b0).
    int destination_register;
    int source_register;
    bool has_immediate;
    uint64_t immediate;
    // For a second operand in memory not relative to rip ([rbx+0x20]): its base register and displacement.
    int memory_base;
    int64_t memory_displacement;
    // For a first operand in memory not relative to rip (mov [rbx+0x60], 0): its base register, or 0.
    int written_memory_base;
    int reference_count;
    Reference references[DISASSEMBLY_MAX_REFERENCES];
    // Up to 8 letters the instruction writes as a number: how GCC builds short texts in place.
    char inline_text[9];
    // mov ecx/rcx, number: the first argument of the next call, like the size given to operator new.
    bool sets_first_argument;
    uint32_t first_argument;
    char text[192];
} Instruction;

typedef void (*EachReference)(uint32_t from, const Reference* reference, void* opaque);

bool disassembly_decode(const GameImage* image, uint32_t rva, Instruction* instruction);

// Every reference in the game's code, one symbol at a time (so the walk starts again at each function), then
// every pointer into the image kept in its data (vtables, tables of functions).
void disassembly_each_reference(const GameImage* image, EachReference each, void* opaque);

const char* reference_kind_word(ReferenceKind kind);

#endif
