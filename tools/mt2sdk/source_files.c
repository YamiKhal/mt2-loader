#include "source_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../../src/common/pe_image.h"

#define COFF_ENTRY_SIZE 18
#define CLASS_EXTERNAL 2
#define CLASS_STATIC 3
#define CLASS_FILE 103
#define TYPE_FUNCTION 0x20
#define CODE_ROOT "../code/"


static bool grow(void** items, int* capacity, int needed, size_t item_size) {
    if (needed <= *capacity) {
        return true;
    }

    int grown = *capacity == 0 ? 1024 : *capacity * 2;
    void* resized = realloc(*items, (size_t)grown * item_size);

    if (resized == NULL) {
        return false;
    }

    *items = resized;
    *capacity = grown;

    return true;
}

static int compare_functions(const void* left, const void* right) {
    uint32_t left_rva = ((const SourceFunction*)left)->rva;
    uint32_t right_rva = ((const SourceFunction*)right)->rva;

    return left_rva < right_rva ? -1 : left_rva > right_rva;
}

// A .file entry keeps the name in its auxiliary records, 18 bytes each, or, for a long one, 4 zero bytes and where
// it starts in the string table after the symbols.
static void file_name_of(const GameImage* image, uint32_t strings, const uint8_t* entry, int aux_count, char* name, size_t capacity) {
    const uint8_t* aux = entry + COFF_ENTRY_SIZE;
    uint32_t zero = 0;
    uint32_t offset = 0;

    memcpy(&zero, aux, sizeof zero);
    memcpy(&offset, aux + 4, sizeof offset);

    if (aux_count > 0 && zero == 0 && strings + offset < image->file_size) {
        snprintf(name, capacity, "%.*s", (int)strnlen((const char*)image->file + strings + offset, capacity - 1),
            (const char*)image->file + strings + offset);

        return;
    }

    size_t length = (size_t)aux_count * COFF_ENTRY_SIZE;
    length = length < capacity - 1 ? length : capacity - 1;

    memcpy(name, aux, length);
    name[length] = '\0';
}

static bool read_symbols(const GameImage* image, SourceFiles* sources) {
    IMAGE_NT_HEADERS64* headers = pe_nt_headers((HMODULE)image->file);
    int file_capacity = 0;
    int function_capacity = 0;
    int current = -1;

    if (headers == NULL) {
        return false;
    }

    uint32_t table = headers->FileHeader.PointerToSymbolTable;
    uint32_t count = headers->FileHeader.NumberOfSymbols;

    for (uint32_t index = 0; index < count && table + (index + 1) * COFF_ENTRY_SIZE <= image->file_size;) {
        const uint8_t* entry = image->file + table + index * COFF_ENTRY_SIZE;
        uint32_t value = 0;
        int16_t section = 0;
        uint16_t type = 0;
        uint8_t storage = entry[16];
        uint8_t aux_count = entry[17];

        memcpy(&value, entry + 8, sizeof value);
        memcpy(&section, entry + 12, sizeof section);
        memcpy(&type, entry + 14, sizeof type);

        if (storage == CLASS_FILE && grow((void**)&sources->files, &file_capacity, sources->file_count + 1, sizeof(SourceFile))) {
            SourceFile* file = &sources->files[sources->file_count];
            memset(file, 0, sizeof *file);
            file_name_of(image, table + count * COFF_ENTRY_SIZE, entry, aux_count, file->name, sizeof file->name);
            current = sources->file_count++;
        } else if (current >= 0 && section > 0 && section <= image->section_count && image->sections[section - 1].is_code
            && (storage == CLASS_EXTERNAL || storage == CLASS_STATIC) && (type & TYPE_FUNCTION)
            && grow((void**)&sources->functions, &function_capacity, sources->function_count + 1, sizeof(SourceFunction))) {
            sources->functions[sources->function_count++] = (SourceFunction){ image->sections[section - 1].rva + value, current };
        }

        index += 1u + aux_count;
    }

    return sources->file_count > 0;
}

// Asserts carry their source path: "../code/Games/MMORPG/Utils/MMO_DropTable.cpp".
static void read_paths(const GameImage* image, SourceFiles* sources) {
    for (int section_index = 0; section_index < image->section_count; section_index++) {
        const ImageSection* section = &image->sections[section_index];

        if (section->is_code || section->file_size == 0) {
            continue;
        }

        const char* bytes = (const char*)image->file + section->file_offset;
        uint32_t size = section->size < section->file_size ? section->size : section->file_size;
        size_t root_length = strlen(CODE_ROOT);

        for (uint32_t at = 0; at + root_length < size; at++) {
            if (memcmp(bytes + at, CODE_ROOT, root_length) != 0 || (at > 0 && bytes[at - 1] != '\0')) {
                continue;
            }

            const char* path = bytes + at + root_length;
            size_t length = strnlen(path, size - at - root_length);

            if (length == size - at - root_length) {
                continue;
            }

            const char* slash = strrchr(path, '/');
            const char* base = slash != NULL ? slash + 1 : path;

            for (int index = 0; index < sources->file_count && length < SOURCE_PATH_CAPACITY; index++) {
                SourceFile* file = &sources->files[index];

                if (file->path[0] == '\0' && strcmp(file->name, base) == 0) {
                    snprintf(file->path, sizeof file->path, "%s", path);
                }
            }
        }
    }
}


static void folder_of(const char* path, char* folder, size_t capacity) {
    const char* slash = strrchr(path, '/');
    snprintf(folder, capacity, "%.*s", slash != NULL ? (int)(slash - path) : 0, path);
}

// The linker takes a folder's files together, so a file no assert names sits with its folder's other files: it gets
// the folder of the files around it (the one before, when the two sides differ).
static void guess_missing_paths(SourceFiles* sources) {
    char folder[SOURCE_PATH_CAPACITY];
    const char* last_path = NULL;

    for (int index = 0; index < sources->file_count; index++) {
        SourceFile* file = &sources->files[index];

        if (file->path[0] != '\0') {
            last_path = file->path;
            continue;
        }

        bool game_file = strstr(file->name, ".cpp") != NULL && (strncmp(file->name, "MMO_", 4) == 0 || strncmp(file->name, "VS_", 3) == 0);

        if (last_path == NULL || !game_file) {
            continue;
        }

        folder_of(last_path, folder, sizeof folder);

        int length = snprintf(file->path, sizeof file->path, "%s/%s", folder, file->name);
        file->path_guessed = length > 0 && (size_t)length < sizeof file->path;

        if (!file->path_guessed) {
            file->path[0] = '\0';
        }
    }
}


bool source_files_read(const GameImage* image, SourceFiles* sources) {
    memset(sources, 0, sizeof *sources);

    if (!read_symbols(image, sources)) {
        return false;
    }

    qsort(sources->functions, (size_t)sources->function_count, sizeof(SourceFunction), compare_functions);
    read_paths(image, sources);
    guess_missing_paths(sources);

    return true;
}

void source_files_free(SourceFiles* sources) {
    free(sources->files);
    free(sources->functions);
    memset(sources, 0, sizeof *sources);
}

int source_file_of(const SourceFiles* sources, uint32_t rva) {
    SourceFunction key = { rva, 0 };
    const SourceFunction* found = bsearch(&key, sources->functions, (size_t)sources->function_count, sizeof key, compare_functions);

    return found != NULL ? found->file : -1;
}
