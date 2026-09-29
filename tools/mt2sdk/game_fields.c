#include "game_fields.h"

#include <stdlib.h>
#include <string.h>

#include "rtti.h"

#define MAX_DEPTH 12

typedef struct KnownSize {
    const char* type;
    uint32_t size;
} KnownSize;

static const KnownSize BASIC_TYPES[] = {
    { "bool", 1 }, { "char", 1 }, { "unsigned char", 1 }, { "short", 2 }, { "unsigned short", 2 }, { "int", 4 },
    { "unsigned int", 4 }, { "float", 4 }, { "long long", 8 }, { "unsigned long long", 8 }, { "double", 8 },
    { "std::string", 0x20 },
};


static int add_fields(const GameImage* image, const Reflection* reflection, const Mappings* mappings, const char* class_name,
    uint32_t shift, int depth, GameField* fields, int count, int capacity) {
    if (depth > MAX_DEPTH) {
        return count;
    }

    for (int index = 0; index < reflection->count && count < capacity; index++) {
        const ReflectedField* field = &reflection->fields[index];

        if (field->has_offset && strcmp(field->class_name, class_name) == 0) {
            fields[count++] = (GameField){ field, field->offset + shift, game_type_size(mappings, field->type) };
        }
    }

    RttiBase* bases = malloc(RTTI_MAX_BASES * sizeof *bases);
    int base_count = bases != NULL ? rtti_bases(image, class_name, bases, RTTI_MAX_BASES) : 0;

    for (int index = 0; index < base_count; index++) {
        count = add_fields(image, reflection, mappings, bases[index].name, shift + bases[index].offset, depth + 1, fields, count, capacity);
    }

    free(bases);

    return count;
}


int game_fields_of(const GameImage* image, const Reflection* reflection, const Mappings* mappings, const char* class_name,
    GameField* fields, int capacity) {
    return add_fields(image, reflection, mappings, class_name, 0, 0, fields, 0, capacity);
}

uint32_t game_type_size(const Mappings* mappings, const char* type) {
    size_t length = strlen(type);

    if (length > 0 && (type[length - 1] == '*' || type[length - 1] == '&')) {
        return 8;
    }

    for (size_t index = 0; index < sizeof BASIC_TYPES / sizeof BASIC_TYPES[0]; index++) {
        if (strcmp(BASIC_TYPES[index].type, type) == 0) {
            return BASIC_TYPES[index].size;
        }
    }

    // vsWeakObjectLink<mmoNPC> has the size written for vsWeakObjectLink<T>.
    size_t template_start = strcspn(type, "<");

    for (int index = 0; mappings != NULL && index < mappings->entry_count; index++) {
        const Entry* entry = &mappings->entries[index];
        const Note* size = entry->kind == ENTRY_CLASS ? mappings_note(entry, "size") : NULL;
        bool same = strcmp(entry->name, type) == 0;
        bool same_template = type[template_start] == '<' && strncmp(entry->name, type, template_start) == 0
            && strcmp(entry->name + template_start, "<T>") == 0;

        if (size != NULL && (same || same_template)) {
            return (uint32_t)strtoul(size->value + 2, NULL, 16);
        }
    }

    return 0;
}
