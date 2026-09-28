#include "memory.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../common/pe_image.h"


bool memory_read(const void* address, void* buffer, size_t size) {
    SIZE_T read = 0;

    // ReadProcessMemory on this process fails cleanly on an unmapped address, where a plain copy would crash.
    return ReadProcessMemory(GetCurrentProcess(), address, buffer, size, &read) && read == size;
}

bool memory_write(void* address, const void* bytes, size_t size) {
    DWORD old_protection = 0;

    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protection)) {
        return false;
    }

    SIZE_T written = 0;
    bool ok = WriteProcessMemory(GetCurrentProcess(), address, bytes, size, &written) && written == size;
    DWORD ignored = 0;

    VirtualProtect(address, size, old_protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), address, size);

    return ok;
}


static int hex_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }

    character = (char)tolower((unsigned char)character);

    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }

    return -1;
}

// "48 8B ?? 10": hex bytes separated by spaces, ?? or ? for any byte.
bool pattern_parse(const char* text, Pattern* pattern) {
    pattern->length = 0;

    if (text == NULL) {
        return false;
    }

    const char* position = text;

    while (*position != '\0') {
        if (*position == ' ') {
            position++;
            continue;
        }

        if (pattern->length == PATTERN_MAX_BYTES) {
            return false;
        }

        size_t index = pattern->length++;

        if (*position == '?') {
            pattern->wildcard[index] = true;
            pattern->bytes[index] = 0;
            position += position[1] == '?' ? 2 : 1;
            continue;
        }

        int high = hex_value(position[0]);
        int low = position[1] != '\0' ? hex_value(position[1]) : -1;

        if (high < 0 || low < 0) {
            return false;
        }

        pattern->wildcard[index] = false;
        pattern->bytes[index] = (uint8_t)(high * 16 + low);
        position += 2;
    }

    return pattern->length > 0;
}

static bool matches_bytes(const uint8_t* bytes, const Pattern* pattern) {
    for (size_t index = 0; index < pattern->length; index++) {
        if (!pattern->wildcard[index] && bytes[index] != pattern->bytes[index]) {
            return false;
        }
    }

    return true;
}

bool pattern_matches(const void* address, const Pattern* pattern) {
    uint8_t bytes[PATTERN_MAX_BYTES];

    return memory_read(address, bytes, pattern->length) && matches_bytes(bytes, pattern);
}

void* pattern_scan(HMODULE module, const Pattern* pattern) {
    IMAGE_SECTION_HEADER* text = pe_find_section(module, ".text");

    if (text == NULL || text->Misc.VirtualSize < pattern->length) {
        return NULL;
    }

    const uint8_t* start = (const uint8_t*)module + text->VirtualAddress;
    size_t last = text->Misc.VirtualSize - pattern->length;

    for (size_t offset = 0; offset <= last; offset++) {
        if (matches_bytes(start + offset, pattern)) {
            return (void*)(start + offset);
        }
    }

    return NULL;
}

// Copied first, so a range that runs into unreadable memory fails instead of crashing.
void* pattern_scan_range(const void* start, size_t size, const Pattern* pattern) {
    if (size < pattern->length) {
        return NULL;
    }

    uint8_t* copy = malloc(size);
    void* found = NULL;

    if (copy != NULL && memory_read(start, copy, size)) {
        for (size_t offset = 0; offset + pattern->length <= size && found == NULL; offset++) {
            found = matches_bytes(copy + offset, pattern) ? (uint8_t*)start + offset : NULL;
        }
    }

    free(copy);

    return found;
}
