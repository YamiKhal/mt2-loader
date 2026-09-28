#ifndef CORE_TEXT_CHECK_H
#define CORE_TEXT_CHECK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

typedef struct TextCheckResult {
    size_t compared_bytes;
    size_t relocated_bytes;
    size_t different_bytes;
    uint32_t first_difference_rva;
    char problem[160];
} TextCheckResult;

bool text_check_run(HMODULE exe_module, const wchar_t* exe_path, TextCheckResult* result);

#endif
