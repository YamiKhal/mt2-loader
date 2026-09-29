#ifndef MT2SDK_ENUMS_H
#define MT2SDK_ENUMS_H

#include <stdbool.h>
#include <stdint.h>

#include "game_image.h"

#define ENUM_NAME_CAPACITY 256
#define ENUM_MAX_VALUES 256
#define ENUM_VALUE_CAPACITY 64

// An enum the game turns into words for its data files and saves (ConvertToString<E>): its values' words, in order.
typedef struct GameEnum {
    char name[ENUM_NAME_CAPACITY];
    char values[ENUM_MAX_VALUES][ENUM_VALUE_CAPACITY];
    int count;
    // The word list: where it starts, and how many words fit before the next symbol.
    uint32_t words;
    uint32_t word_capacity;
} GameEnum;

typedef struct GameEnums {
    GameEnum* items;
    int count;
} GameEnums;

// Reads each enum's words from the static setup code that fills its word list.
bool enums_read(const GameImage* image, GameEnums* enums);
void enums_free(GameEnums* enums);

const GameEnum* enums_find(const GameEnums* enums, const char* name);

#endif
