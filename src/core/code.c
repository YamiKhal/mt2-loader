#include "code.h"

#include "../../third_party/minhook/src/hde/hde64.h"
#include "memory.h"

#define MAX_INSTRUCTION 15
#define OPCODE_CALL 0xe8
#define OPCODE_JMP 0xe9
#define OPCODE_JMP_SHORT 0xeb
#define OPCODE_TWO_BYTE 0x0f
#define MODRM_RIP_RELATIVE_RM 5
#define MODRM_REGISTER 3
#define REGISTER_STACK_POINTER 4
#define OPCODE_RETURN 0xc3
#define OPCODE_RETURN_POP 0xc2
#define OPCODE_INDIRECT 0xff
#define INDIRECT_CALL 2
#define INDIRECT_CALL_FAR 3
#define INDIRECT_JUMP 4
#define INDIRECT_JUMP_FAR 5
#define OPCODE_ARITHMETIC_IMM32 0x81
#define OPCODE_ARITHMETIC_IMM8 0x83
#define ARITHMETIC_ADD 0
#define ARITHMETIC_SUB 5


// Decodes from a copy, so an unreadable address fails instead of crashing.
static bool decode(const uint8_t* address, hde64s* instruction) {
    uint8_t bytes[MAX_INSTRUCTION];

    if (!memory_read(address, bytes, sizeof bytes)) {
        return false;
    }

    hde64_disasm(bytes, instruction);

    return (instruction->flags & F_ERROR) == 0 && instruction->len > 0;
}

static bool target_of(const uint8_t* address, const hde64s* instruction, const uint8_t** target) {
    const uint8_t* next = address + instruction->len;
    bool is_rip_relative = (instruction->flags & F_MODRM) && instruction->modrm_mod == 0 && instruction->modrm_rm == MODRM_RIP_RELATIVE_RM;
    bool is_call_or_jump = instruction->opcode == OPCODE_CALL || instruction->opcode == OPCODE_JMP;
    bool is_conditional_jump = instruction->opcode == OPCODE_TWO_BYTE && (instruction->opcode2 & 0xf0) == 0x80;
    bool is_short_jump = instruction->opcode == OPCODE_JMP_SHORT || (instruction->opcode & 0xf0) == 0x70;

    if (is_rip_relative) {
        *target = next + (int32_t)instruction->disp.disp32;

        return true;
    }

    if (is_call_or_jump || is_conditional_jump) {
        *target = next + (int32_t)instruction->imm.imm32;

        return true;
    }

    if (is_short_jump) {
        *target = next + (int8_t)instruction->imm.imm8;

        return true;
    }

    return false;
}


static uint32_t immediate_size_of(const hde64s* instruction) {
    if (instruction->flags & F_RELATIVE) {
        return 0;
    }

    if (instruction->flags & F_IMM64) {
        return 8;
    }

    if (instruction->flags & F_IMM32) {
        return 4;
    }

    if (instruction->flags & F_IMM16) {
        return 2;
    }

    return (instruction->flags & F_IMM8) ? 1 : 0;
}

static int64_t immediate_of(const hde64s* instruction, uint32_t size) {
    switch (size) {
    case 8:
        return (int64_t)instruction->imm.imm64;
    case 4:
        return (int32_t)instruction->imm.imm32;
    case 2:
        return (int16_t)instruction->imm.imm16;
    case 1:
        return (int8_t)instruction->imm.imm8;
    default:
        return 0;
    }
}

static bool is_indirect(const hde64s* instruction, uint8_t first, uint8_t second) {
    return instruction->opcode == OPCODE_INDIRECT && (instruction->modrm_reg == first || instruction->modrm_reg == second);
}


bool code_decode(const void* address, Instruction* instruction) {
    hde64s decoded;
    const uint8_t* target = NULL;

    *instruction = (Instruction){ 0 };

    if (!decode(address, &decoded)) {
        return false;
    }

    uint32_t immediate_size = immediate_size_of(&decoded);
    bool is_rip_relative = (decoded.flags & F_MODRM) && decoded.modrm_mod == 0 && decoded.modrm_rm == MODRM_RIP_RELATIVE_RM;
    bool is_arithmetic = decoded.opcode == OPCODE_ARITHMETIC_IMM32 || decoded.opcode == OPCODE_ARITHMETIC_IMM8;

    instruction->length = decoded.len;
    instruction->immediate_size = immediate_size;
    instruction->immediate_offset = immediate_size > 0 ? decoded.len - immediate_size : 0;
    instruction->immediate = immediate_of(&decoded, immediate_size);
    instruction->is_call = decoded.opcode == OPCODE_CALL || is_indirect(&decoded, INDIRECT_CALL, INDIRECT_CALL_FAR);
    instruction->is_jump = decoded.opcode == OPCODE_JMP || decoded.opcode == OPCODE_JMP_SHORT || (decoded.opcode & 0xf0) == 0x70
        || (decoded.opcode == OPCODE_TWO_BYTE && (decoded.opcode2 & 0xf0) == 0x80) || is_indirect(&decoded, INDIRECT_JUMP, INDIRECT_JUMP_FAR);
    instruction->is_return = decoded.opcode == OPCODE_RETURN || decoded.opcode == OPCODE_RETURN_POP;
    instruction->adjusts_stack = is_arithmetic && decoded.modrm_mod == MODRM_REGISTER && decoded.modrm_rm == REGISTER_STACK_POINTER
        && !decoded.rex_b && (decoded.modrm_reg == ARITHMETIC_ADD || decoded.modrm_reg == ARITHMETIC_SUB);

    if (target_of(address, &decoded, &target)) {
        bool short_branch = !is_rip_relative && (decoded.flags & F_IMM8);

        instruction->reference = (void*)target;
        instruction->reference_size = short_branch ? 1 : 4;
        // A rip-relative offset comes before any number in the instruction; a branch's offset is its last bytes.
        instruction->reference_offset = is_rip_relative ? decoded.len - immediate_size - 4 : decoded.len - instruction->reference_size;
    }

    return true;
}

bool code_memory_displacement(const void* address, int64_t* displacement) {
    hde64s decoded;

    if (!decode(address, &decoded) || !(decoded.flags & F_MODRM) || decoded.modrm_mod == MODRM_REGISTER) {
        return false;
    }

    if (decoded.modrm_mod == 0 && decoded.modrm_rm == MODRM_RIP_RELATIVE_RM) {
        return false;
    }

    if (decoded.flags & F_DISP8) {
        *displacement = (int8_t)decoded.disp.disp8;
    } else if (decoded.flags & F_DISP32) {
        *displacement = (int32_t)decoded.disp.disp32;
    } else {
        *displacement = 0;
    }

    return true;
}

size_t code_instruction_length(const void* address) {
    hde64s instruction;

    return decode(address, &instruction) ? instruction.len : 0;
}

void* code_find_reference(const void* start, size_t size, ReferenceTest test, const void* context) {
    const uint8_t* position = start;
    const uint8_t* end = position + size;

    while (position < end) {
        hde64s instruction;
        const uint8_t* target = NULL;

        if (!decode(position, &instruction)) {
            return NULL;
        }

        if (target_of(position, &instruction, &target) && test(target, context)) {
            return (void*)position;
        }

        position += instruction.len;
    }

    return NULL;
}
