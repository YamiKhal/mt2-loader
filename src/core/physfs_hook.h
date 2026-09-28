#ifndef CORE_PHYSFS_HOOK_H
#define CORE_PHYSFS_HOOK_H

#include <stdbool.h>
#include <windows.h>

typedef void (*WriteDirectoryReadyFunction)(const char* write_directory);

bool physfs_hook_install(HMODULE exe_module, WriteDirectoryReadyFunction on_ready);

#endif
