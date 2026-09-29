#include "reflection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/core/symbols.h"
#include "disassembly.h"
#include "places.h"
#include "register_walk.h"

// A property keeps its field's offset here (the loader reads it at the same place when the game runs).
#define OFFSET_IN_PROPERTY 0x38
#define PROPERTY_SIZE_LIMIT 0x100

typedef struct Property {
    uint32_t rva;
    uint32_t size;
    int field;
} Property;

typedef struct PropertyList {
    Property* items;
    int count;
    int capacity;
    Reflection* reflection;
    int field_capacity;
} PropertyList;

typedef struct WriterSearch {
    const PropertyList* properties;
    uint32_t* functions;
    int count;
    int capacity;
} WriterSearch;

static bool grow(void** items, int* capacity, int needed, size_t item_size) {
    if (needed <= *capacity) {
        return true;
    }

    int grown = *capacity == 0 ? 256 : *capacity * 2;
    void* resized = realloc(*items, (size_t)grown * item_size);

    if (resized == NULL) {
        return false;
    }

    *items = resized;
    *capacity = grown;

    return true;
}

// mmoCharacter::s_weaponIlvlProperty, or mmoCharacter::s_attributesProperty_deprecated: the owner is before "::s_".
static void collect_property(size_t index, const char* readable, void* opaque) {
    PropertyList* list = opaque;
    const char* marker = strstr(readable, "::s_");
    uint32_t start = 0;
    uint32_t size = 0;

    if (marker == NULL || strstr(marker, "Property") == NULL || strchr(marker, '(') != NULL || strstr(readable, "vtable") != NULL) {
        return;
    }

    if (!symbols_extent_at(symbols_rva_of(index), &start, &size) || start != symbols_rva_of(index)) {
        return;
    }

    Reflection* reflection = list->reflection;

    if (!grow((void**)&list->items, &list->capacity, list->count + 1, sizeof(Property))
        || !grow((void**)&reflection->fields, &list->field_capacity, reflection->count + 1, sizeof(ReflectedField))) {
        return;
    }

    ReflectedField* field = &reflection->fields[reflection->count];
    memset(field, 0, sizeof *field);
    snprintf(field->class_name, sizeof field->class_name, "%.*s", (int)(marker - readable), readable);
    field->property = start;

    list->items[list->count++] = (Property){ start, size > PROPERTY_SIZE_LIMIT || size == 0 ? PROPERTY_SIZE_LIMIT : size, reflection->count };
    reflection->count++;
}

static int compare_properties(const void* left, const void* right) {
    uint32_t left_rva = ((const Property*)left)->rva;
    uint32_t right_rva = ((const Property*)right)->rva;

    return left_rva < right_rva ? -1 : left_rva > right_rva;
}

static const Property* property_holding(const PropertyList* list, uint32_t rva) {
    int low = 0;
    int high = list->count;

    while (low < high) {
        int middle = (low + high) / 2;

        if (list->items[middle].rva + list->items[middle].size <= rva) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    return low < list->count && rva >= list->items[low].rva ? &list->items[low] : NULL;
}

static void collect_writer(uint32_t from, const Reference* reference, void* opaque) {
    WriterSearch* search = opaque;

    if ((reference->kind != REFERENCE_WRITE && reference->kind != REFERENCE_ADDRESS) || property_holding(search->properties, reference->target) == NULL) {
        return;
    }

    Function function;

    if (!place_function_at(from, &function)) {
        return;
    }

    for (int index = search->count - 1; index >= 0 && index >= search->count - 4; index--) {
        if (search->functions[index] == function.start) {
            return;
        }
    }

    if (grow((void**)&search->functions, &search->capacity, search->count + 1, sizeof(uint32_t))) {
        search->functions[search->count++] = function.start;
    }
}

static int compare_rvas(const void* left, const void* right) {
    uint32_t left_rva = *(const uint32_t*)left;
    uint32_t right_rva = *(const uint32_t*)right;

    return left_rva < right_rva ? -1 : left_rva > right_rva;
}

// "vtable for vsProperty<int, mmoCharacter>" gives kind vsProperty and type int; the first template argument can
// itself have commas inside <>.
static void read_type(const char* vtable_name, ReflectedField* field) {
    const char* start = strncmp(vtable_name, "vtable for ", 11) == 0 ? vtable_name + 11 : vtable_name;
    const char* open = strchr(start, '<');

    if (open == NULL) {
        return;
    }

    snprintf(field->kind, sizeof field->kind, "%.*s", (int)(open - start), start);

    int depth = 0;
    const char* end = open + 1;

    for (; *end != '\0'; end++) {
        if (*end == '<') {
            depth++;
        } else if (*end == '>') {
            if (depth == 0) {
                break;
            }

            depth--;
        } else if (*end == ',' && depth == 0) {
            break;
        }
    }

    snprintf(field->type, sizeof field->type, "%.*s", (int)(end - open - 1), open + 1);
}

static void note_vtable(const RegisterWalk* walk, int register_number, ReflectedField* field) {
    char name[PLACE_CAPACITY];
    uint32_t vtable = 0;

    if (!register_walk_address(walk, register_number, &vtable)) {
        return;
    }

    place_name(vtable, name, sizeof name);

    char* plus = strrchr(name, '+');

    if (plus != NULL && strncmp(name, "vtable for ", 11) == 0) {
        *plus = '\0';
    }

    read_type(name, field);
}

static void note_name(const GameImage* image, const RegisterWalk* walk, ReflectedField* field) {
    char text[REFLECTION_NAME_CAPACITY];
    uint32_t name = 0;

    if (register_walk_address(walk, disassembly_register_rdx(), &name) && game_image_text_at(image, name, text, sizeof text)) {
        snprintf(field->name, sizeof field->name, "%s", text);
    }
}

static void note_write(const Instruction* instruction, const Reference* reference, const RegisterWalk* walk, const PropertyList* list) {
    const Property* property = reference->kind == REFERENCE_WRITE ? property_holding(list, reference->target) : NULL;

    if (property == NULL) {
        return;
    }

    ReflectedField* field = &list->reflection->fields[property->field];
    uint32_t inside = reference->target - property->rva;

    if (inside == OFFSET_IN_PROPERTY && instruction->has_immediate) {
        field->offset = (uint32_t)instruction->immediate;
        field->has_offset = true;
    } else if (inside == 0 && instruction->source_register > 0) {
        note_vtable(walk, instruction->source_register, field);
    }
}

static void walk_writer(const GameImage* image, uint32_t start, const PropertyList* list, uint32_t constructor) {
    Function function;
    Instruction instruction;
    RegisterWalk* walk = malloc(sizeof *walk);

    if (walk == NULL || !place_function_at(start, &function)) {
        free(walk);

        return;
    }

    register_walk_start(walk);

    for (uint32_t rva = function.start; rva < function.end; rva += instruction.length) {
        if (!disassembly_decode(image, rva, &instruction)) {
            break;
        }

        for (int index = 0; index < instruction.reference_count; index++) {
            note_write(&instruction, &instruction.references[index], walk, list);
        }

        uint32_t object = 0;
        bool is_constructor = instruction.operation == OPERATION_CALL && instruction.reference_count > 0
            && instruction.references[0].target == constructor;
        const Property* property = is_constructor && register_walk_address(walk, disassembly_register_rcx(), &object)
            ? property_holding(list, object) : NULL;

        if (property != NULL) {
            note_name(image, walk, &list->reflection->fields[property->field]);
        }

        register_walk_step(walk, image, &instruction);
    }

    free(walk);
}

// A property built by a constructor of its own (vsPropertyObject<...>::vsPropertyObject) instead of in place still
// has its name in its symbol: s_<name>Property.
static void name_from_symbol(ReflectedField* field, uint32_t property) {
    char name[PLACE_CAPACITY];
    uint32_t offset = 0;

    if (field->name[0] != '\0' || !symbols_name_at(property, name, sizeof name, &offset)) {
        return;
    }

    const char* start = strstr(name, "::s_");
    const char* end = start != NULL ? strstr(start, "Property") : NULL;

    if (end != NULL) {
        snprintf(field->name, sizeof field->name, "%.*s", (int)(end - start - 4), start + 4);
    }
}


bool reflection_read(const GameImage* image, Reflection* reflection) {
    memset(reflection, 0, sizeof *reflection);

    PropertyList list = { .reflection = reflection };
    symbols_each_containing("Property", collect_property, &list);

    if (list.count == 0) {
        return false;
    }

    qsort(list.items, (size_t)list.count, sizeof(Property), compare_properties);

    WriterSearch search = { .properties = &list };
    disassembly_each_reference(image, collect_writer, &search);
    qsort(search.functions, (size_t)search.count, sizeof(uint32_t), compare_rvas);

    SymbolMatch constructor = symbols_find("vsPropertyBase::vsPropertyBase(char const*, bool)");

    for (int index = 0; index < search.count; index++) {
        if (index == 0 || search.functions[index] != search.functions[index - 1]) {
            walk_writer(image, search.functions[index], &list, constructor.result == SYMBOL_FOUND ? constructor.rva : 0);
        }
    }

    for (int index = 0; index < reflection->count; index++) {
        name_from_symbol(&reflection->fields[index], reflection->fields[index].property);
    }

    free(search.functions);
    free(list.items);

    return true;
}

void reflection_free(Reflection* reflection) {
    free(reflection->fields);
    memset(reflection, 0, sizeof *reflection);
}
