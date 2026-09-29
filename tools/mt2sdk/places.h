#ifndef MT2SDK_PLACES_H
#define MT2SDK_PLACES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "disassembly.h"
#include "game_image.h"

#define PLACE_CAPACITY 1200

typedef struct Function {
    uint32_t start;
    uint32_t end;
    char name[PLACE_CAPACITY];
} Function;

// A name as mt2sdk find prints it (with or without its parameters), a raw name, or an address like 0x1403a1df0.
// Prints why when it can't tell which place is meant.
bool place_find(const char* written, uint32_t* rva);

// A class's vtable ("_ZTV") or type info ("_ZTI") by the class's name, like mmoCharacter or mmoCharacter::Stats.
// Quiet: false for a name it can't spell that way (templates) or that the game doesn't have.
bool place_class_symbol(const char* prefix, const char* class_name, uint32_t* rva);

// The function or global that holds rva, from its symbol to the next one.
bool place_function_at(uint32_t rva, Function* function);

// "mmoCharacter::EquipWeaponModel(bool)+0x25c", or the address when no symbol holds it.
void place_name(uint32_t rva, char* name, size_t capacity);

// What a reference points at, for a note after an instruction: a name, a text in quotes, a float's value, or
// "+0x40" for a jump inside the function it's in. Empty when there's nothing useful to say.
void place_describe(const GameImage* image, const Reference* reference, const Function* inside, char* note, size_t capacity);

void place_quote_text(const char* text, char* quoted, size_t capacity);

#endif
