#ifndef MT2SDK_REFLECTION_H
#define MT2SDK_REFLECTION_H

#include <stdbool.h>
#include <stdint.h>

#include "game_image.h"

#define REFLECTION_NAME_CAPACITY 256
#define REFLECTION_TYPE_CAPACITY 512

// A field the game names for its data files and saves: registered once, when the game starts, as a property object
// (mmoCharacter::s_weaponIlvlProperty) that keeps the field's name, its offset and, through its vtable, its type.
typedef struct ReflectedField {
    char class_name[REFLECTION_NAME_CAPACITY];
    char name[REFLECTION_NAME_CAPACITY];
    uint32_t offset;
    bool has_offset;
    // The property's own kind (vsProperty, vsPropertyObject, vsPropertyObjectLink, ...) and the field's C++ type.
    char kind[REFLECTION_NAME_CAPACITY];
    char type[REFLECTION_TYPE_CAPACITY];
    uint32_t property;
} ReflectedField;

typedef struct Reflection {
    ReflectedField* fields;
    int count;
} Reflection;

// Reads every property's setup from the code that builds it, without running the game.
bool reflection_read(const GameImage* image, Reflection* reflection);
void reflection_free(Reflection* reflection);

#endif
