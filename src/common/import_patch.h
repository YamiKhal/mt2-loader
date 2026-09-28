#ifndef PROXY_IMPORT_PATCH_H
#define PROXY_IMPORT_PATCH_H

#include <stdbool.h>
#include <windows.h>

void** import_find_slot(HMODULE module, const char* dll_name, const char* function_name);
bool import_slot_is_filled(void** slot, const char* dll_name);
bool import_replace(void** slot, void* replacement);

#endif
