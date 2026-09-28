#ifndef CORE_MOD_SETTINGS_H
#define CORE_MOD_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

/*
    A setting the mod declares in its config.json (the mod manager's settings format), for its plugins. kind gets
    "int", "float", "bool", "string" or "choice"; value gets the player's value from settings.json (written by the
    mod manager) when it fits, the default otherwise, as text. Returns false with the reason in problem.
*/
bool mod_settings_get(const wchar_t* mod_folder, const char* key, char* kind, size_t kind_size, char* value, size_t value_size,
    char* problem, size_t problem_size);

#endif
