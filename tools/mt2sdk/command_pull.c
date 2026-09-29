#include <stdio.h>
#include <windows.h>

#include "commands.h"
#include "game_exe.h"
#include "game_image.h"
#include "ghidra.h"
#include "mappings.h"
#include "mappings_json.h"
#include "program_json.h"

#define PATH_CAPACITY 1024


static bool is_file(const wchar_t* path) {
    DWORD attributes = GetFileAttributesW(path);

    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static void scripts_folder(wchar_t* scripts) {
    GetModuleFileNameW(NULL, scripts, PATH_CAPACITY);
    wchar_t* last_slash = wcsrchr(scripts, L'\\');

    if (last_slash != NULL) {
        *last_slash = L'\0';
    }

    wcscat(scripts, L"\\ghidra");
}

static bool write_inputs(const wchar_t* exe_path, const wchar_t* mappings_folder, const char* build, const wchar_t* program_json,
    const wchar_t* mappings_json) {
    GameImage image;
    Mappings mappings;

    if (!game_image_load(exe_path, &image)) {
        return false;
    }

    bool written = program_json_write(&image, build, program_json) && mappings_load(mappings_folder, &mappings);

    if (written) {
        written = mappings.problem_count == 0 && mappings_json_write(&mappings, &image, mappings_folder, mappings_json);

        if (mappings.problem_count > 0) {
            printf("Fix these first: mt2sdk mappings check\n");
        }

        mappings_free(&mappings);
    }

    game_image_free(&image);

    return written;
}


int command_mappings_pull(const wchar_t* exe, const wchar_t* workspace, const wchar_t* mappings_folder, const wchar_t* ghidra_folder) {
    wchar_t exe_path[GAME_PATH_CAPACITY];
    wchar_t full[PATH_CAPACITY];
    wchar_t project_folder[PATH_CAPACITY];
    wchar_t project_name[128];
    wchar_t project_file[PATH_CAPACITY];
    wchar_t program_json[PATH_CAPACITY];
    wchar_t mappings_json[PATH_CAPACITY];
    wchar_t found_json[PATH_CAPACITY];
    wchar_t output_log[PATH_CAPACITY];
    wchar_t scripts[PATH_CAPACITY];
    char build[64];
    Ghidra ghidra;

    if (!game_exe_load_symbols(exe, exe_path) || !ghidra_find(ghidra_folder, &ghidra)) {
        return 1;
    }

    game_exe_build_name(exe_path, build, sizeof build);
    GetFullPathNameW(workspace, PATH_CAPACITY, full, NULL);
    swprintf(project_folder, PATH_CAPACITY, L"%ls\\ghidra", full);
    swprintf(project_name, 128, L"MT2_%hs", build);
    swprintf(project_file, PATH_CAPACITY, L"%ls\\%ls.gpr", project_folder, project_name);
    swprintf(program_json, PATH_CAPACITY, L"%ls\\program.json", full);
    swprintf(mappings_json, PATH_CAPACITY, L"%ls\\mappings.json", full);
    swprintf(found_json, PATH_CAPACITY, L"%ls\\found_in_ghidra.json", full);
    swprintf(output_log, PATH_CAPACITY, L"%ls\\ghidra_output.txt", full);
    scripts_folder(scripts);

    if (!is_file(project_file)) {
        fwprintf(stderr, L"%ls has no Ghidra project for build %hs: make it with mt2sdk decompile first\n", full, build);

        return 1;
    }

    if (!write_inputs(exe_path, mappings_folder, build, program_json, mappings_json)) {
        return 1;
    }

    const wchar_t* arguments[] = {
        project_folder, project_name, L"-process", L"MT2.exe", L"-noanalysis", L"-readOnly", L"-scriptPath", scripts,
        L"-postScript", L"MT2Import.java", found_json, program_json, mappings_json,
    };

    printf("Reading what was named in Ghidra (close Ghidra first: it locks the project)\n");
    fflush(stdout);

    if (!ghidra_run(&ghidra, arguments, (int)(sizeof arguments / sizeof arguments[0]), output_log)) {
        wprintf(L"Ghidra stopped with a problem: see %ls\n", output_log);

        return 1;
    }

    return command_mappings_import(exe, found_json, mappings_folder);
}
