#include "disassembly.h"

#include <string.h>

#include "../../src/core/symbols.h"
#include "../../third_party/zydis/Zydis.h"

#define LONGEST_INSTRUCTION 15

typedef struct Disassembler {
    bool ready;
    ZydisDecoder decoder;
    ZydisFormatter formatter;
} Disassembler;

static Disassembler disassembler;


static void prepare(void) {
    if (disassembler.ready) {
        return;
    }

    ZydisDecoderInit(&disassembler.decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    ZydisFormatterInit(&disassembler.formatter, ZYDIS_FORMATTER_STYLE_INTEL);
    ZydisFormatterSetProperty(&disassembler.formatter, ZYDIS_FORMATTER_PROP_HEX_UPPERCASE, ZYAN_FALSE);
    ZydisFormatterSetProperty(&disassembler.formatter, ZYDIS_FORMATTER_PROP_ADDR_PADDING_ABSOLUTE, ZYDIS_PADDING_DISABLED);
    ZydisFormatterSetProperty(&disassembler.formatter, ZYDIS_FORMATTER_PROP_DISP_PADDING, ZYDIS_PADDING_DISABLED);
    ZydisFormatterSetProperty(&disassembler.formatter, ZYDIS_FORMATTER_PROP_IMM_PADDING, ZYDIS_PADDING_DISABLED);
    disassembler.ready = true;
}

static size_t available_at(const GameImage* image, uint32_t rva, const uint8_t** bytes) {
    for (size_t size = LONGEST_INSTRUCTION; size > 0; size--) {
        *bytes = game_image_at(image, rva, size);

        if (*bytes != NULL) {
            return size;
        }
    }

    return 0;
}

static uint8_t float_size_of(const ZydisDecodedOperand* operand) {
    if (operand->element_type != ZYDIS_ELEMENT_TYPE_FLOAT32 && operand->element_type != ZYDIS_ELEMENT_TYPE_FLOAT64) {
        return 0;
    }

    return operand->element_size == 64 ? 8 : 4;
}

static bool reference_of(const ZydisDecodedInstruction* decoded, const ZydisDecodedOperand* operand, uint32_t rva,
    Reference* reference) {
    bool is_branch = operand->type == ZYDIS_OPERAND_TYPE_IMMEDIATE && operand->imm.is_relative;
    bool is_rip_memory = operand->type == ZYDIS_OPERAND_TYPE_MEMORY && operand->mem.base == ZYDIS_REGISTER_RIP;
    ZyanU64 target = 0;

    if ((!is_branch && !is_rip_memory) || !ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(decoded, operand, PREFERRED_BASE + rva, &target))) {
        return false;
    }

    if (target < PREFERRED_BASE || target - PREFERRED_BASE > UINT32_MAX) {
        return false;
    }

    reference->target = (uint32_t)(target - PREFERRED_BASE);
    reference->float_size = float_size_of(operand);

    if (is_branch) {
        reference->kind = decoded->mnemonic == ZYDIS_MNEMONIC_CALL ? REFERENCE_CALL : REFERENCE_JUMP;
    } else if (decoded->mnemonic == ZYDIS_MNEMONIC_LEA) {
        reference->kind = REFERENCE_ADDRESS;
    } else {
        reference->kind = (operand->actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) ? REFERENCE_WRITE : REFERENCE_READ;
    }

    return true;
}

static void add_references(const ZydisDecodedInstruction* decoded, const ZydisDecodedOperand* operands, uint32_t rva,
    Instruction* instruction) {
    instruction->reference_count = 0;

    for (int index = 0; index < decoded->operand_count_visible; index++) {
        Reference reference;

        if (instruction->reference_count < DISASSEMBLY_MAX_REFERENCES && reference_of(decoded, &operands[index], rva, &reference)) {
            instruction->references[instruction->reference_count++] = reference;
        }
    }
}

// An immediate whose bytes are all letters (trailing zeros aside) is most likely a piece of text.
static void find_inline_text(const ZydisDecodedInstruction* decoded, const ZydisDecodedOperand* operands, char* text) {
    text[0] = '\0';

    for (int index = 0; index < decoded->operand_count_visible; index++) {
        const ZydisDecodedOperand* operand = &operands[index];

        if (operand->type != ZYDIS_OPERAND_TYPE_IMMEDIATE || operand->imm.is_relative || operand->size < 32) {
            continue;
        }

        uint64_t value = operand->imm.value.u;
        int length = 0;

        for (; length < operand->size / 8 && ((value >> (length * 8)) & 0xff) != 0; length++) {
            uint8_t character = (uint8_t)(value >> (length * 8));

            if (character < 32 || character >= 127) {
                return;
            }

            text[length] = (char)character;
        }

        bool rest_is_zero = length == 8 || (value >> (length * 8)) == 0;
        text[length >= 3 && rest_is_zero ? length : 0] = '\0';

        return;
    }
}


static int register_of(const ZydisDecodedOperand* operand) {
    if (operand->type != ZYDIS_OPERAND_TYPE_REGISTER) {
        return 0;
    }

    return ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, operand->reg.value);
}

static void describe_operands(const ZydisDecodedInstruction* decoded, const ZydisDecodedOperand* operands, Instruction* instruction) {
    bool two_operands = decoded->operand_count_visible >= 2;

    switch (decoded->mnemonic) {
    case ZYDIS_MNEMONIC_MOV:
        instruction->operation = OPERATION_MOV;
        break;
    case ZYDIS_MNEMONIC_LEA:
        instruction->operation = OPERATION_LEA;
        break;
    case ZYDIS_MNEMONIC_ADD:
        instruction->operation = OPERATION_ADD;
        break;
    case ZYDIS_MNEMONIC_CALL:
        instruction->operation = OPERATION_CALL;
        break;
    default:
        instruction->operation = OPERATION_OTHER;
        break;
    }

    instruction->destination_register = decoded->operand_count_visible >= 1 ? register_of(&operands[0]) : 0;
    instruction->source_register = two_operands ? register_of(&operands[1]) : 0;
    instruction->has_immediate = two_operands && operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE;
    instruction->immediate = instruction->has_immediate ? operands[1].imm.value.u : 0;

    bool based_memory = two_operands && operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY && operands[1].mem.base != ZYDIS_REGISTER_RIP
        && operands[1].mem.base != ZYDIS_REGISTER_NONE && operands[1].mem.index == ZYDIS_REGISTER_NONE;
    instruction->memory_base = based_memory ? ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, operands[1].mem.base) : 0;
    instruction->memory_displacement = based_memory ? operands[1].mem.disp.value : 0;

    bool writes_based_memory = decoded->operand_count_visible >= 1 && operands[0].type == ZYDIS_OPERAND_TYPE_MEMORY
        && operands[0].mem.base != ZYDIS_REGISTER_RIP && operands[0].mem.base != ZYDIS_REGISTER_NONE
        && (operands[0].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE);
    instruction->written_memory_base = writes_based_memory ? ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, operands[0].mem.base) : 0;
}


bool disassembly_decode(const GameImage* image, uint32_t rva, Instruction* instruction) {
    prepare();

    const uint8_t* bytes = NULL;
    size_t available = available_at(image, rva, &bytes);
    ZydisDecodedInstruction decoded;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

    if (available == 0 || !ZYAN_SUCCESS(ZydisDecoderDecodeFull(&disassembler.decoder, bytes, available, &decoded, operands))) {
        return false;
    }

    instruction->rva = rva;
    instruction->length = decoded.length;
    add_references(&decoded, operands, rva, instruction);
    find_inline_text(&decoded, operands, instruction->inline_text);

    describe_operands(&decoded, operands, instruction);

    bool to_first_argument = instruction->operation == OPERATION_MOV && instruction->has_immediate
        && instruction->destination_register == ZYDIS_REGISTER_RCX && operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER;
    instruction->sets_first_argument = to_first_argument;
    instruction->first_argument = to_first_argument ? (uint32_t)instruction->immediate : 0;

    ZyanStatus formatted = ZydisFormatterFormatInstruction(&disassembler.formatter, &decoded, operands,
        decoded.operand_count_visible, instruction->text, sizeof instruction->text, PREFERRED_BASE + rva, ZYAN_NULL);

    if (!ZYAN_SUCCESS(formatted)) {
        strcpy(instruction->text, "(unreadable)");
    }

    return true;
}

static void each_reference_in(const GameImage* image, uint32_t start, uint32_t end, EachReference each, void* opaque) {
    ZydisDecoderContext context;
    ZydisDecodedInstruction decoded;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    const uint8_t* bytes = NULL;

    for (uint32_t rva = start; rva < end;) {
        size_t available = available_at(image, rva, &bytes);
        available = available > end - rva ? end - rva : available;

        if (available == 0 || !ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&disassembler.decoder, &context, bytes, available, &decoded))) {
            rva++;
            continue;
        }

        // Most instructions refer to nothing; only those get their operands decoded.
        bool refers = (decoded.attributes & ZYDIS_ATTRIB_IS_RELATIVE) != 0;

        if (refers && ZYAN_SUCCESS(ZydisDecoderDecodeOperands(&disassembler.decoder, &context, &decoded, operands, decoded.operand_count_visible))) {
            for (int index = 0; index < decoded.operand_count_visible; index++) {
                Reference reference;

                if (reference_of(&decoded, &operands[index], rva, &reference)) {
                    each(rva, &reference, opaque);
                }
            }
        }

        rva += decoded.length;
    }
}

static void each_code_reference(const GameImage* image, const ImageSection* section, EachReference each, void* opaque) {
    uint32_t section_end = section->rva + section->size;
    uint32_t start = section->rva;

    for (size_t index = 0; index < symbols_count(); index++) {
        uint32_t symbol = symbols_rva_of(index);

        if (symbol <= start || symbol >= section_end) {
            continue;
        }

        each_reference_in(image, start, symbol, each, opaque);
        start = symbol;
    }

    each_reference_in(image, start, section_end, each, opaque);
}

static void each_table_reference(const GameImage* image, const ImageSection* section, EachReference each, void* opaque) {
    uint32_t size = section->size < section->file_size ? section->size : section->file_size;

    for (uint32_t inside = 0; inside + 8 <= size; inside += 8) {
        uint64_t pointer = 0;
        uint32_t target = 0;

        memcpy(&pointer, image->file + section->file_offset + inside, sizeof pointer);

        if (game_image_rva_of_pointer(image, pointer, &target)) {
            Reference reference = { .target = target, .kind = REFERENCE_TABLE };
            each(section->rva + inside, &reference, opaque);
        }
    }
}


void disassembly_each_reference(const GameImage* image, EachReference each, void* opaque) {
    prepare();

    for (int index = 0; index < image->section_count; index++) {
        const ImageSection* section = &image->sections[index];

        if (section->is_code) {
            each_code_reference(image, section, each, opaque);
        } else if (section->file_size > 0) {
            each_table_reference(image, section, each, opaque);
        }
    }
}

bool disassembly_is_volatile_register(int register_number) {
    switch (register_number) {
    case ZYDIS_REGISTER_RAX:
    case ZYDIS_REGISTER_RCX:
    case ZYDIS_REGISTER_RDX:
    case ZYDIS_REGISTER_R8:
    case ZYDIS_REGISTER_R9:
    case ZYDIS_REGISTER_R10:
    case ZYDIS_REGISTER_R11:
        return true;
    default:
        return false;
    }
}

int disassembly_register_rax(void) {
    return ZYDIS_REGISTER_RAX;
}

int disassembly_register_rcx(void) {
    return ZYDIS_REGISTER_RCX;
}

int disassembly_register_rdx(void) {
    return ZYDIS_REGISTER_RDX;
}

const char* reference_kind_word(ReferenceKind kind) {
    switch (kind) {
    case REFERENCE_CALL:
        return "call";
    case REFERENCE_JUMP:
        return "jump";
    case REFERENCE_READ:
        return "read";
    case REFERENCE_WRITE:
        return "write";
    case REFERENCE_ADDRESS:
        return "address";
    case REFERENCE_TABLE:
        return "table";
    }

    return "?";
}
