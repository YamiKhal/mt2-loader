#ifndef MT2SDK_MAPPINGS_H
#define MT2SDK_MAPPINGS_H

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

typedef enum EntryKind {
    ENTRY_CLASS,
    ENTRY_FUNCTION,
    ENTRY_GLOBAL,
    ENTRY_FIELD,
    ENTRY_METHOD,
} EntryKind;

// A line that describes its entry: doc, seen, unsure, size, returns, param, inlined or type.
typedef struct Note {
    char* keyword;
    char* value;
    int line;
} Note;

typedef struct Entry {
    EntryKind kind;
    char* name;
    // Fields only: where it is and what it holds.
    uint32_t offset;
    char* type;
    const char* file;
    int line;
    Note* notes;
    int note_count;
    // Classes only: their fields and methods, in file order.
    struct Entry* members;
    int member_count;
} Entry;

typedef struct Mappings {
    Entry* entries;
    int entry_count;
    char** files;
    int file_count;
    int problem_count;
} Mappings;

// Reads every .mapping file under folder, printing each problem with its file and line.
bool mappings_load(const wchar_t* folder, Mappings* mappings);
void mappings_free(Mappings* mappings);

// Prints "file:line: " and the message, and counts it.
void mappings_problem(Mappings* mappings, const char* file, int line, const char* format, ...);

// The first note with keyword, or NULL.
const Note* mappings_note(const Entry* entry, const char* keyword);

#endif
