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
// --export: every file again, over what the last whole run worked out (MT2Apply and the export, not MT2Infer).
#define EVERY_FILE L"*"

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
    const wchar_t* only_file, const wchar_t** arguments) {
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
    // found in the first round tells what's read through it in the second), put under them. One file only takes the
    // mappings again, over what the last whole run worked out.
    int rounds = only_file != NULL ? 1 : 3;

    for (int round = 0; round < rounds; round++) {
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

        if (round > 0 || only_file != NULL) {
            arguments[count++] = workspace->inferred_json;
        }
    }

    arguments[count++] = L"-postScript";
    arguments[count++] = L"MT2Export.java";
    arguments[count++] = workspace->source;
    arguments[count++] = workspace->program_json;
    arguments[count++] = with_mappings ? workspace->mappings_json : L"-";
    arguments[count++] = everything ? L"all" : L"game";

    if (only_file != NULL && wcscmp(only_file, EVERY_FILE) != 0) {
        arguments[count++] = only_file;
    }

    return count;
}

static void mark_analyzed(const Workspace* workspace) {
    FILE* mark = _wfopen(workspace->analyzed_mark, L"w");

    if (mark != NULL) {
        fputs("Ghidra's analysis of this build finished.\n", mark);
        fclose(mark);
    }
}


// How long the log is: Ghidra adds to it, and only what this run adds says how it went.
static long log_length(const wchar_t* path) {
    FILE* file = _wfopen(path, L"rb");

    if (file == NULL) {
        return 0;
    }

    fseek(file, 0, SEEK_END);

    long length = ftell(file);

    fclose(file);

    return length;
}

// A script that fails doesn't stop Ghidra, and leaves the files it didn't get to as they were: prints the error's
// first lines when this run's part of the log has one.
static bool script_failed(const wchar_t* path, long from) {
    FILE* file = _wfopen(path, L"rb");
    char line[1024];
    int shown = 0;

    if (file == NULL) {
        return false;
    }

    fseek(file, from, SEEK_SET);

    while (fgets(line, sizeof line, file) != NULL && shown < 6) {
        if (shown > 0 || strstr(line, "SCRIPT ERROR") != NULL) {
            fputs(line, stderr);
            shown++;
        }
    }

    fclose(file);

    return shown > 0;
}

// The mappings folder a run used is kept in the workspace, and a run without --mappings uses it again: forgetting it
// would lose every name. NULL when no run has had one.
static const wchar_t* remembered_mappings(const wchar_t* folder, const wchar_t* given, wchar_t* remembered) {
    wchar_t path[GAME_PATH_CAPACITY];
    char text[GAME_PATH_CAPACITY * 3] = "";

    swprintf(path, GAME_PATH_CAPACITY, L"%ls\\mappings_folder.txt", folder);

    if (given != NULL) {
        GetFullPathNameW(given, GAME_PATH_CAPACITY, remembered, NULL);
        WideCharToMultiByte(CP_UTF8, 0, remembered, -1, text, sizeof text, NULL, NULL);

        FILE* file = _wfopen(path, L"wb");

        if (file != NULL) {
            fputs(text, file);
            fclose(file);
        }

        return remembered;
    }

    FILE* file = _wfopen(path, L"rb");

    if (file == NULL) {
        return NULL;
    }

    size_t length = fread(text, 1, sizeof text - 1, file);

    fclose(file);
    text[length] = '\0';

    if (length == 0 || MultiByteToWideChar(CP_UTF8, 0, text, -1, remembered, GAME_PATH_CAPACITY) == 0) {
        return NULL;
    }

    wprintf(L"Mappings: %ls (from the last run; --mappings for others)\n", remembered);

    return remembered;
}

int command_decompile(const wchar_t* exe, const wchar_t* folder, const wchar_t* ghidra_folder, const wchar_t* given_mappings,
    bool everything, const wchar_t* only_file) {
    wchar_t remembered[GAME_PATH_CAPACITY];
    wchar_t exe_path[GAME_PATH_CAPACITY];
    char build[64];
    GameImage image;
    Workspace workspace;
    Ghidra ghidra;

    if (!game_exe_load_symbols(exe, exe_path) || !ghidra_find(ghidra_folder, &ghidra) || !game_image_load(exe_path, &image)) {
        return 1;
    }

    game_exe_build_name(exe_path, build, sizeof build);

    bool ready = prepare_workspace(folder, build, &workspace);
    const wchar_t* mappings_folder = ready ? remembered_mappings(folder, given_mappings, remembered) : NULL;

    ready = ready && program_json_write(&image, build, workspace.program_json)
        && (mappings_folder == NULL || write_mappings(mappings_folder, &image, &workspace));

    game_image_free(&image);

    if (!ready) {
        return 1;
    }

    bool analyzed = is_file(workspace.analyzed_mark);

    if (only_file != NULL && (!analyzed || !is_file(workspace.inferred_json))) {
        fwprintf(stderr, L"One file needs a whole run first: mt2sdk decompile %ls\n", folder);

        return 1;
    }

    if (only_file == NULL || wcscmp(only_file, EVERY_FILE) == 0) {
        clear_old_export(&workspace);
    }

    const wchar_t* arguments[GHIDRA_MAX_ARGUMENTS];
    int count = build_arguments(&workspace, exe_path, mappings_folder != NULL, everything, only_file, arguments);

    wprintf(L"Ghidra: %ls\nJava: %ls\n", ghidra.folder, ghidra.java_home);

    if (!analyzed) {
        wprintf(L"Ghidra analyzes MT2.exe once, which takes a while (about 20 minutes on a fast PC); then it works out names and types from the code, about 20 minutes each run. "
                L"It's kept in %ls for next time.\n", workspace.project_folder);
    }

    wprintf(L"Everything Ghidra prints goes to %ls\n", workspace.output_log);
    fflush(stdout);

    long log_start = log_length(workspace.output_log);

    if (!ghidra_run(&ghidra, arguments, count, workspace.output_log)) {
        wprintf(L"Ghidra stopped with a problem: see %ls\n", workspace.output_log);

        return 1;
    }

    if (script_failed(workspace.output_log, log_start)) {
        wprintf(L"A script failed, so some files weren't rebuilt: see %ls\n", workspace.output_log);

        return 1;
    }

    if (!analyzed) {
        mark_analyzed(&workspace);
    }

    if (only_file != NULL && wcscmp(only_file, EVERY_FILE) != 0) {
        return 0;
    }

    wprintf(L"The game's code as C++, laid out like its source: %ls (start with README.md)\nThe Ghidra project, to open in Ghidra: %ls\n",
        workspace.source, workspace.project_file);

    return 0;
}
