#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "../../src/core/game_build.h"
#include "../../src/include/mt2loader_version.h"
#include "commands.h"
#include "game_exe.h"
#include "ghidra.h"

#define PATH_CAPACITY 1024
#define LABEL "  %-12s"


static bool is_file(const wchar_t* path) {
    DWORD attributes = GetFileAttributesW(path);

    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool is_folder(const wchar_t* path) {
    DWORD attributes = GetFileAttributesW(path);

    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool on_path(const wchar_t* program, wchar_t* found) {
    return SearchPathW(NULL, program, NULL, PATH_CAPACITY, found, NULL) > 0;
}

// Visual Studio with its C++ tools, found by vswhere, the tool Visual Studio installs to find itself.
static bool visual_studio(wchar_t* folder) {
    wchar_t vswhere[PATH_CAPACITY];
    wchar_t command[PATH_CAPACITY * 2];

    ExpandEnvironmentStringsW(L"%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe", vswhere, PATH_CAPACITY);

    if (!is_file(vswhere)) {
        return false;
    }

    swprintf(command, PATH_CAPACITY * 2, L"\"\"%ls\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 "
        L"-property installationPath\"", vswhere);

    FILE* output = _wpopen(command, L"r");
    char line[PATH_CAPACITY] = "";
    bool found = output != NULL && fgets(line, sizeof line, output) != NULL;

    if (output != NULL) {
        _pclose(output);
    }

    line[strcspn(line, "\r\n")] = '\0';
    MultiByteToWideChar(CP_UTF8, 0, line, -1, folder, PATH_CAPACITY);

    return found && line[0] != '\0';
}

static void game_line(const wchar_t* exe, wchar_t* game_folder, bool* ready) {
    wchar_t path[GAME_PATH_CAPACITY];

    if (!game_exe_find(exe, path)) {
        printf(LABEL "missing: give MT2.exe with --exe, or set MT2_EXE\n", "game");
        *ready = false;
        game_folder[0] = L'\0';

        return;
    }

    char build[64];
    game_exe_build_name(path, build, sizeof build);
    wprintf(L"  %-12ls%ls, build %hs%ls\n", L"game", path, build,
        strncmp(build, "unknown", 7) == 0 ? L" (newer than this SDK: get the newest SDK)" : L"");

    wcscpy(game_folder, path);
    wchar_t* last_slash = wcsrchr(game_folder, L'\\');

    if (last_slash != NULL) {
        *last_slash = L'\0';
    }
}

static void loader_line(const wchar_t* game_folder) {
    wchar_t core[PATH_CAPACITY];

    swprintf(core, PATH_CAPACITY, L"%ls\\mt2loader\\mt2loader.dll", game_folder);

    if (game_folder[0] != L'\0' && is_file(core)) {
        wprintf(L"  %-12lsinstalled (%ls)\n", L"loader", core);
    } else {
        printf(LABEL "missing: install it with the MT2 Mod Manager (Install loader), or by hand (the loader zip's INSTALL.txt)\n", "loader");
    }
}

static void compiler_line(bool* ready) {
    wchar_t found[PATH_CAPACITY];

    if (visual_studio(found)) {
        wprintf(L"  %-12lsVisual Studio with C++ (%ls)\n", L"compiler", found);
    } else if (on_path(L"g++.exe", found)) {
        wprintf(L"  %-12lsGCC (%ls)\n", L"compiler", found);
    } else {
        printf(LABEL "missing: Visual Studio Community with \"Desktop development with C++\" (visualstudio.microsoft.com), "
            "or MSYS2's MinGW-w64 GCC\n", "compiler");
        *ready = false;
    }

    if (on_path(L"cmake.exe", found)) {
        wprintf(L"  %-12ls%ls\n", L"cmake", found);
    } else {
        printf(LABEL "comes with Visual Studio (its Open Folder uses it); for the command line, get it from cmake.org\n", "cmake");
    }
}

static void ghidra_lines(const wchar_t* ghidra_folder) {
    Ghidra ghidra;
    int java_major = 0;

    ghidra_look(ghidra_folder, &ghidra, &java_major);

    if (ghidra.folder[0] != L'\0') {
        wprintf(L"  %-12ls%ls\n", L"ghidra", ghidra.folder);
    } else {
        printf(LABEL "missing (only mt2sdk decompile needs it): unzip it from ghidra-sre.org, then --ghidra <folder> or "
            "GHIDRA_INSTALL_DIR\n", "ghidra");
    }

    if (ghidra.java_home[0] != L'\0') {
        wprintf(L"  %-12lsJava %d (%ls)\n", L"java", java_major, ghidra.java_home);
    } else {
        printf(LABEL "missing (Ghidra needs Java 21 or newer): adoptium.net, or winget install EclipseAdoptium.Temurin.21.JDK\n", "java");
    }
}

static void mappings_line(const wchar_t* mappings_folder) {
    wchar_t build_file[PATH_CAPACITY];

    if (mappings_folder == NULL) {
        printf(LABEL "not given: get mt2-mappings (what modders found out about the game's code), then add "
            "--mappings <folder> to class, headers, decompile and new\n", "mappings");

        return;
    }

    swprintf(build_file, PATH_CAPACITY, L"%ls\\build.txt", mappings_folder);
    wprintf(is_file(build_file) ? L"  %-12ls%ls\n" : L"  %-12ls%ls isn't an mt2-mappings folder (it has no build.txt)\n",
        L"mappings", mappings_folder);
}

// %APPDATA%\VectorStorm\MMORPG Tycoon 2\<Steam number>\mod: where mods go, one Steam profile at a time.
static void mod_folder_line(void) {
    wchar_t pattern[PATH_CAPACITY];
    WIN32_FIND_DATAW found;
    int shown = 0;

    ExpandEnvironmentStringsW(L"%APPDATA%\\VectorStorm\\MMORPG Tycoon 2\\*", pattern, PATH_CAPACITY);
    HANDLE search = FindFirstFileW(pattern, &found);

    while (search != INVALID_HANDLE_VALUE) {
        wchar_t mods[PATH_CAPACITY];
        pattern[wcslen(pattern) - 1] = L'\0';
        swprintf(mods, PATH_CAPACITY, L"%ls%ls\\mod", pattern, found.cFileName);
        wcscat(pattern, L"*");

        if (found.cFileName[0] != L'.' && is_folder(mods)) {
            wprintf(L"  %-12ls%ls\n", shown == 0 ? L"mod folder" : L"", mods);
            shown++;
        }

        if (!FindNextFileW(search, &found)) {
            FindClose(search);
            search = INVALID_HANDLE_VALUE;
        }
    }

    if (shown == 0) {
        printf(LABEL "not made yet: start the game once\n", "mod folder");
    }
}


int command_setup(const wchar_t* exe, const wchar_t* mappings_folder, const wchar_t* ghidra_folder) {
    wchar_t game_folder[PATH_CAPACITY];
    bool ready = true;

    printf("mt2sdk " MT2LOADER_VERSION ": what a plugin project needs on this PC\n");
    game_line(exe, game_folder, &ready);
    loader_line(game_folder);
    compiler_line(&ready);
    mappings_line(mappings_folder);
    ghidra_lines(ghidra_folder);
    mod_folder_line();

    printf(ready ? "Ready. Next: mt2sdk new MyPlugin \"My Plugin\"\n" : "Get what's missing above, then run mt2sdk setup again\n");

    return ready ? 0 : 1;
}
