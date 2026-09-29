#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../../src/core/game_build.h"
#include "commands.h"
#include "game_exe.h"

#define TEMPLATE_ID "my_plugin"
#define TEMPLATE_NAME "My Plugin"
#define TEMPLATE_BUILD "\"0.30.7\""
#define ID_CAPACITY 25
#define NAME_CAPACITY 128
#define MAX_TEXT_FILE (1024 * 1024)

typedef struct Project {
    char id[ID_CAPACITY];
    char name[NAME_CAPACITY];
    const char* build;
} Project;


static bool sdk_folder(wchar_t* folder) {
    DWORD length = GetModuleFileNameW(NULL, folder, GAME_PATH_CAPACITY);

    if (length == 0 || length >= GAME_PATH_CAPACITY) {
        return false;
    }

    *wcsrchr(folder, L'\\') = L'\0';

    return true;
}

// "Wing Flaps-2" -> "wing_flaps_2": the mod id rules (a-z, 0-9, _, a letter first, 2-24 characters).
static bool id_from(const char* text, char* id) {
    size_t length = 0;

    for (const char* character = text; *character != '\0' && length < ID_CAPACITY - 1; character++) {
        char lowered = (char)tolower((unsigned char)*character);
        bool keeps = (lowered >= 'a' && lowered <= 'z') || (lowered >= '0' && lowered <= '9');
        bool separates = length > 0 && id[length - 1] != '_';

        if (keeps) {
            id[length++] = lowered;
        } else if (separates) {
            id[length++] = '_';
        }
    }

    while (length > 0 && id[length - 1] == '_') {
        length--;
    }

    id[length] = '\0';

    return length >= 2 && id[0] >= 'a' && id[0] <= 'z';
}

static char* read_text(const wchar_t* path) {
    FILE* file = _wfopen(path, L"rb");

    if (file == NULL) {
        return NULL;
    }

    char* text = malloc(MAX_TEXT_FILE + 1);
    size_t length = text != NULL ? fread(text, 1, MAX_TEXT_FILE, file) : 0;
    fclose(file);

    if (text != NULL) {
        text[length] = '\0';
    }

    return text;
}

static bool write_replaced(const wchar_t* path, const char* text, const Project* project) {
    FILE* file = _wfopen(path, L"wb");

    if (file == NULL) {
        return false;
    }

    const char* position = text;

    while (*position != '\0') {
        if (strncmp(position, TEMPLATE_ID, strlen(TEMPLATE_ID)) == 0) {
            fputs(project->id, file);
            position += strlen(TEMPLATE_ID);
        } else if (strncmp(position, TEMPLATE_NAME, strlen(TEMPLATE_NAME)) == 0) {
            fputs(project->name, file);
            position += strlen(TEMPLATE_NAME);
        } else if (strncmp(position, TEMPLATE_BUILD, strlen(TEMPLATE_BUILD)) == 0) {
            fprintf(file, "\"%s\"", project->build);
            position += strlen(TEMPLATE_BUILD);
        } else {
            fputc(*position++, file);
        }
    }

    return fclose(file) == 0;
}

static bool copy_file(const wchar_t* from, const wchar_t* to, const Project* project) {
    char* text = read_text(from);
    bool copied = text != NULL && write_replaced(to, text, project);

    free(text);

    if (!copied) {
        fwprintf(stderr, L"%ls couldn't be copied to %ls\n", from, to);
    }

    return copied;
}

static bool copy_folder(const wchar_t* from, const wchar_t* to, const Project* project) {
    wchar_t pattern[GAME_PATH_CAPACITY];
    WIN32_FIND_DATAW found;
    bool ok = CreateDirectoryW(to, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;

    swprintf(pattern, GAME_PATH_CAPACITY, L"%ls\\*", from);

    HANDLE search = FindFirstFileW(pattern, &found);

    while (ok && search != INVALID_HANDLE_VALUE) {
        bool is_folder = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool is_special = wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0;
        wchar_t source[GAME_PATH_CAPACITY];
        wchar_t target[GAME_PATH_CAPACITY];

        swprintf(source, GAME_PATH_CAPACITY, L"%ls\\%ls", from, found.cFileName);
        swprintf(target, GAME_PATH_CAPACITY, L"%ls\\%ls", to, found.cFileName);

        if (!is_special) {
            ok = is_folder ? copy_folder(source, target, project) : copy_file(source, target, project);
        }

        if (!FindNextFileW(search, &found)) {
            FindClose(search);
            search = INVALID_HANDLE_VALUE;
        }
    }

    return ok;
}

static bool folder_is_empty_or_missing(const wchar_t* folder) {
    DWORD attributes = GetFileAttributesW(folder);

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    wchar_t pattern[GAME_PATH_CAPACITY];
    WIN32_FIND_DATAW found;
    int entries = 0;

    swprintf(pattern, GAME_PATH_CAPACITY, L"%ls\\*", folder);

    HANDLE search = FindFirstFileW(pattern, &found);

    while (search != INVALID_HANDLE_VALUE) {
        entries++;

        if (!FindNextFileW(search, &found)) {
            FindClose(search);
            search = INVALID_HANDLE_VALUE;
        }
    }

    // "." and ".." only.
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) && entries <= 2;
}

// The build the player has, if this SDK knows it; otherwise the newest it knows.
static const char* current_build(const wchar_t* exe) {
    wchar_t path[GAME_PATH_CAPACITY];

    if (!game_exe_find(exe, path)) {
        return game_build_newest_name();
    }

    HMODULE module = LoadLibraryExW(path, NULL, DONT_RESOLVE_DLL_REFERENCES);
    GameBuild build = module != NULL ? game_build_identify(module) : (GameBuild){ 0, 0, NULL };

    if (module != NULL) {
        FreeLibrary(module);
    }

    return game_build_is_known(&build) ? build.name : game_build_newest_name();
}


static bool copy_header(const wchar_t* sdk, const wchar_t* include_folder, const wchar_t* name) {
    wchar_t from[GAME_PATH_CAPACITY];
    wchar_t to[GAME_PATH_CAPACITY];

    swprintf(from, GAME_PATH_CAPACITY, L"%ls\\include\\%ls", sdk, name);
    swprintf(to, GAME_PATH_CAPACITY, L"%ls\\%ls", include_folder, name);

    return CopyFileW(from, to, FALSE);
}


// projects\MyPlugin works when projects doesn't exist yet: each missing folder on the way is made.
static void make_parent_folders(const wchar_t* folder) {
    wchar_t path[GAME_PATH_CAPACITY];
    swprintf(path, GAME_PATH_CAPACITY, L"%ls", folder);

    for (wchar_t* at = path + 1; *at != L'\0'; at++) {
        if ((*at == L'\\' || *at == L'/') && at[-1] != L':') {
            wchar_t kept = *at;
            *at = L'\0';
            CreateDirectoryW(path, NULL);
            *at = kept;
        }
    }
}

static const char* const VSCODE_LAUNCH =
    "{\n"
    "    \"version\": \"0.2.0\",\n"
    "    \"configurations\": [\n"
    "        {\n"
    "            \"name\": \"Attach to MT2 (start the game from Steam first)\",\n"
    "            \"type\": \"cppvsdbg\",\n"
    "            \"request\": \"attach\",\n"
    "            \"processId\": \"${command:pickProcess}\"\n"
    "        },\n"
    "        {\n"
    "            \"name\": \"Start MT2 (needs steam_appid.txt with 486860 in the game folder)\",\n"
    "            \"type\": \"cppvsdbg\",\n"
    "            \"request\": \"launch\",\n"
    "            \"program\": \"%s/MT2.exe\",\n"
    "            \"cwd\": \"%s\"\n"
    "        }\n"
    "    ]\n"
    "}\n";

static const char* const VISUAL_STUDIO_LAUNCH =
    "{\n"
    "  \"version\": \"0.2.1\",\n"
    "  \"defaults\": {},\n"
    "  \"configurations\": [\n"
    "    {\n"
    "      \"type\": \"default\",\n"
    "      \"project\": \"CMakeLists.txt\",\n"
    "      \"projectTarget\": \"%s.dll\",\n"
    "      \"name\": \"Start MT2 (needs steam_appid.txt with 486860 in the game folder)\",\n"
    "      \"exe\": \"%s/MT2.exe\",\n"
    "      \"currentDir\": \"%s\"\n"
    "    }\n"
    "  ]\n"
    "}\n";

static bool write_text(const wchar_t* folder, const wchar_t* subfolder, const wchar_t* file_name, const char* text) {
    wchar_t path[GAME_PATH_CAPACITY];

    swprintf(path, GAME_PATH_CAPACITY, L"%ls\\%ls", folder, subfolder);
    CreateDirectoryW(path, NULL);
    swprintf(path, GAME_PATH_CAPACITY, L"%ls\\%ls\\%ls", folder, subfolder, file_name);

    FILE* file = _wfopen(path, L"wb");
    bool written = file != NULL && fputs(text, file) >= 0;

    if (file != NULL) {
        fclose(file);
    }

    return written;
}

// Debugging from VS Code or Visual Studio. The game asks Steam to restart it when it's started any other way (unless
// steam_appid.txt is next to it), which loses the debugger: attaching to the running game always works.
static void write_debug_settings(const wchar_t* folder, const wchar_t* exe, const char* id) {
    wchar_t path[GAME_PATH_CAPACITY];
    char game_folder[GAME_PATH_CAPACITY];
    char text[4096];

    if (!game_exe_find(exe, path)) {
        return;
    }

    WideCharToMultiByte(CP_UTF8, 0, path, -1, game_folder, sizeof game_folder, NULL, NULL);

    for (char* character = game_folder; *character != '\0'; character++) {
        *character = *character == '\\' ? '/' : *character;
    }

    char* last_slash = strrchr(game_folder, '/');

    if (last_slash != NULL) {
        *last_slash = '\0';
    }

    snprintf(text, sizeof text, VSCODE_LAUNCH, game_folder, game_folder);
    write_text(folder, L".vscode", L"launch.json", text);
    snprintf(text, sizeof text, VISUAL_STUDIO_LAUNCH, id, game_folder, game_folder);
    write_text(folder, L".vs", L"launch.vs.json", text);
}

// The game's classes as C++ (mt2game.hpp), when the game is there to read them from. A project works without it.
static void write_game_classes(const wchar_t* exe, const wchar_t* include_folder, const wchar_t* mappings_folder) {
    wchar_t path[GAME_PATH_CAPACITY];
    wchar_t header[GAME_PATH_CAPACITY];

    if (!game_exe_find(exe, path)) {
        return;
    }

    swprintf(header, GAME_PATH_CAPACITY, L"%ls\\mt2game.hpp", include_folder);
    printf("  ");
    fflush(stdout);

    if (command_headers(path, header, mappings_folder) != 0) {
        printf("  include\\mt2game.hpp wasn't made: make it later with mt2sdk headers include\\mt2game.hpp\n");
    }
}

int command_new(const wchar_t* folder, const wchar_t* name, const wchar_t* exe, const wchar_t* mappings_folder) {
    wchar_t sdk[GAME_PATH_CAPACITY];
    wchar_t template_folder[GAME_PATH_CAPACITY];
    wchar_t include_folder[GAME_PATH_CAPACITY];
    Project project;

    if (!sdk_folder(sdk)) {
        return 1;
    }

    swprintf(template_folder, GAME_PATH_CAPACITY, L"%ls\\template", sdk);

    const wchar_t* folder_name = wcsrchr(folder, L'\\') != NULL ? wcsrchr(folder, L'\\') + 1 : folder;
    char folder_name_utf8[NAME_CAPACITY];

    WideCharToMultiByte(CP_UTF8, 0, folder_name, -1, folder_name_utf8, sizeof folder_name_utf8, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, name != NULL ? name : folder_name, -1, project.name, sizeof project.name, NULL, NULL);

    if (!id_from(folder_name_utf8, project.id)) {
        fprintf(stderr, "'%s' can't become a mod id: name the folder with letters first, like wing_flaps\n", folder_name_utf8);

        return 1;
    }

    if (!folder_is_empty_or_missing(folder)) {
        fwprintf(stderr, L"%ls already exists and isn't empty\n", folder);

        return 1;
    }

    make_parent_folders(folder);
    project.build = current_build(exe);

    swprintf(include_folder, GAME_PATH_CAPACITY, L"%ls\\include", folder);

    bool made = copy_folder(template_folder, folder, &project);
    made = made && (CreateDirectoryW(include_folder, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
    // mt2loader.hpp is what plugins include; it wraps mt2loader.h.
    made = made && copy_header(sdk, include_folder, L"mt2loader.hpp");
    made = made && copy_header(sdk, include_folder, L"mt2loader.h");

    if (!made) {
        fwprintf(stderr, L"The project couldn't be made from %ls\n", sdk);

        return 1;
    }

    wprintf(L"Made %ls\n", folder);
    printf("  mod id %s, name \"%s\", game build %s\n", project.id, project.name, project.build);
    write_debug_settings(folder, exe, project.id);
    write_game_classes(exe, include_folder, mappings_folder);
    printf("  Open the folder in Visual Studio (File > Open > Folder) and build, or: cmake -B build && cmake --build build\n");
    printf("  Each build puts the DLL in mod\\native and copies mod\\ into the game's mod folder\n");

    return 0;
}
