#include "register_walk.h"

#include <string.h>


static bool fits(int register_number) {
    return register_number > 0 && register_number < REGISTER_WALK_REGISTERS;
}

static void forget_volatile(RegisterWalk* walk) {
    for (int index = 0; index < REGISTER_WALK_REGISTERS; index++) {
        if (disassembly_is_volatile_register(index)) {
            walk->values[index] = 0;
        }
    }
}

// mov rax, [slot]: MinGW keeps a vtable's or a global's address in an unnamed slot.
static uint64_t address_in_slot(const GameImage* image, uint32_t slot) {
    const uint8_t* bytes = game_image_at(image, slot, sizeof(uint64_t));
    uint64_t pointer = 0;
    uint32_t pointee = 0;

    if (bytes != NULL) {
        memcpy(&pointer, bytes, sizeof pointer);
    }

    return game_image_rva_of_pointer(image, pointer, &pointee) ? (uint64_t)pointee + 1 : 0;
}


void register_walk_start(RegisterWalk* walk) {
    memset(walk, 0, sizeof *walk);
}

void register_walk_step(RegisterWalk* walk, const GameImage* image, const Instruction* instruction) {
    if (instruction->operation == OPERATION_CALL) {
        forget_volatile(walk);

        return;
    }

    int destination = instruction->destination_register;

    if (!fits(destination)) {
        return;
    }

    const Reference* reference = instruction->reference_count > 0 ? &instruction->references[0] : NULL;
    uint64_t* value = &walk->values[destination];

    switch (instruction->operation) {
    case OPERATION_LEA:
        if (reference != NULL && reference->kind == REFERENCE_ADDRESS) {
            *value = (uint64_t)reference->target + 1;
        } else if (fits(instruction->memory_base) && walk->values[instruction->memory_base] != 0) {
            *value = walk->values[instruction->memory_base] + (uint64_t)instruction->memory_displacement;
        } else {
            *value = 0;
        }

        break;
    case OPERATION_MOV:
        if (reference != NULL && reference->kind == REFERENCE_READ) {
            *value = address_in_slot(image, reference->target);
        } else if (reference == NULL && fits(instruction->source_register)) {
            *value = walk->values[instruction->source_register];
        } else if (reference == NULL) {
            *value = 0;
        }

        break;
    case OPERATION_ADD:
        *value = instruction->has_immediate && *value != 0 ? *value + instruction->immediate : 0;
        break;
    default:
        *value = 0;
        break;
    }
}

bool register_walk_address(const RegisterWalk* walk, int register_number, uint32_t* rva) {
    if (!fits(register_number) || walk->values[register_number] == 0) {
        return false;
    }

    *rva = (uint32_t)(walk->values[register_number] - 1);

    return true;
}
