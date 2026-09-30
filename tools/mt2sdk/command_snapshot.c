#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "commands.h"
#include "readability.h"

#define PATH_CAPACITY 1024
#define LINE_CAPACITY 1024
#define MAX_FILES 64
#define LIST_CAPACITY 8192

// Files that together show most of what the export does: a map object, a class with mapped fields, a big map entity,
// a window, engine code and a template's copies.
static const char* const DEFAULT_FILES[] = {
    "MMO_District.cpp", "MMO_Blueprint.cpp", "MMO_NPC.cpp", "MMO_BoostWindow.cpp", "VS_DisplayList.cpp", "_templates/vsArray.cpp",
};

typedef struct Snapshot {
    wchar_t workspace[PATH_CAPACITY];
    wchar_t source[PATH_CAPACITY];
    wchar_t saved[PATH_CAPACITY];
    wchar_t list_path[PATH_CAPACITY];
    wchar_t diff_path[PATH_CAPACITY];
    char names[MAX_FILES][PATH_CAPACITY];
    wchar_t paths[MAX_FILES][PATH_CAPACITY];
    int count;
} Snapshot;


static bool is_file(const wchar_t* path) {
    DWORD attributes = GetFileAttributesW(path);

    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static void write_default_list(const wchar_t* path) {
    FILE* file = _wfopen(path, L"w");

    if (file == NULL) {
        return;
    }

    fputs("# The files mt2sdk snapshot compares: a name, or as much of the path as tells it apart. One per line.\n", file);

    for (size_t index = 0; index < sizeof DEFAULT_FILES / sizeof DEFAULT_FILES[0]; index++) {
        fprintf(file, "%s\n", DEFAULT_FILES[index]);
    }

    fclose(file);
}

static void read_list(Snapshot* snapshot) {
    FILE* file = _wfopen(snapshot->list_path, L"r");
    char line[LINE_CAPACITY];

    while (file != NULL && snapshot->count < MAX_FILES && fgets(line, sizeof line, file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] != '\0' && line[0] != '#') {
            snprintf(snapshot->names[snapshot->count++], PATH_CAPACITY, "%s", line);
        }
    }

    if (file != NULL) {
        fclose(file);
    }
}

// Whether the path (relative to the source folder, with /) is the file the list names.
static bool names_path(const char* name, const char* path) {
    char wanted[PATH_CAPACITY];
    size_t path_length = strlen(path);

    snprintf(wanted, sizeof wanted, "%s%s", name, strstr(name, ".cpp") != NULL ? "" : ".cpp");

    size_t wanted_length = strlen(wanted);

    if (strcmp(path, wanted) == 0) {
        return true;
    }

    return path_length > wanted_length && strcmp(path + path_length - wanted_length, wanted) == 0 && path[path_length - wanted_length - 1] == '/';
}

static void find_in(const wchar_t* folder, size_t root_length, Snapshot* snapshot) {
    wchar_t pattern[PATH_CAPACITY];
    WIN32_FIND_DATAW entry;

    swprintf(pattern, PATH_CAPACITY, L"%ls\\*", folder);
    HANDLE search = FindFirstFileW(pattern, &entry);

    while (search != INVALID_HANDLE_VALUE) {
        wchar_t full[PATH_CAPACITY];
        char relative[PATH_CAPACITY];
        bool is_dot = wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0;

        swprintf(full, PATH_CAPACITY, L"%ls\\%ls", folder, entry.cFileName);

        if (!is_dot && (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            find_in(full, root_length, snapshot);
        } else if (!is_dot) {
            WideCharToMultiByte(CP_UTF8, 0, full + root_length + 1, -1, relative, sizeof relative, NULL, NULL);

            for (char* at = relative; *at != '\0'; at++) {
                *at = *at == '\\' ? '/' : *at;
            }

            for (int index = 0; index < snapshot->count; index++) {
                if (snapshot->paths[index][0] == L'\0' && names_path(snapshot->names[index], relative)) {
                    MultiByteToWideChar(CP_UTF8, 0, relative, -1, snapshot->paths[index], PATH_CAPACITY);
                }
            }
        }

        if (!FindNextFileW(search, &entry)) {
            break;
        }
    }

    if (search != INVALID_HANDLE_VALUE) {
        FindClose(search);
    }
}

static bool prepare(const wchar_t* workspace, Snapshot* snapshot) {
    wchar_t full[PATH_CAPACITY];

    memset(snapshot, 0, sizeof *snapshot);
    GetFullPathNameW(workspace, PATH_CAPACITY, full, NULL);
    swprintf(snapshot->workspace, PATH_CAPACITY, L"%ls", full);
    swprintf(snapshot->source, PATH_CAPACITY, L"%ls\\source", full);
    swprintf(snapshot->saved, PATH_CAPACITY, L"%ls\\snapshots", full);
    swprintf(snapshot->list_path, PATH_CAPACITY, L"%ls\\snapshots\\files.txt", full);
    swprintf(snapshot->diff_path, PATH_CAPACITY, L"%ls\\snapshots\\changes.diff", full);

    CreateDirectoryW(snapshot->saved, NULL);

    if (!is_file(snapshot->list_path)) {
        write_default_list(snapshot->list_path);
    }

    read_list(snapshot);
    find_in(snapshot->source, wcslen(snapshot->source), snapshot);

    for (int index = 0; index < snapshot->count; index++) {
        if (snapshot->paths[index][0] == L'\0') {
            fprintf(stderr, "%s isn't in the exported source (snapshots\\files.txt names it)\n", snapshot->names[index]);

            return false;
        }
    }

    return snapshot->count > 0;
}

static void saved_path(const Snapshot* snapshot, const wchar_t* relative, wchar_t* path) {
    swprintf(path, PATH_CAPACITY, L"%ls\\%ls", snapshot->saved, relative);

    for (wchar_t* at = path; *at != L'\0'; at++) {
        *at = *at == L'/' ? L'\\' : *at;
    }
}

static void source_path(const Snapshot* snapshot, const wchar_t* relative, wchar_t* path) {
    swprintf(path, PATH_CAPACITY, L"%ls\\%ls", snapshot->source, relative);

    for (wchar_t* at = path; *at != L'\0'; at++) {
        *at = *at == L'/' ? L'\\' : *at;
    }
}

static void make_folders_for(const wchar_t* path) {
    wchar_t folder[PATH_CAPACITY];

    swprintf(folder, PATH_CAPACITY, L"%ls", path);

    for (wchar_t* at = folder + 3; *at != L'\0'; at++) {
        if (*at == L'\\') {
            *at = L'\0';
            CreateDirectoryW(folder, NULL);
            *at = L'\\';
        }
    }
}

// The .h beside a .cpp.
static void header_of(const wchar_t* cpp, wchar_t* header) {
    swprintf(header, PATH_CAPACITY, L"%ls", cpp);

    wchar_t* dot = wcsrchr(header, L'.');

    if (dot != NULL) {
        wcscpy(dot, L".h");
    }
}

static int save(const Snapshot* snapshot) {
    for (int index = 0; index < snapshot->count; index++) {
        wchar_t from[PATH_CAPACITY];
        wchar_t to[PATH_CAPACITY];
        wchar_t from_header[PATH_CAPACITY];
        wchar_t to_header[PATH_CAPACITY];

        source_path(snapshot, snapshot->paths[index], from);
        saved_path(snapshot, snapshot->paths[index], to);
        header_of(from, from_header);
        header_of(to, to_header);
        make_folders_for(to);

        if (!CopyFileW(from, to, FALSE)) {
            fwprintf(stderr, L"%ls couldn't be copied\n", from);

            return 1;
        }

        if (is_file(from_header)) {
            CopyFileW(from_header, to_header, FALSE);
        }

        wprintf(L"  %ls\n", snapshot->paths[index]);
    }

    wprintf(L"Saved in %ls. mt2sdk snapshot check shows what a change to the scripts or mappings does to them.\n", snapshot->saved);

    return 0;
}

// git diff --no-index shows the change; its --numstat line says how many lines went in and out.
static void compare_with_git(const wchar_t* before, const wchar_t* after, const wchar_t* diff_path, int* added, int* removed) {
    wchar_t command[PATH_CAPACITY * 3];
    char line[LINE_CAPACITY];

    *added = -1;
    *removed = -1;
    swprintf(command, sizeof command / sizeof command[0], L"git diff --no-index --numstat \"%ls\" \"%ls\" 2>nul", before, after);

    FILE* output = _wpopen(command, L"r");

    if (output == NULL) {
        return;
    }

    *added = 0;
    *removed = 0;

    if (fgets(line, sizeof line, output) != NULL) {
        sscanf(line, "%d %d", added, removed);
    }

    _pclose(output);

    swprintf(command, sizeof command / sizeof command[0], L"git diff --no-index \"%ls\" \"%ls\" >> \"%ls\" 2>nul", before, after, diff_path);
    _wsystem(command);
}

static int check(const Snapshot* snapshot, const wchar_t* exe, const wchar_t* ghidra, const wchar_t* mappings) {
    wchar_t list[LIST_CAPACITY] = L"";

    for (int index = 0; index < snapshot->count; index++) {
        wchar_t saved[PATH_CAPACITY];

        saved_path(snapshot, snapshot->paths[index], saved);

        if (!is_file(saved)) {
            fwprintf(stderr, L"No snapshot of %ls yet: mt2sdk snapshot save %ls\n", snapshot->paths[index], snapshot->workspace);

            return 1;
        }

        wcscat(list, index > 0 ? L"," : L"");
        wcscat(list, snapshot->paths[index]);
    }

    if (command_decompile(exe, snapshot->workspace, ghidra, mappings, false, list) != 0) {
        return 1;
    }

    DeleteFileW(snapshot->diff_path);
    printf("%-40s %8s %8s   %s\n", "file", "added", "removed", "machine-like words");

    for (int index = 0; index < snapshot->count; index++) {
        wchar_t before[PATH_CAPACITY];
        wchar_t after[PATH_CAPACITY];
        wchar_t before_header[PATH_CAPACITY];
        wchar_t after_header[PATH_CAPACITY];
        Readability old_readability;
        Readability new_readability;
        int added;
        int removed;
        int header_added;
        int header_removed;

        saved_path(snapshot, snapshot->paths[index], before);
        source_path(snapshot, snapshot->paths[index], after);
        header_of(before, before_header);
        header_of(after, after_header);
        readability_of_file(before, &old_readability);
        readability_of_file(after, &new_readability);
        compare_with_git(before, after, snapshot->diff_path, &added, &removed);

        if (is_file(before_header) && is_file(after_header)) {
            compare_with_git(before_header, after_header, snapshot->diff_path, &header_added, &header_removed);
            added += header_added > 0 ? header_added : 0;
            removed += header_removed > 0 ? header_removed : 0;
        }

        printf("%-40s %8d %8d   %d -> %d\n", snapshot->names[index], added, removed, readability_total(&old_readability),
            readability_total(&new_readability));
    }

    wprintf(L"Every change: %ls\nKeep them as the new snapshot: mt2sdk snapshot save %ls\n", snapshot->diff_path, snapshot->workspace);

    return 0;
}


int command_snapshot(const wchar_t* action, const wchar_t* workspace, const wchar_t* exe, const wchar_t* ghidra, const wchar_t* mappings) {
    Snapshot* snapshot = calloc(1, sizeof *snapshot);
    int result = 2;

    if (!prepare(workspace, snapshot)) {
        result = 1;
    } else if (wcscmp(action, L"save") == 0) {
        result = save(snapshot);
    } else if (wcscmp(action, L"check") == 0) {
        result = check(snapshot, exe, ghidra, mappings);
    }

    free(snapshot);

    return result;
}
