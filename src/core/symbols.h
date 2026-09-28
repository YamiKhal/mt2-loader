#ifndef CORE_SYMBOLS_H
#define CORE_SYMBOLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#define SYMBOL_NAME_CAPACITY 1024
#define SYMBOL_MAX_DISTINCT 16

typedef enum SymbolLookup {
    SYMBOL_FOUND,
    SYMBOL_MISSING,
    SYMBOL_AMBIGUOUS,
    SYMBOL_TABLE_MISSING,
} SymbolLookup;

typedef struct SymbolMatch {
    SymbolLookup result;
    uint32_t rva;
    int candidate_count;
    uint32_t distinct_rvas[SYMBOL_MAX_DISTINCT];
    // For SYMBOL_AMBIGUOUS: a few of the candidates, readable, separated by " | ".
    char candidates[600];
} SymbolMatch;

bool symbols_load(const wchar_t* exe_path, char* problem, size_t problem_size);
bool symbols_loaded(void);
size_t symbols_count(void);

void symbols_prepare_readable_names(void);
bool symbols_readable_names_ready(void);
SymbolMatch symbols_find(const char* name);
bool symbols_name_at(uint32_t rva, char* name, size_t name_size, uint32_t* offset);
// The symbol that contains rva: where it starts, and how far to the next symbol (0 for the last one).
bool symbols_extent_at(uint32_t rva, uint32_t* start, uint32_t* size);

// Calls each with every symbol whose readable name contains text, in address order.
void symbols_each_containing(const char* text, void (*each)(size_t index, const char* readable, void* opaque), void* opaque);

// For listing: the symbol at index, in address order.
uint32_t symbols_rva_of(size_t index);
const char* symbols_raw_name_of(size_t index);
void symbols_readable_name_of(size_t index, char* name, size_t name_size);

#endif
