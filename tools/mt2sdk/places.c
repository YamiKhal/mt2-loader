#include "places.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/core/symbols.h"

#define TEXT_CAPACITY 200
#define FAR_FROM_NAME 0x1000


static bool parse_address(const char* written, uint32_t* rva) {
    if (strncmp(written, "0x", 2) != 0 && strncmp(written, "0X", 2) != 0) {
        return false;
    }

    char* end = NULL;
    unsigned long long address = strtoull(written + 2, &end, 16);

    if (*end != '\0' || address < PREFERRED_BASE || address - PREFERRED_BASE > UINT32_MAX) {
        return false;
    }

    *rva = (uint32_t)(address - PREFERRED_BASE);

    return true;
}


bool place_find(const char* written, uint32_t* rva) {
    if (parse_address(written, rva)) {
        return true;
    }

    SymbolMatch match = symbols_find(written);

    switch (match.result) {
    case SYMBOL_FOUND:
        *rva = match.rva;

        return true;
    case SYMBOL_AMBIGUOUS:
        fprintf(stderr, "%s could be any of %d places: %s\nWrite it with its (parameters), as mt2sdk find prints it\n",
            written, match.candidate_count, match.candidates);

        return false;
    default:
        fprintf(stderr, "%s isn't a name in the game. Search with: mt2sdk find <words>\n", written);

        return false;
    }
}

// GCC's raw name for "vtable for A::B" is _ZTVN1A1BE, and for a plain class _ZTV1A (_ZTI for type info).
bool place_class_symbol(const char* prefix, const char* class_name, uint32_t* rva) {
    char parts[PLACE_CAPACITY] = "";
    char raw[PLACE_CAPACITY];
    int part_count = 0;
    size_t used = 0;

    for (const char* part = class_name; *part != '\0';) {
        const char* end = strstr(part, "::");
        size_t length = end != NULL ? (size_t)(end - part) : strlen(part);

        if (length == 0 || strpbrk(part, "<>( ") != NULL) {
            return false;
        }

        used += (size_t)snprintf(parts + used, sizeof parts - used, "%zu%.*s", length, (int)length, part);
        part_count++;
        part = end != NULL ? end + 2 : part + length;
    }

    snprintf(raw, sizeof raw, part_count > 1 ? "%sN%sE" : "%s%s", prefix, parts);

    SymbolMatch match = part_count > 0 ? symbols_find(raw) : (SymbolMatch){ .result = SYMBOL_MISSING };

    if (match.result == SYMBOL_FOUND) {
        *rva = match.rva;
    }

    return match.result == SYMBOL_FOUND;
}

bool place_function_at(uint32_t rva, Function* function) {
    uint32_t start = 0;
    uint32_t size = 0;
    uint32_t offset = 0;

    if (!symbols_extent_at(rva, &start, &size) || !symbols_name_at(start, function->name, sizeof function->name, &offset)) {
        return false;
    }

    function->start = start;
    function->end = start + size;

    return true;
}

void place_name(uint32_t rva, char* name, size_t capacity) {
    char symbol[PLACE_CAPACITY];
    uint32_t offset = 0;

    if (!symbols_name_at(rva, symbol, sizeof symbol, &offset)) {
        snprintf(name, capacity, "0x%llx", PREFERRED_BASE + rva);

        return;
    }

    if (offset == 0) {
        snprintf(name, capacity, "%s", symbol);
    } else {
        snprintf(name, capacity, "%s+0x%x", symbol, offset);
    }
}

void place_quote_text(const char* text, char* quoted, size_t capacity) {
    size_t length = 0;

    if (capacity < 3) {
        return;
    }

    quoted[length++] = '"';

    for (const char* character = text; *character != '\0' && length + 3 < capacity; character++) {
        if (*character == '\n' || *character == '\t' || *character == '"' || *character == '\\') {
            quoted[length++] = '\\';
            quoted[length++] = *character == '\n' ? 'n' : *character == '\t' ? 't' : *character;
        } else {
            quoted[length++] = *character;
        }
    }

    quoted[length++] = '"';
    quoted[length] = '\0';
}

static bool describe_float(const GameImage* image, const Reference* reference, char* note, size_t capacity) {
    const uint8_t* bytes = game_image_at(image, reference->target, reference->float_size);

    if (reference->float_size == 0 || bytes == NULL) {
        return false;
    }

    if (reference->float_size == 4) {
        float value = 0;
        memcpy(&value, bytes, sizeof value);
        snprintf(note, capacity, "%g", (double)value);
    } else {
        double value = 0;
        memcpy(&value, bytes, sizeof value);
        snprintf(note, capacity, "%g", value);
    }

    return true;
}


void place_describe(const GameImage* image, const Reference* reference, const Function* inside, char* note, size_t capacity) {
    uint32_t target = reference->target;
    note[0] = '\0';

    if (inside != NULL && target >= inside->start && target < inside->end && target != inside->start) {
        snprintf(note, capacity, "+0x%x", target - inside->start);

        return;
    }

    char text[TEXT_CAPACITY];
    uint32_t start = 0;
    uint32_t size = 0;
    bool at_symbol = symbols_extent_at(target, &start, &size) && start == target;

    if (!at_symbol && game_image_text_at(image, target, text, sizeof text)) {
        place_quote_text(text, note, capacity);

        return;
    }

    if (describe_float(image, reference, note, capacity)) {
        return;
    }

    // MinGW reaches vtables and globals through unnamed 8-byte slots holding their address: name what the slot holds.
    uint64_t pointer = 0;
    uint32_t pointee = 0;
    const uint8_t* slot = at_symbol ? NULL : game_image_at(image, target, sizeof pointer);

    if (slot != NULL && reference->kind == REFERENCE_READ) {
        memcpy(&pointer, slot, sizeof pointer);
    }

    uint32_t pointee_start = 0;
    uint32_t pointee_size = 0;

    if (pointer != 0 && game_image_rva_of_pointer(image, pointer, &pointee)
        && symbols_extent_at(pointee, &pointee_start, &pointee_size) && pointee - pointee_start < FAR_FROM_NAME) {
        char name[PLACE_CAPACITY];
        place_name(pointee, name, sizeof name);
        snprintf(note, capacity, "address of %s", name);

        return;
    }

    // Unnamed data far past the last name before it has nothing to do with that name.
    bool far_from_name = !game_image_is_code(image, target) && target - start >= FAR_FROM_NAME;

    if (far_from_name) {
        snprintf(note, capacity, "0x%llx", PREFERRED_BASE + target);

        return;
    }

    place_name(target, note, capacity);
}
