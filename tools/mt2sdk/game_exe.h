#ifndef MT2SDK_GAME_EXE_H
#define MT2SDK_GAME_EXE_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

#define GAME_PATH_CAPACITY 1024

// MT2.exe from --exe, the MT2_EXE environment variable, or the Steam libraries. Prints why when it fails.
bool game_exe_find(const wchar_t* given, wchar_t* path);

// The game build's name (0.30.7), or "unknown-<link time>" for a build this SDK doesn't know.
void game_exe_build_name(const wchar_t* path, char* name, size_t capacity);

// game_exe_find, then the exe's symbols with their readable names. Prints why when it fails.
bool game_exe_load_symbols(const wchar_t* given, wchar_t* path);

#endif
