#ifndef MT2SDK_VTABLES_H
#define MT2SDK_VTABLES_H

#include <stdint.h>

#include "game_image.h"

#define VTABLE_MAX_ENTRIES 2000

typedef struct VtableEntry {
    uint32_t function;
    // 0 for the class's own table; 1, 2, ... for the tables of its other bases, which sit at base_offset in the object.
    int table;
    uint32_t base_offset;
    int slot;
} VtableEntry;

// The virtual functions in the vtable at rva ("vtable for X"), table by table, slot by slot.
int vtables_read(const GameImage* image, uint32_t table, VtableEntry* entries, int capacity);

#endif
