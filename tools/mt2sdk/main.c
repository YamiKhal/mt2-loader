#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include "../../src/include/mt2loader_version.h"
#include "commands.h"

static const char* USAGE =
    "mt2sdk " MT2LOADER_VERSION ": tools for MT2 Loader plugins\n"
    "\n"
    "  mt2sdk new <folder> [\"Mod Name\"]   make a plugin project from the template\n"
    "  mt2sdk find <text> [more text]     search the game's functions and globals by name\n"
    "  mt2sdk symbols [file]              write every name to a file (game_symbols.txt), to search in an editor\n"
    "  mt2sdk check <mod folder>          check a plugin mod the way the loader will\n"
    "  mt2sdk build                       show the game's build, and whether this loader knows it\n"
    "\n"
    "Options: --exe <path to MT2.exe> (found in the Steam libraries otherwise)\n"
    "         --cpp (find: a C++ declaration for each), --raw (find: the raw names too)\n"
    "Guide: LOADER_MODDING.md\n";


static const wchar_t* take_option(int* count, wchar_t** arguments, const wchar_t* option, int has_value) {
    for (int index = 0; index < *count; index++) {
        if (wcscmp(arguments[index], option) != 0) {
            continue;
        }

        const wchar_t* value = has_value && index + 1 < *count ? arguments[index + 1] : option;
        int removed = has_value ? 2 : 1;

        memmove(&arguments[index], &arguments[index + removed], (size_t)(*count - index - removed) * sizeof arguments[0]);
        *count -= removed;

        return value;
    }

    return NULL;
}

int wmain(int argc, wchar_t** argv) {
    int count = argc;
    const wchar_t* exe = take_option(&count, argv, L"--exe", 1);
    int raw = take_option(&count, argv, L"--raw", 0) != NULL;
    int cpp = take_option(&count, argv, L"--cpp", 0) != NULL;

    if (count < 2) {
        fputs(USAGE, stdout);

        return 2;
    }

    const wchar_t* command = argv[1];
    const wchar_t* first = count > 2 ? argv[2] : NULL;
    const wchar_t* second = count > 3 ? argv[3] : NULL;

    if (wcscmp(command, L"new") == 0 && first != NULL) {
        return command_new(first, second, exe);
    }

    if (wcscmp(command, L"find") == 0 && first != NULL) {
        return command_find(exe, count - 2, argv + 2, raw, cpp);
    }

    if (wcscmp(command, L"symbols") == 0) {
        return command_symbols(exe, first != NULL ? first : L"game_symbols.txt");
    }

    if (wcscmp(command, L"check") == 0 && first != NULL) {
        return command_check(first, exe);
    }

    if (wcscmp(command, L"build") == 0) {
        return command_build(exe);
    }

    fputs(USAGE, stdout);

    return 2;
}
