#ifndef MT2SDK_GAME_FIELDS_H
#define MT2SDK_GAME_FIELDS_H

#include <stdint.h>

#include "game_image.h"
#include "mappings.h"
#include "reflection.h"

#define GAME_FIELDS_MAX 512

typedef struct GameField {
    const ReflectedField* field;
    // Where it is in an object of the class asked about (its own offset plus its base class's place).
    uint32_t offset;
    // How many bytes it takes, or 0 when its type's size isn't known.
    uint32_t size;
} GameField;

// The fields the game names for a class and for every class it's built on, where they sit in the class's objects.
int game_fields_of(const GameImage* image, const Reflection* reflection, const Mappings* mappings, const char* class_name,
    GameField* fields, int capacity);

// A C++ type's size: the basic types, pointers, std::string, and classes whose mapping has a size. 0 when unknown.
uint32_t game_type_size(const Mappings* mappings, const char* type);

#endif
