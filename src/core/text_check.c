#include "text_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/pe_image.h"


static bool read_file_range(const wchar_t* path, uint32_t offset, uint32_t length, uint8_t* buffer) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER position = { .QuadPart = offset };
    bool ok = SetFilePointerEx(file, position, NULL, FILE_BEGIN);
    uint32_t total = 0;

    while (ok && total < length) {
        DWORD read = 0;
        ok = ReadFile(file, buffer + total, length - total, &read, NULL) && read > 0;
        total += read;
    }

    CloseHandle(file);

    return ok && total == length;
}

static size_t mask_relocations(HMODULE exe_module, uint32_t text_rva, uint32_t text_length, uint8_t* mask) {
    IMAGE_NT_HEADERS64* nt_headers = pe_nt_headers(exe_module);
    IMAGE_DATA_DIRECTORY relocations = nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    uint8_t* base = (uint8_t*)exe_module;
    uint8_t* block_position = base + relocations.VirtualAddress;
    uint8_t* end = block_position + relocations.Size;
    size_t masked = 0;

    while (relocations.VirtualAddress != 0 && block_position + sizeof(IMAGE_BASE_RELOCATION) <= end) {
        IMAGE_BASE_RELOCATION* block = (IMAGE_BASE_RELOCATION*)block_position;

        if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION)) {
            break;
        }

        size_t entry_count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);
        uint16_t* entries = (uint16_t*)(block + 1);

        for (size_t index = 0; index < entry_count; index++) {
            uint16_t type = entries[index] >> 12;
            uint32_t rva = block->VirtualAddress + (entries[index] & 0x0fff);

            if (type != IMAGE_REL_BASED_DIR64 || rva < text_rva || rva + 8 > text_rva + text_length) {
                continue;
            }

            memset(mask + (rva - text_rva), 1, 8);
            masked += 8;
        }

        block_position += block->SizeOfBlock;
    }

    return masked;
}


bool text_check_run(HMODULE exe_module, const wchar_t* exe_path, TextCheckResult* result) {
    memset(result, 0, sizeof *result);

    IMAGE_SECTION_HEADER* text = pe_find_section(exe_module, ".text");

    if (text == NULL) {
        snprintf(result->problem, sizeof result->problem, "MT2.exe has no .text section");

        return false;
    }

    uint32_t length = text->Misc.VirtualSize < text->SizeOfRawData ? text->Misc.VirtualSize : text->SizeOfRawData;
    uint8_t* file_bytes = malloc(length);
    uint8_t* mask = calloc(length, 1);
    bool ok = file_bytes != NULL && mask != NULL;

    if (!ok) {
        snprintf(result->problem, sizeof result->problem, "not enough memory to compare %u bytes", length);
    }

    if (ok && !read_file_range(exe_path, text->PointerToRawData, length, file_bytes)) {
        snprintf(result->problem, sizeof result->problem, "MT2.exe couldn't be read from disk (error %lu)", GetLastError());
        ok = false;
    }

    if (ok) {
        const uint8_t* memory_bytes = (const uint8_t*)exe_module + text->VirtualAddress;
        result->relocated_bytes = mask_relocations(exe_module, text->VirtualAddress, length, mask);
        result->compared_bytes = length;

        for (uint32_t offset = 0; offset < length; offset++) {
            if (mask[offset] || memory_bytes[offset] == file_bytes[offset]) {
                continue;
            }

            if (result->different_bytes == 0) {
                result->first_difference_rva = text->VirtualAddress + offset;
            }

            result->different_bytes++;
        }
    }

    free(file_bytes);
    free(mask);

    return ok;
}
