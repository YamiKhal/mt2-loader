#ifndef CORE_CODE_H
#define CORE_CODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../sdk/include/mt2loader.h"

typedef bool (*ReferenceTest)(const uint8_t* target, const void* context);

// Decodes the instruction at address: its length, the number written in it, and what it refers to.
bool code_decode(const void* address, Instruction* instruction);

// Length of the x64 instruction at address, or 0 if it can't be read or decoded.
size_t code_instruction_length(const void* address);

// First instruction in [start, start + size) whose rip-relative operand, call, jump or conditional jump
// points at an address the test accepts. Walks instruction by instruction, so start must be the start of one.
void* code_find_reference(const void* start, size_t size, ReferenceTest test, const void* context);

#endif
