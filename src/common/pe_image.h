#ifndef COMMON_PE_IMAGE_H
#define COMMON_PE_IMAGE_H

#include <windows.h>

IMAGE_NT_HEADERS64* pe_nt_headers(HMODULE module);
IMAGE_SECTION_HEADER* pe_find_section(HMODULE module, const char* name);

#endif
