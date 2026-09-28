#include "pe_image.h"

#include <string.h>


IMAGE_NT_HEADERS64* pe_nt_headers(HMODULE module) {
    BYTE* base = (BYTE*)module;
    IMAGE_DOS_HEADER* dos_header = (IMAGE_DOS_HEADER*)base;

    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return NULL;
    }

    IMAGE_NT_HEADERS64* nt_headers = (IMAGE_NT_HEADERS64*)(base + dos_header->e_lfanew);

    if (nt_headers->Signature != IMAGE_NT_SIGNATURE || nt_headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return NULL;
    }

    return nt_headers;
}

IMAGE_SECTION_HEADER* pe_find_section(HMODULE module, const char* name) {
    IMAGE_NT_HEADERS64* nt_headers = pe_nt_headers(module);

    if (nt_headers == NULL) {
        return NULL;
    }

    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt_headers);

    for (WORD index = 0; index < nt_headers->FileHeader.NumberOfSections; index++, section++) {
        if (strncmp((const char*)section->Name, name, IMAGE_SIZEOF_SHORT_NAME) == 0) {
            return section;
        }
    }

    return NULL;
}
