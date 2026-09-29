#ifndef MT2SDK_PROGRAM_JSON_H
#define MT2SDK_PROGRAM_JSON_H

#include <stdbool.h>
#include <wchar.h>

#include "game_image.h"

// Everything the exe says about how the game is built, for tools that rebuild its source (the Ghidra export): the
// source files and which function comes from which, every class with type info or named fields (its bases, size,
// fields and vtable) and every enum's words. The exe's symbols must be loaded.
bool program_json_write(const GameImage* image, const char* build, const wchar_t* output);

#endif
