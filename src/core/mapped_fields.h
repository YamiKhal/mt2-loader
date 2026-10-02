#ifndef CORE_MAPPED_FIELDS_H
#define CORE_MAPPED_FIELDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A field the game doesn't name, as mt2-mappings describes it, at one place its code uses it.
typedef struct MappedField {
    // "mmoCharacterType::colors"
    const char* name;
    uint32_t offset;
    // C++, as the mapping writes it: "int", "vsArrayStore<mmoGizmoVariant>", "?".
    const char* type;
    const char* seen_function;
    uint32_t seen_offset;
} MappedField;

// Made from mt2-mappings when the loader is built (mt2sdk mappings fields); the last one's name is NULL.
extern const MappedField MAPPED_FIELDS[];

/*
    The field's offset and type, once a place the mappings name still reaches it in this game: the instruction there
    uses the same offset. Otherwise false, with the reason when it's mapped but the game changed there (an empty one
    when it isn't mapped).
*/
bool mapped_field_find(const uint8_t* game_base, const char* name, size_t* offset, const char** type, char* problem, size_t problem_size);

#endif
