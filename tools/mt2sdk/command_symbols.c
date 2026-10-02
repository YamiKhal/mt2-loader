#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../../src/core/game_build.h"
#include "../../src/core/symbols.h"
#include "commands.h"
#include "cpp_declaration.h"
#include "game_exe.h"
#include "game_image.h"

#define MAX_SHOWN 300


static void to_lower(char* text) {
    for (; *text != '\0'; text++) {
        *text = (char)tolower((unsigned char)*text);
    }
}

static bool matches_all(const char* name, int term_count, char terms[][256]) {
    char lowered[SYMBOL_NAME_CAPACITY];
    snprintf(lowered, sizeof lowered, "%s", name);
    to_lower(lowered);

    for (int index = 0; index < term_count; index++) {
        if (strstr(lowered, terms[index]) == NULL) {
            return false;
        }
    }

    return true;
}


int command_find(const wchar_t* exe, int term_count, wchar_t** terms, int raw, int cpp) {
    wchar_t path[GAME_PATH_CAPACITY];
    char lowered_terms[16][256];

    if (term_count > 16 || !game_exe_load_symbols(exe, path)) {
        return 1;
    }

    for (int index = 0; index < term_count; index++) {
        WideCharToMultiByte(CP_UTF8, 0, terms[index], -1, lowered_terms[index], sizeof lowered_terms[index], NULL, NULL);
        to_lower(lowered_terms[index]);
    }

    int found = 0;
    bool declared_function = false;
    char name[SYMBOL_NAME_CAPACITY];
    char previous[SYMBOL_NAME_CAPACITY] = "";

    for (size_t index = 0; index < symbols_count(); index++) {
        symbols_readable_name_of(index, name, sizeof name);

        // A constructor or destructor has two names at one address that read the same: show it once.
        bool repeats = index > 0 && symbols_rva_of(index - 1) == symbols_rva_of(index) && strcmp(previous, name) == 0;
        strcpy(previous, name);

        if (repeats || !matches_all(name, term_count, lowered_terms)) {
            continue;
        }

        found++;

        if (found > MAX_SHOWN) {
            continue;
        }

        printf("%s    0x%llx\n", name, PREFERRED_BASE + symbols_rva_of(index));

        if (raw) {
            printf("    %s\n", symbols_raw_name_of(index));
        }

        if (cpp) {
            char declaration[SYMBOL_NAME_CAPACITY * 2];
            declared_function = cpp_declaration(name, declaration, sizeof declaration) || declared_function;
            printf("    %s\n", declaration);
        }
    }

    if (found > MAX_SHOWN) {
        printf("... %d more. Add more words to narrow it down, or use: mt2sdk symbols\n", found - MAX_SHOWN);
    }

    printf("%d found. Use a name as it's written here, with or without its (parameters)\n", found);

    if (declared_function) {
        printf("RESULT is the function's return type, which a name doesn't include: see its code (in Ghidra or x64dbg).\n");
        printf("For a static function, leave out the object. Details: LOADER_MODDING.md, section 7 (game::Function)\n");
    }

    return found > 0 ? 0 : 1;
}

int command_symbols(const wchar_t* exe, const wchar_t* output) {
    wchar_t path[GAME_PATH_CAPACITY];

    if (!game_exe_load_symbols(exe, path)) {
        return 1;
    }

    FILE* file = _wfopen(output, L"w");

    if (file == NULL) {
        fwprintf(stderr, L"%ls couldn't be written\n", output);

        return 1;
    }

    char name[SYMBOL_NAME_CAPACITY];

    for (size_t index = 0; index < symbols_count(); index++) {
        symbols_readable_name_of(index, name, sizeof name);
        fprintf(file, "0x%llx  %s\n", PREFERRED_BASE + symbols_rva_of(index), name);
    }

    fclose(file);
    wprintf(L"%zu names written to %ls, in address order\n", symbols_count(), output);

    return 0;
}

// A class's type information names it as its length and name ("12mmoCharacter"); templates go on with their arguments.
static bool is_type_name(const char* text, size_t length) {
    size_t digits = 0;
    size_t name_length = 0;

    while (digits < length && digits < 3 && isdigit((unsigned char)text[digits])) {
        name_length = name_length * 10 + (size_t)(text[digits] - '0');
        digits++;
    }

    if (digits == 0 || name_length == 0 || digits + name_length > length || !isalpha((unsigned char)text[digits])) {
        return false;
    }

    for (size_t index = digits; index < digits + name_length; index++) {
        if (!isalnum((unsigned char)text[index]) && text[index] != '_') {
            return false;
        }
    }

    return digits + name_length == length || text[digits + name_length] == 'I';
}

static IMAGE_NT_HEADERS64* headers_of(HMODULE module) {
    return (IMAGE_NT_HEADERS64*)((uint8_t*)module + ((IMAGE_DOS_HEADER*)module)->e_lfanew);
}

static int count_type_names(HMODULE module) {
    IMAGE_NT_HEADERS64* headers = headers_of(module);
    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(headers);
    int count = 0;

    for (int index = 0; index < headers->FileHeader.NumberOfSections; index++, section++) {
        if (strncmp((const char*)section->Name, ".rdata", IMAGE_SIZEOF_SHORT_NAME) != 0) {
            continue;
        }

        const char* start = (const char*)module + section->VirtualAddress;
        const char* end = start + section->Misc.VirtualSize;

        for (const char* text = start; text < end; text += strnlen(text, (size_t)(end - text)) + 1) {
            count += is_type_name(text, strnlen(text, (size_t)(end - text))) ? 1 : 0;
        }
    }

    return count;
}

// What a build still says about itself: names for its code, debug information, and its classes' type information.
static void print_names_kept(HMODULE module) {
    IMAGE_NT_HEADERS64* headers = headers_of(module);
    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(headers);
    int debug_sections = 0;

    for (int index = 0; index < headers->FileHeader.NumberOfSections; index++, section++) {
        debug_sections += section->Name[0] == '/' ? 1 : 0;
    }

    if (headers->FileHeader.NumberOfSymbols > 0) {
        printf("Names: a symbol table of %lu entries, so the game's functions and fields can be found by name\n",
            headers->FileHeader.NumberOfSymbols);
    } else {
        printf("Names: stripped. Nothing in this build can be found by name, so plugins and these tools can't work on it\n");
    }

    printf("Debug information: %s\n", debug_sections > 0 ? "kept" : "stripped");
    printf("Type information: about %d classes named\n", count_type_names(module));
}

int command_build(const wchar_t* exe) {
    wchar_t path[GAME_PATH_CAPACITY];

    if (!game_exe_find(exe, path)) {
        return 1;
    }

    // Mapped as an image without resolving imports or running any of its code.
    HMODULE module = LoadLibraryExW(path, NULL, DONT_RESOLVE_DLL_REFERENCES);

    if (module == NULL) {
        fwprintf(stderr, L"%ls couldn't be read (error %lu)\n", path, GetLastError());

        return 1;
    }

    GameBuild build = game_build_identify(module);

    wprintf(L"%ls\n", path);
    print_names_kept(module);
    FreeLibrary(module);

    if (game_build_is_known(&build)) {
        printf("Game build %s: this loader knows it. Put \"%s\" in your manifest's game_builds\n", build.name, build.name);

        return 0;
    }

    printf("Game build not known to this loader (linked %08x, image %u bytes): the game was probably updated after this loader\n",
        build.link_timestamp, build.image_size);

    return 1;
}
