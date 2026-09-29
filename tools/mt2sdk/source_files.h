#ifndef MT2SDK_SOURCE_FILES_H
#define MT2SDK_SOURCE_FILES_H

#include <stdbool.h>
#include <stdint.h>

#include "game_image.h"

#define SOURCE_PATH_CAPACITY 260

typedef struct SourceFile {
    // MMO_DropTable.cpp, as the symbol table names it, and Games/MMORPG/Utils/MMO_DropTable.cpp when an assert in
    // the game gives its folder (empty otherwise).
    char name[SOURCE_PATH_CAPACITY];
    char path[SOURCE_PATH_CAPACITY];
    // No assert names this file: its folder is the one of the files the linker put around it.
    bool path_guessed;
} SourceFile;

typedef struct SourceFunction {
    uint32_t rva;
    int file;
} SourceFunction;

typedef struct SourceFiles {
    SourceFile* files;
    int file_count;
    SourceFunction* functions;
    int function_count;
} SourceFiles;

// The source file each function was compiled from. The exe's symbol table lists each object file's name (a .file
// entry) before that object's symbols.
bool source_files_read(const GameImage* image, SourceFiles* sources);
void source_files_free(SourceFiles* sources);

// The file index of the function starting at rva, or -1.
int source_file_of(const SourceFiles* sources, uint32_t rva);

#endif
