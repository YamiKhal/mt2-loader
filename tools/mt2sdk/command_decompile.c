#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "commands.h"
#include "game_exe.h"
#include "game_image.h"
#include "ghidra.h"
#include "mappings.h"
#include "mappings_json.h"
#include "program_json.h"

#define PATH_CAPACITY 1024

typedef struct Workspace {
    wchar_t folder[PATH_CAPACITY];
    wchar_t project_folder[PATH_CAPACITY];
    wchar_t project_name[128];
    wchar_t project_file[PATH_CAPACITY];
    wchar_t analyzed_mark[PATH_CAPACITY];
    wchar_t source[PATH_CAPACITY];
    wchar_t program_json[PATH_CAPACITY];
    wchar_t mappings_json[PATH_CAPACITY];
    wchar_t inferred_json[PATH_CAPACITY];
    wchar_t output_log[PATH_CAPACITY];
    wchar_t scripts[PATH_CAPACITY];
} Workspace;


static bool is_file(const wchar_t* path) {
    DWORD attributes = GetFileAttributesW(path);

    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// The Ghidra scripts travel next to mt2sdk.exe, in its ghidra folder.
static bool find_scripts(wchar_t* scripts) {
    wchar_t script[PATH_CAPACITY];

    GetModuleFileNameW(NULL, scripts, PATH_CAPACITY);
    wchar_t* last_slash = wcsrchr(scripts, L'\\');

    if (last_slash != NULL) {
        *last_slash = L'\0';
    }

    wcscat(scripts, L"\\ghidra");
    swprintf(script, PATH_CAPACITY, L"%ls\\MT2Export.java", scripts);

    if (!is_file(script)) {
        fwprintf(stderr, L"The kit's Ghidra scripts aren't in %ls: keep the ghidra folder next to mt2sdk.exe\n", scripts);

        return false;
    }

    return true;
}

static bool prepare_workspace(const wchar_t* folder, const char* build, Workspace* workspace) {
    wchar_t full[PATH_CAPACITY];

    GetFullPathNameW(folder, PATH_CAPACITY, full, NULL);
    swprintf(workspace->folder, PATH_CAPACITY, L"%ls", full);
    swprintf(workspace->project_folder, PATH_CAPACITY, L"%ls\\ghidra", full);
    swprintf(workspace->project_name, 128, L"MT2_%hs", build);
    swprintf(workspace->project_file, PATH_CAPACITY, L"%ls\\%ls.gpr", workspace->project_folder, workspace->project_name);
    swprintf(workspace->analyzed_mark, PATH_CAPACITY, L"%ls\\%ls.analyzed", workspace->project_folder, workspace->project_name);
    swprintf(workspace->source, PATH_CAPACITY, L"%ls\\source", full);
    swprintf(workspace->program_json, PATH_CAPACITY, L"%ls\\program.json", full);
    swprintf(workspace->mappings_json, PATH_CAPACITY, L"%ls\\mappings.json", full);
    swprintf(workspace->inferred_json, PATH_CAPACITY, L"%ls\\inferred.json", full);
    swprintf(workspace->output_log, PATH_CAPACITY, L"%ls\\ghidra_output.txt", full);

    CreateDirectoryW(workspace->folder, NULL);
    CreateDirectoryW(workspace->project_folder, NULL);

    DWORD attributes = GetFileAttributesW(workspace->project_folder);

    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        fwprintf(stderr, L"%ls couldn't be made\n", workspace->project_folder);

        return false;
    }

    return find_scripts(workspace->scripts);
}

static void delete_tree(const wchar_t* folder) {
    wchar_t pattern[PATH_CAPACITY];
    WIN32_FIND_DATAW entry;

    swprintf(pattern, PATH_CAPACITY, L"%ls\\*", folder);
    HANDLE search = FindFirstFileW(pattern, &entry);

    while (search != INVALID_HANDLE_VALUE) {
        wchar_t path[PATH_CAPACITY];
        bool is_dot = wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0;

        swprintf(path, PATH_CAPACITY, L"%ls\\%ls", folder, entry.cFileName);

        if (!is_dot && (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            delete_tree(path);
        } else if (!is_dot) {
            DeleteFileW(path);
        }

        if (!FindNextFileW(search, &entry)) {
            break;
        }
    }

    if (search != INVALID_HANDLE_VALUE) {
        FindClose(search);
    }

    RemoveDirectoryW(folder);
}

// Only a folder this command wrote (it has index.txt) is emptied, so a function the game lost doesn't linger.
static void clear_old_export(const Workspace* workspace) {
    wchar_t index[PATH_CAPACITY];
    swprintf(index, PATH_CAPACITY, L"%ls\\index.txt", workspace->source);

    if (is_file(index)) {
        delete_tree(workspace->source);
    }
}

static bool write_mappings(const wchar_t* mappings_folder, const GameImage* image, const Workspace* workspace) {
    Mappings mappings;

    if (!mappings_load(mappings_folder, &mappings)) {
        return false;
    }

    bool usable = mappings.problem_count == 0 && mappings_json_write(&mappings, image, mappings_folder, workspace->mappings_json);

    if (mappings.problem_count > 0) {
        printf("Fix these first (mt2sdk mappings check), or leave out --mappings\n");
    }

    mappings_free(&mappings);

    return usable;
}

static int build_arguments(const Workspace* workspace, const wchar_t* exe_path, bool with_mappings, bool everything,
    const wchar_t** arguments) {
    int count = 0;

    arguments[count++] = workspace->project_folder;
    arguments[count++] = workspace->project_name;

    if (is_file(workspace->project_file)) {
        arguments[count++] = L"-process";
        arguments[count++] = L"MT2.exe";
    } else {
        arguments[count++] = L"-import";
        arguments[count++] = exe_path;
    }

    if (is_file(workspace->analyzed_mark)) {
        arguments[count++] = L"-noanalysis";
    }

    arguments[count++] = L"-scriptPath";
    arguments[count++] = workspace->scripts;

    // What the game and the mappings say first; then twice over, what the code says given that (a field's type
    // found in the first round tells what's read through it in the second), put under them.
    for (int round = 0; round < 3; round++) {
        if (round > 0) {
            arguments[count++] = L"-postScript";
            arguments[count++] = L"MT2Infer.java";
            arguments[count++] = workspace->program_json;
            arguments[count++] = workspace->inferred_json;
        }

        arguments[count++] = L"-postScript";
        arguments[count++] = L"MT2Apply.java";
        arguments[count++] = workspace->program_json;
        arguments[count++] = with_mappings ? workspace->mappings_json : L"-";

        if (round > 0) {
            arguments[count++] = workspace->inferred_json;
        }
    }

    arguments[count++] = L"-postScript";
    arguments[count++] = L"MT2Export.java";
    arguments[count++] = workspace->source;
    arguments[count++] = workspace->program_json;
    arguments[count++] = with_mappings ? workspace->mappings_json : L"-";
    arguments[count++] = everything ? L"all" : L"game";

    return count;
}

static void mark_analyzed(const Workspace* workspace) {
    FILE* mark = _wfopen(workspace->analyzed_mark, L"w");

    if (mark != NULL) {
        fputs("Ghidra's analysis of this build finished.\n", mark);
        fclose(mark);
    }
}


int command_decompile(const wchar_t* exe, const wchar_t* folder, const wchar_t* ghidra_folder, const wchar_t* mappings_folder,
    bool everything) {
    wchar_t exe_path[GAME_PATH_CAPACITY];
    char build[64];
    GameImage image;
    Workspace workspace;
    Ghidra ghidra;

    if (!game_exe_load_symbols(exe, exe_path) || !ghidra_find(ghidra_folder, &ghidra) || !game_image_load(exe_path, &image)) {
        return 1;
    }

    game_exe_build_name(exe_path, build, sizeof build);

    bool ready = prepare_workspace(folder, build, &workspace)
        && program_json_write(&image, build, workspace.program_json)
        && (mappings_folder == NULL || write_mappings(mappings_folder, &image, &workspace));

    game_image_free(&image);

    if (!ready) {
        return 1;
    }

    clear_old_export(&workspace);

    const wchar_t* arguments[GHIDRA_MAX_ARGUMENTS];
    int count = build_arguments(&workspace, exe_path, mappings_folder != NULL, everything, arguments);
    bool analyzed = is_file(workspace.analyzed_mark);

    wprintf(L"Ghidra: %ls\nJava: %ls\n", ghidra.folder, ghidra.java_home);

    if (!analyzed) {
        wprintf(L"Ghidra analyzes MT2.exe once, which takes a while (about 20 minutes on a fast PC); then it works out names and types from the code, about 20 minutes each run. "
                L"It's kept in %ls for next time.\n", workspace.project_folder);
    }

    wprintf(L"Everything Ghidra prints goes to %ls\n", workspace.output_log);
    fflush(stdout);

    if (!ghidra_run(&ghidra, arguments, count, workspace.output_log)) {
        wprintf(L"Ghidra stopped with a problem: see %ls\n", workspace.output_log);

        return 1;
    }

    if (!analyzed) {
        mark_analyzed(&workspace);
    }

    wprintf(L"The game's code as C++, laid out like its source: %ls (start with README.md)\nThe Ghidra project, to open in Ghidra: %ls\n",
        workspace.source, workspace.project_file);

    return 0;
}
