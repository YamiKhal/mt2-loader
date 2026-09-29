#ifndef MT2SDK_CLASS_SIZES_H
#define MT2SDK_CLASS_SIZES_H

#include <stdint.h>

#include "game_image.h"
#include "mappings.h"

// A size read from the code, and at how many places the code agrees on it. One place can mislead: an object whose
// first member is the class is made the same way, with the bigger object's size.
typedef struct CodeSize {
    uint32_t size;
    int places;
} CodeSize;

// For each class, the size the game asks operator new for right before calling one of the class's constructors on
// the new object (the most common one when calls differ), or 0 when the game never does. Walks the game's code once.
void class_sizes_from_code(const GameImage* image, const char* const* names, int count, CodeSize* sizes);

// The same for each entry of the mappings (0 for what isn't a class), as a new array to free.
CodeSize* class_sizes_for_mappings(const GameImage* image, const Mappings* mappings);

#endif
