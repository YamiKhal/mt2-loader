#include "physfs_hook.h"

#include "../common/import_patch.h"

#define PHYSFS_DLL_NAME "libphysfs.dll"

typedef const char* (*PhysfsDirectoryFunction)(void);

static void** base_dir_slot = NULL;
static PhysfsDirectoryFunction real_get_base_dir = NULL;
static WriteDirectoryReadyFunction ready_callback = NULL;
static volatile LONG fired = 0;


// vsSystem::InitPhysFS asks for the base folder right after it has set the final write folder
// (the Steam profile folder) and right before it looks for mods: the moment the mods folder is known.
static const char* redirected_get_base_dir(void) {
    if (InterlockedCompareExchange(&fired, 1, 0) == 0) {
        import_replace(base_dir_slot, (void*)real_get_base_dir);

        HMODULE physfs = GetModuleHandleA(PHYSFS_DLL_NAME);
        PhysfsDirectoryFunction get_write_dir = physfs ? (PhysfsDirectoryFunction)(void*)GetProcAddress(physfs, "PHYSFS_getWriteDir") : NULL;

        ready_callback(get_write_dir ? get_write_dir() : NULL);
    }

    return real_get_base_dir();
}


bool physfs_hook_install(HMODULE exe_module, WriteDirectoryReadyFunction on_ready) {
    base_dir_slot = import_find_slot(exe_module, PHYSFS_DLL_NAME, "PHYSFS_getBaseDir");

    if (base_dir_slot == NULL || !import_slot_is_filled(base_dir_slot, PHYSFS_DLL_NAME)) {
        return false;
    }

    ready_callback = on_ready;
    real_get_base_dir = (PhysfsDirectoryFunction)*base_dir_slot;

    return import_replace(base_dir_slot, (void*)redirected_get_base_dir);
}
