#ifndef CORE_MEMORY_H
#define CORE_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#define PATTERN_MAX_BYTES 256

typedef struct Pattern {
    size_t length;
    uint8_t bytes[PATTERN_MAX_BYTES];
    bool wildcard[PATTERN_MAX_BYTES];
} Pattern;

bool memory_read(const void* address, void* buffer, size_t size);
bool memory_write(void* address, const void* bytes, size_t size);

bool pattern_parse(const char* text, Pattern* pattern);
bool pattern_matches(const void* address, const Pattern* pattern);
void* pattern_scan(HMODULE module, const Pattern* pattern);
void* pattern_scan_range(const void* start, size_t size, const Pattern* pattern);

#endif
