#include "mapped_fields.h"

#include <stdio.h>
#include <string.h>

#include "code.h"
#include "symbols.h"


static bool reaches_field(const uint8_t* game_base, const MappedField* field) {
    SymbolMatch match = symbols_find(field->seen_function);
    int64_t displacement = 0;

    if (match.result != SYMBOL_FOUND) {
        return false;
    }

    return code_memory_displacement(game_base + match.rva + field->seen_offset, &displacement) && displacement == field->offset;
}


bool mapped_field_find(const uint8_t* game_base, const char* name, size_t* offset, const char** type, char* problem, size_t problem_size) {
    const MappedField* first = NULL;

    snprintf(problem, problem_size, "%s", "");

    for (const MappedField* field = MAPPED_FIELDS; field->name != NULL; field++) {
        if (strcmp(field->name, name) != 0) {
            continue;
        }

        first = first != NULL ? first : field;

        if (reaches_field(game_base, field)) {
            *offset = field->offset;
            *type = field->type;

            return true;
        }
    }

    if (first != NULL) {
        snprintf(problem, problem_size, "mt2-mappings puts %s at +0x%x, but no place it names uses +0x%x in this game "
            "(like %s+0x%x): the game changed there, or the mapping needs a place that uses the field directly",
            name, first->offset, first->offset, first->seen_function, first->seen_offset);
    }

    return false;
}
