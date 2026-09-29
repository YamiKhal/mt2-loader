#ifndef MT2SDK_RTTI_H
#define MT2SDK_RTTI_H

#include <stdbool.h>
#include <stdint.h>

#include "game_image.h"

#define RTTI_NAME_CAPACITY 1200
#define RTTI_MAX_BASES 16

typedef struct RttiBase {
    char name[RTTI_NAME_CAPACITY];
    uint32_t offset;
    bool is_virtual;
} RttiBase;

// The classes a class is built on, from the type info GCC keeps for every class with virtual functions, in order.
// Returns how many (0 for a class with no bases), or -1 when the game has no type info for it.
int rtti_bases(const GameImage* image, const char* class_name, RttiBase* bases, int capacity);

#endif
