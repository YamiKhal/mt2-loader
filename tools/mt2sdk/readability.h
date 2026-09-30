#pragma once

#include <stdbool.h>
#include <wchar.h>

// What still reads like machine code in a rebuilt .cpp, counted per kind.
typedef enum ReadabilityKind {
    UNNAMED_FIELD,
    UNNAMED_LOCAL,
    REGISTER_LEFTOVER,
    STACK_TEMPORARY,
    GOTO,
    UNFOLDED_ASSERT,
    READABILITY_KIND_COUNT
} ReadabilityKind;

typedef struct Readability {
    int lines;
    int functions;
    int counts[READABILITY_KIND_COUNT];
} Readability;

extern const char* const READABILITY_NAMES[READABILITY_KIND_COUNT];

bool readability_of_file(const wchar_t* path, Readability* readability);
int readability_total(const Readability* readability);
