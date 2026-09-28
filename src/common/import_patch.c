#include "import_patch.h"

#include <string.h>

#include "pe_image.h"


static void** find_slot_in_descriptor(BYTE* base, IMAGE_IMPORT_DESCRIPTOR* descriptor, const char* function_name) {
    // Without the name table the names can't be matched once the loader has filled the slots.
    if (descriptor->OriginalFirstThunk == 0) {
        return NULL;
    }

    IMAGE_THUNK_DATA64* name_thunk = (IMAGE_THUNK_DATA64*)(base + descriptor->OriginalFirstThunk);
    IMAGE_THUNK_DATA64* address_thunk = (IMAGE_THUNK_DATA64*)(base + descriptor->FirstThunk);

    for (; name_thunk->u1.AddressOfData != 0; name_thunk++, address_thunk++) {
        if (IMAGE_SNAP_BY_ORDINAL64(name_thunk->u1.Ordinal)) {
            continue;
        }

        IMAGE_IMPORT_BY_NAME* import_name = (IMAGE_IMPORT_BY_NAME*)(base + name_thunk->u1.AddressOfData);

        if (strcmp((const char*)import_name->Name, function_name) == 0) {
            return (void**)&address_thunk->u1.Function;
        }
    }

    return NULL;
}


void** import_find_slot(HMODULE module, const char* dll_name, const char* function_name) {
    IMAGE_NT_HEADERS64* nt_headers = pe_nt_headers(module);

    if (nt_headers == NULL) {
        return NULL;
    }

    IMAGE_DATA_DIRECTORY imports = nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    if (imports.VirtualAddress == 0) {
        return NULL;
    }

    BYTE* base = (BYTE*)module;
    IMAGE_IMPORT_DESCRIPTOR* descriptor = (IMAGE_IMPORT_DESCRIPTOR*)(base + imports.VirtualAddress);

    for (; descriptor->Name != 0; descriptor++) {
        if (_stricmp((const char*)(base + descriptor->Name), dll_name) != 0) {
            continue;
        }

        void** slot = find_slot_in_descriptor(base, descriptor, function_name);

        if (slot != NULL) {
            return slot;
        }
    }

    return NULL;
}

bool import_slot_is_filled(void** slot, const char* dll_name) {
    HMODULE dll = GetModuleHandleA(dll_name);
    HMODULE owner = NULL;

    if (dll == NULL) {
        return false;
    }

    BOOL found = GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)*slot,
        &owner
    );

    return found && owner == dll;
}

bool import_replace(void** slot, void* replacement) {
    DWORD old_protection = 0;

    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old_protection)) {
        return false;
    }

    InterlockedExchangePointer(slot, replacement);

    DWORD ignored = 0;
    VirtualProtect(slot, sizeof *slot, old_protection, &ignored);

    return true;
}
