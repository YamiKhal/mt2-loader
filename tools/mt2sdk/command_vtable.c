#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../../src/core/symbols.h"
#include "commands.h"
#include "game_exe.h"
#include "game_image.h"
#include "places.h"
#include "vtables.h"



static bool find_vtable(const char* class_name, uint32_t* rva) {
    // Anything else than a plain class name (a template class, or "vtable for X" written out) goes through the normal
    // lookup.
    return place_class_symbol("_ZTV", class_name, rva) || place_find(class_name, rva);
}

int command_vtable(const wchar_t* exe, const wchar_t* written) {
    wchar_t path[GAME_PATH_CAPACITY];
    char class_name[PLACE_CAPACITY];
    GameImage image;
    uint32_t table = 0;

    WideCharToMultiByte(CP_UTF8, 0, written, -1, class_name, sizeof class_name, NULL, NULL);

    if (!game_exe_load_symbols(exe, path) || !find_vtable(class_name, &table) || !game_image_load(path, &image)) {
        return 1;
    }

    VtableEntry* entries = malloc(VTABLE_MAX_ENTRIES * sizeof *entries);
    int count = entries != NULL ? vtables_read(&image, table, entries, VTABLE_MAX_ENTRIES) : 0;
    char name[PLACE_CAPACITY];

    place_name(table, name, sizeof name);
    printf("%s    0x%llx\n", name, PREFERRED_BASE + table);

    for (int index = 0; index < count; index++) {
        const VtableEntry* entry = &entries[index];

        if (entry->table > 0 && entry->slot == 0) {
            printf("  table for the base at +0x%x\n", entry->base_offset);
        }

        place_name(entry->function, name, sizeof name);
        printf("  %3d  +0x%-4x  %s\n", entry->slot, entry->slot * 8, name);
    }

    free(entries);
    game_image_free(&image);

    return 0;
}
