#ifndef MT2SDK_MAPPINGS_JSON_H
#define MT2SDK_MAPPINGS_JSON_H

#include <stdbool.h>
#include <wchar.h>

#include "game_image.h"
#include "mappings.h"

// Every entry as JSON, with the game build from folder's build.txt, each function's and global's address, and each
// class's size from the code when its mapping has none (the exe's symbols must be loaded). Prints why when the file
// can't be written.
bool mappings_json_write(const Mappings* mappings, const GameImage* image, const wchar_t* folder, const wchar_t* output);

#endif
