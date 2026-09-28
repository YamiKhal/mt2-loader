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

// Where MT2.exe asks to be loaded. Disassemblers (Ghidra, IDA, x64dbg before it starts) show addresses from here.
#define PREFERRED_BASE 0x140000000ull
#define MAX_SHOWN 300


static bool load_game_symbols(const wchar_t* given, wchar_t* path) {
    char problem[200];

    if (!game_exe_find(given, path)) {
        return false;
    }

    if (!symbols_load(path, problem, sizeof problem)) {
        fprintf(stderr, "%s\n", problem);

        return false;
    }

    symbols_prepare_readable_names();

    return true;
}

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

    if (term_count > 16 || !load_game_symbols(exe, path)) {
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

    if (!load_game_symbols(exe, path)) {
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
    FreeLibrary(module);

    wprintf(L"%ls\n", path);

    if (game_build_is_known(&build)) {
        printf("Game build %s: this loader knows it. Put \"%s\" in your manifest's game_builds\n", build.name, build.name);

        return 0;
    }

    printf("Game build not known to this loader (linked %08x, image %u bytes): the game was probably updated after this loader\n",
        build.link_timestamp, build.image_size);

    return 1;
}
