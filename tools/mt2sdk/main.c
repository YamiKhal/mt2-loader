#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include "../../src/include/mt2loader_version.h"
#include "commands.h"

static const char* USAGE =
    "mt2sdk " MT2LOADER_VERSION ": tools for MT2 Loader plugins\n"
    "\n"
    "Getting set up\n"
    "  mt2sdk setup                       what this PC has and lacks for plugins, and where to get it\n"
    "  mt2sdk build                       the game's build, and whether this SDK knows it\n"
    "\n"
    "A plugin project\n"
    "  mt2sdk new <folder> [\"Mod Name\"]   a project from the template, with the game's classes and debug settings\n"
    "  mt2sdk headers [file]              the game's classes as C++ for plugins (mt2game.hpp): npc.state()\n"
    "  mt2sdk check <mod folder>          check a plugin mod the way the loader will\n"
    "  mt2sdk log                         the loader's log as the game writes it\n"
    "\n"
    "Reading the game\n"
    "  mt2sdk find <text> [more text]     search the game's functions and globals by name\n"
    "  mt2sdk class <class>               what a class is built on, its size, source file and fields\n"
    "  mt2sdk enum [name]                 the game's enums and the words their values have in data files\n"
    "  mt2sdk uses <function>             what a function calls, and the texts, globals and numbers it uses\n"
    "  mt2sdk dis <function>              a function's code, with the names and texts it uses\n"
    "  mt2sdk refs <name or 0xaddress>    every place in the game that calls it, reads it or keeps it in a table\n"
    "  mt2sdk text <text> [more text]     texts in the game that contain the words, and where each is used\n"
    "  mt2sdk vtable <class>              a class's virtual functions, by slot\n"
    "  mt2sdk decompile [folder]          the game's code as C++ source files, made with Ghidra (mt2-workspace)\n"
    "  mt2sdk decompile [folder] --file <MMO_District.cpp>  only that file again, in about a minute\n"
    "  mt2sdk decompile [folder] --export  every file again, over the names and types the last run worked out\n"
    "  mt2sdk stats [folder]              how much of the rebuilt source still reads like machine code\n"
    "  mt2sdk snapshot save|check [folder]  keep a few rebuilt files, and see what a change does to them\n"
    "  mt2sdk reference [folder]          a page per class (fields, functions, enums) without the game's code, to share\n"
    "  mt2sdk diff <old MT2.exe>          what changed in a game update, and which mapped facts moved\n"
    "  mt2sdk symbols [file] / program [file]  every name, or everything above as JSON, for other tools\n"
    "\n"
    "mt2-mappings (what modders found out, shared)\n"
    "  mt2sdk mappings check <folder>     check the files against the game\n"
    "  mt2sdk mappings pull [workspace] --mappings <folder>  add what you named in the workspace's Ghidra project\n"
    "  mt2sdk mappings import <found.json> <folder>  the same from a file MT2Import.java wrote\n"
    "  mt2sdk mappings name <class> <0xoffset> <name> <type> [workspace] --mappings <folder>  name a field: writes it with\n"
    "                                     where the code uses it, checks it and rebuilds the file that uses it most\n"
    "  mt2sdk mappings todo [workspace] [class]  the unnamed fields the most code uses, to name first\n"
    "  mt2sdk mappings cards [workspace] [class]  each of those with its neighbors and the lines that use it\n"
    "  mt2sdk mappings json <folder> [file]  the mappings as one JSON file, for other tools\n"
    "  mt2sdk mappings fields <folder> <file.c>  the mapped fields as the loader's table, to build it with\n"
    "\n"
    "Options: --exe <path to MT2.exe> (found in the Steam libraries otherwise), --mappings <mt2-mappings folder>,\n"
    "         --ghidra <folder>, --all (decompile: the libraries' code too), --cpp and --raw (find)\n"
    "Guides: LOADER_MODDING.md (plugins), DEVKIT.md (these tools), mt2-mappings/CONTRIBUTING.md (mappings)\n";


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
    const wchar_t* ghidra = take_option(&count, argv, L"--ghidra", 1);
    const wchar_t* mappings = take_option(&count, argv, L"--mappings", 1);
    bool everything = take_option(&count, argv, L"--all", 0) != NULL;
    const wchar_t* only_file = take_option(&count, argv, L"--file", 1);

    if (take_option(&count, argv, L"--export", 0) != NULL) {
        only_file = L"*";
    }

    if (count < 2) {
        fputs(USAGE, stdout);

        return 2;
    }

    const wchar_t* command = argv[1];
    const wchar_t* first = count > 2 ? argv[2] : NULL;
    const wchar_t* second = count > 3 ? argv[3] : NULL;
    const wchar_t* third = count > 4 ? argv[4] : NULL;

    if (wcscmp(command, L"new") == 0 && first != NULL) {
        return command_new(first, second, exe, mappings);
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

    if (wcscmp(command, L"dis") == 0 && first != NULL) {
        return command_dis(exe, first);
    }

    if (wcscmp(command, L"uses") == 0 && first != NULL) {
        return command_uses(exe, first);
    }

    if (wcscmp(command, L"refs") == 0 && first != NULL) {
        return command_refs(exe, first);
    }

    if (wcscmp(command, L"text") == 0 && first != NULL) {
        return command_text(exe, count - 2, argv + 2);
    }

    if (wcscmp(command, L"vtable") == 0 && first != NULL) {
        return command_vtable(exe, first);
    }

    if (wcscmp(command, L"class") == 0 && first != NULL) {
        return command_class(exe, first, mappings);
    }

    if (wcscmp(command, L"setup") == 0) {
        return command_setup(exe, mappings, ghidra);
    }

    if (wcscmp(command, L"log") == 0) {
        return command_log(exe);
    }

    if (wcscmp(command, L"diff") == 0 && first != NULL) {
        return command_diff(exe, first, mappings);
    }

    if (wcscmp(command, L"reference") == 0) {
        return command_reference(exe, first != NULL ? first : L"mt2-reference", mappings);
    }

    if (wcscmp(command, L"headers") == 0) {
        return command_headers(exe, first != NULL ? first : L"mt2game.hpp", mappings);
    }

    if (wcscmp(command, L"program") == 0) {
        return command_program(exe, first != NULL ? first : L"program.json");
    }

    if (wcscmp(command, L"enum") == 0) {
        return command_enum(exe, first);
    }

    if (wcscmp(command, L"stats") == 0) {
        return command_stats(first != NULL ? first : L"mt2-workspace");
    }

    if (wcscmp(command, L"snapshot") == 0 && first != NULL && (wcscmp(first, L"save") == 0 || wcscmp(first, L"check") == 0)) {
        return command_snapshot(first, second != NULL ? second : L"mt2-workspace", exe, ghidra, mappings);
    }

    if (wcscmp(command, L"decompile") == 0) {
        return command_decompile(exe, first != NULL ? first : L"mt2-workspace", ghidra, mappings, everything, only_file);
    }

    if (wcscmp(command, L"mappings") == 0 && first != NULL && wcscmp(first, L"import") == 0 && second != NULL && third != NULL) {
        return command_mappings_import(exe, second, third);
    }

    if (wcscmp(command, L"mappings") == 0 && first != NULL && wcscmp(first, L"pull") == 0 && mappings != NULL) {
        return command_mappings_pull(exe, second != NULL ? second : L"mt2-workspace", mappings, ghidra);
    }

    if (wcscmp(command, L"mappings") == 0 && first != NULL && wcscmp(first, L"cards") == 0) {
        return command_mappings_cards(second != NULL ? second : L"mt2-workspace", third);
    }

    if (wcscmp(command, L"mappings") == 0 && first != NULL && wcscmp(first, L"name") == 0 && count > 6 && mappings != NULL) {
        return command_mappings_name(exe, count > 7 ? argv[7] : L"mt2-workspace", mappings, ghidra, argv[3], argv[4], argv[5], argv[6]);
    }

    if (wcscmp(command, L"mappings") == 0 && first != NULL && wcscmp(first, L"todo") == 0) {
        return command_mappings_todo(second != NULL ? second : L"mt2-workspace", third);
    }

    if (wcscmp(command, L"mappings") == 0 && first != NULL && wcscmp(first, L"check") == 0 && second != NULL) {
        return command_mappings_check(exe, second);
    }

    if (wcscmp(command, L"mappings") == 0 && first != NULL && wcscmp(first, L"json") == 0 && second != NULL) {
        return command_mappings_json(exe, second, third != NULL ? third : L"mappings.json");
    }

    if (wcscmp(command, L"mappings") == 0 && first != NULL && wcscmp(first, L"fields") == 0 && second != NULL && third != NULL) {
        return command_mappings_fields(second, third);
    }

    fputs(USAGE, stdout);

    return 2;
}
