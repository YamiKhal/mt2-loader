#include "game_exe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define GAME_IN_LIBRARY L"\\steamapps\\common\\MMORPG Tycoon 2\\MT2.exe"


static bool is_file(const wchar_t* path) {
    DWORD attributes = GetFileAttributesW(path);

    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool steam_folder(wchar_t* folder) {
    DWORD size = GAME_PATH_CAPACITY * sizeof(wchar_t);

    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, NULL, folder, &size) == ERROR_SUCCESS;
}

static bool game_in_library(const wchar_t* library, wchar_t* path) {
    swprintf(path, GAME_PATH_CAPACITY, L"%ls%ls", library, GAME_IN_LIBRARY);

    for (wchar_t* character = path; *character != L'\0'; character++) {
        *character = *character == L'/' ? L'\\' : *character;
    }

    return is_file(path);
}

// libraryfolders.vdf lists every Steam library as a line like: "path"  "F:\\SteamLibrary"
static bool game_in_listed_libraries(const wchar_t* steam, wchar_t* path) {
    wchar_t list_path[GAME_PATH_CAPACITY];
    swprintf(list_path, GAME_PATH_CAPACITY, L"%ls\\steamapps\\libraryfolders.vdf", steam);

    FILE* list = _wfopen(list_path, L"r");
    char line[2048];
    bool found = false;

    while (list != NULL && !found && fgets(line, sizeof line, list) != NULL) {
        char* key = strstr(line, "\"path\"");
        char* start = key != NULL ? strchr(key + 6, '"') : NULL;
        char* end = start != NULL ? strrchr(start + 1, '"') : NULL;

        if (end == NULL) {
            continue;
        }

        *end = '\0';

        char library[GAME_PATH_CAPACITY];
        size_t length = 0;

        for (char* character = start + 1; *character != '\0' && length + 1 < sizeof library; character++) {
            bool escaped_backslash = character[0] == '\\' && character[1] == '\\';
            library[length++] = *character;
            character += escaped_backslash ? 1 : 0;
        }

        library[length] = '\0';

        wchar_t library_wide[GAME_PATH_CAPACITY];

        if (MultiByteToWideChar(CP_UTF8, 0, library, -1, library_wide, GAME_PATH_CAPACITY) > 0) {
            found = game_in_library(library_wide, path);
        }
    }

    if (list != NULL) {
        fclose(list);
    }

    return found;
}


bool game_exe_find(const wchar_t* given, wchar_t* path) {
    const wchar_t* from_environment = _wgetenv(L"MT2_EXE");
    const wchar_t* chosen = given != NULL ? given : from_environment;

    if (chosen != NULL) {
        swprintf(path, GAME_PATH_CAPACITY, L"%ls", chosen);

        if (is_file(path)) {
            return true;
        }

        fwprintf(stderr, L"%ls isn't a file\n", path);

        return false;
    }

    wchar_t steam[GAME_PATH_CAPACITY];

    if (steam_folder(steam) && (game_in_library(steam, path) || game_in_listed_libraries(steam, path))) {
        return true;
    }

    fwprintf(stderr, L"MT2.exe wasn't found in the Steam libraries. Give it with --exe \"<path to MT2.exe>\", or set MT2_EXE\n");

    return false;
}
