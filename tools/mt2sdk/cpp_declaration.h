#ifndef MT2SDK_CPP_DECLARATION_H
#define MT2SDK_CPP_DECLARATION_H

#include <stdbool.h>
#include <stddef.h>

// A game::Function or game::find line for mt2loader.hpp, from a symbol's readable name, for: mt2sdk find --cpp
// Returns true for a function (its declaration has a RESULT to fill in), false for a global.
bool cpp_declaration(const char* name, char* declaration, size_t capacity);

#endif
