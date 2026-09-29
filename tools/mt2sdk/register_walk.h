#ifndef MT2SDK_REGISTER_WALK_H
#define MT2SDK_REGISTER_WALK_H

#include <stdbool.h>
#include <stdint.h>

#include "disassembly.h"
#include "game_image.h"

#define REGISTER_WALK_REGISTERS 512

// What a walk through one function, instruction by instruction, knows each register holds: an address in the image
// as rva + 1, or 0 when it doesn't know. Enough to follow static setup code, which loads addresses and passes them on.
typedef struct RegisterWalk {
    uint64_t values[REGISTER_WALK_REGISTERS];
} RegisterWalk;

void register_walk_start(RegisterWalk* walk);

// Updates what the registers hold after the instruction (lea, mov, add, and calls, which forget the registers a call
// may change).
void register_walk_step(RegisterWalk* walk, const GameImage* image, const Instruction* instruction);

// The address a register holds, if known.
bool register_walk_address(const RegisterWalk* walk, int register_number, uint32_t* rva);

#endif
