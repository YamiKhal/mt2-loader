#include "rtti.h"

#include <stdio.h>
#include <string.h>

#include "../../src/core/symbols.h"
#include "places.h"

#define TYPEINFO_PREFIX "typeinfo for "
// __vmi_class_type_info: after the name, 4 bytes of flags, 4 of base count, then 16 bytes per base.
#define VMI_BASES 0x18
#define VMI_BASE_SIZE 0x10
#define BASE_IS_VIRTUAL 0x1
#define BASE_OFFSET_SHIFT 8


static bool read_u64(const GameImage* image, uint32_t rva, uint64_t* value) {
    const uint8_t* bytes = game_image_at(image, rva, sizeof *value);

    if (bytes != NULL) {
        memcpy(value, bytes, sizeof *value);
    }

    return bytes != NULL;
}

// A type info's first word points into the vtable of its own kind: __si_class_type_info for one plain base,
// __vmi_class_type_info for several (or virtual ones), __class_type_info for none.
static bool kind_of(const GameImage* image, uint32_t type_info, char* kind, size_t capacity) {
    uint64_t pointer = 0;
    uint32_t vtable = 0;

    if (!read_u64(image, type_info, &pointer) || !game_image_rva_of_pointer(image, pointer, &vtable)) {
        return false;
    }

    place_name(vtable, kind, capacity);

    return true;
}

static bool base_name(const GameImage* image, uint64_t pointer, char* name, size_t capacity) {
    char symbol[PLACE_CAPACITY];
    uint32_t rva = 0;

    if (!game_image_rva_of_pointer(image, pointer, &rva)) {
        return false;
    }

    place_name(rva, symbol, sizeof symbol);

    if (strncmp(symbol, TYPEINFO_PREFIX, strlen(TYPEINFO_PREFIX)) != 0) {
        return false;
    }

    snprintf(name, capacity, "%s", symbol + strlen(TYPEINFO_PREFIX));

    return true;
}


// A plain class's type info by its mangled name; a template's (vsObject<mmoToon, mmoCharacter>) by its readable one.
static bool find_type_info(const char* class_name, uint32_t* type_info) {
    char readable[PLACE_CAPACITY + 16];

    if (place_class_symbol("_ZTI", class_name, type_info)) {
        return true;
    }

    snprintf(readable, sizeof readable, "%s%s", TYPEINFO_PREFIX, class_name);

    SymbolMatch match = symbols_find(readable);

    if (match.result == SYMBOL_FOUND) {
        *type_info = match.rva;
    }

    return match.result == SYMBOL_FOUND;
}


int rtti_bases(const GameImage* image, const char* class_name, RttiBase* bases, int capacity) {
    char kind[PLACE_CAPACITY];
    uint32_t type_info = 0;

    if (!find_type_info(class_name, &type_info) || !kind_of(image, type_info, kind, sizeof kind)) {
        return -1;
    }

    uint64_t pointer = 0;

    if (strstr(kind, "__si_class_type_info") != NULL) {
        bool read = capacity > 0 && read_u64(image, type_info + 0x10, &pointer) && base_name(image, pointer, bases[0].name, sizeof bases[0].name);

        if (read) {
            bases[0].offset = 0;
            bases[0].is_virtual = false;
        }

        return read ? 1 : 0;
    }

    if (strstr(kind, "__vmi_class_type_info") == NULL) {
        return 0;
    }

    const uint8_t* counts = game_image_at(image, type_info + 0x10, 8);
    uint32_t base_count = 0;

    if (counts == NULL) {
        return 0;
    }

    memcpy(&base_count, counts + 4, sizeof base_count);

    int found = 0;

    for (uint32_t index = 0; index < base_count && found < capacity && found < RTTI_MAX_BASES; index++) {
        uint32_t entry = type_info + VMI_BASES + index * VMI_BASE_SIZE;
        uint64_t offset_flags = 0;

        if (!read_u64(image, entry, &pointer) || !read_u64(image, entry + 8, &offset_flags)
            || !base_name(image, pointer, bases[found].name, sizeof bases[found].name)) {
            continue;
        }

        bases[found].is_virtual = (offset_flags & BASE_IS_VIRTUAL) != 0;
        bases[found].offset = (uint32_t)((int64_t)offset_flags >> BASE_OFFSET_SHIFT);
        found++;
    }

    return found;
}
