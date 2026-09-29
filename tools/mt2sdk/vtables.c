#include "vtables.h"

#include <stdbool.h>
#include <string.h>

#include "../../src/core/symbols.h"

// A vtable starts with the offset to the object's start and the class's type info; slot 0 comes after them.
#define TABLE_HEADER 16


static bool read_pointer(const GameImage* image, uint32_t rva, uint64_t* pointer) {
    const uint8_t* bytes = game_image_at(image, rva, sizeof *pointer);

    if (bytes == NULL) {
        return false;
    }

    memcpy(pointer, bytes, sizeof *pointer);

    return true;
}

// A class with several bases has one table per base in a row, each after its own offset and type info.
static bool is_table_header(const GameImage* image, uint32_t rva) {
    uint64_t offset_to_top = 0;
    uint64_t type_info = 0;
    uint32_t type_info_rva = 0;

    if (!read_pointer(image, rva, &offset_to_top) || !read_pointer(image, rva + 8, &type_info)) {
        return false;
    }

    bool small_offset = (int64_t)offset_to_top <= 0 && (int64_t)offset_to_top > -0x100000;

    return small_offset && game_image_rva_of_pointer(image, type_info, &type_info_rva) && !game_image_is_code(image, type_info_rva);
}


int vtables_read(const GameImage* image, uint32_t table, VtableEntry* entries, int capacity) {
    uint32_t start = 0;
    uint32_t size = 0;
    uint32_t end = symbols_extent_at(table, &start, &size) && start == table && size > 0 ? table + size : table + VTABLE_MAX_ENTRIES * 8;
    int count = 0;
    int table_index = 0;
    int slot = 0;
    uint32_t base_offset = 0;

    for (uint32_t entry = table + TABLE_HEADER; entry + 8 <= end && count < capacity; entry += 8) {
        uint64_t pointer = 0;
        uint32_t target = 0;

        if (entry != table + TABLE_HEADER && is_table_header(image, entry)) {
            read_pointer(image, entry, &pointer);
            base_offset = (uint32_t)-(int64_t)pointer;
            table_index++;
            slot = 0;
            entry += 8;
            continue;
        }

        if (!read_pointer(image, entry, &pointer) || !game_image_rva_of_pointer(image, pointer, &target) || !game_image_is_code(image, target)) {
            break;
        }

        entries[count++] = (VtableEntry){ target, table_index, base_offset, slot++ };
    }

    return count;
}
