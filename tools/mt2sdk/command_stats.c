#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "commands.h"
#include "readability.h"

#define PATH_CAPACITY 1024
#define LINE_CAPACITY 2048
#define WORST_SHOWN 15

typedef struct FileReadability {
    char path[PATH_CAPACITY];
    Readability readability;
} FileReadability;

typedef struct FileList {
    FileReadability* items;
    int count;
    int capacity;
} FileList;


static bool ends_with(const wchar_t* text, const wchar_t* end) {
    size_t text_length = wcslen(text);
    size_t end_length = wcslen(end);

    return text_length >= end_length && _wcsicmp(text + text_length - end_length, end) == 0;
}

static void add_file(FileList* list, const wchar_t* full, size_t root_length) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity == 0 ? 512 : list->capacity * 2;
        list->items = realloc(list->items, (size_t)list->capacity * sizeof list->items[0]);
    }

    FileReadability* item = &list->items[list->count];

    if (!readability_of_file(full, &item->readability)) {
        return;
    }

    WideCharToMultiByte(CP_UTF8, 0, full + root_length + 1, -1, item->path, PATH_CAPACITY, NULL, NULL);

    for (char* at = item->path; *at != '\0'; at++) {
        *at = *at == '\\' ? '/' : *at;
    }

    list->count++;
}

static void read_folder(const wchar_t* folder, size_t root_length, FileList* list) {
    wchar_t pattern[PATH_CAPACITY];
    WIN32_FIND_DATAW entry;

    swprintf(pattern, PATH_CAPACITY, L"%ls\\*", folder);
    HANDLE search = FindFirstFileW(pattern, &entry);

    while (search != INVALID_HANDLE_VALUE) {
        wchar_t full[PATH_CAPACITY];
        bool is_dot = wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0;

        swprintf(full, PATH_CAPACITY, L"%ls\\%ls", folder, entry.cFileName);

        if (!is_dot && (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            read_folder(full, root_length, list);
        } else if (!is_dot && ends_with(entry.cFileName, L".cpp")) {
            add_file(list, full, root_length);
        }

        if (!FindNextFileW(search, &entry)) {
            break;
        }
    }

    if (search != INVALID_HANDLE_VALUE) {
        FindClose(search);
    }
}

static void add_up(const FileList* list, Readability* total) {
    memset(total, 0, sizeof *total);

    for (int index = 0; index < list->count; index++) {
        const Readability* file = &list->items[index].readability;

        total->lines += file->lines;
        total->functions += file->functions;

        for (int kind = 0; kind < READABILITY_KIND_COUNT; kind++) {
            total->counts[kind] += file->counts[kind];
        }
    }
}

// The totals line of the last stats.tsv: "total  lines  functions  counts...".
static bool read_last_total(const wchar_t* path, Readability* last) {
    FILE* file = _wfopen(path, L"r");
    char line[LINE_CAPACITY];
    bool found = false;

    while (file != NULL && !found && fgets(line, sizeof line, file) != NULL) {
        if (strncmp(line, "total\t", 6) != 0) {
            continue;
        }

        char* at = line + 6;
        last->lines = (int)strtol(at, &at, 10);
        last->functions = (int)strtol(at, &at, 10);

        for (int kind = 0; kind < READABILITY_KIND_COUNT; kind++) {
            last->counts[kind] = (int)strtol(at, &at, 10);
        }

        found = true;
    }

    if (file != NULL) {
        fclose(file);
    }

    return found;
}

static void write_row(FILE* file, const char* name, const Readability* readability) {
    fprintf(file, "%s\t%d\t%d", name, readability->lines, readability->functions);

    for (int kind = 0; kind < READABILITY_KIND_COUNT; kind++) {
        fprintf(file, "\t%d", readability->counts[kind]);
    }

    fputc('\n', file);
}

static bool write_stats(const wchar_t* path, const FileList* list, const Readability* total) {
    FILE* file = _wfopen(path, L"w");

    if (file == NULL) {
        return false;
    }

    fprintf(file, "file\tlines\tfunctions");

    for (int kind = 0; kind < READABILITY_KIND_COUNT; kind++) {
        fprintf(file, "\t%s", READABILITY_NAMES[kind]);
    }

    fputc('\n', file);
    write_row(file, "total", total);

    for (int index = 0; index < list->count; index++) {
        write_row(file, list->items[index].path, &list->items[index].readability);
    }

    fclose(file);

    return true;
}

static void print_row(const char* name, int now, const int* before) {
    if (before == NULL || now == *before) {
        printf("  %-20s %10d\n", name, now);
    } else {
        printf("  %-20s %10d  %+d\n", name, now, now - *before);
    }
}

static int by_total(const void* left, const void* right) {
    return readability_total(&((const FileReadability*)right)->readability) - readability_total(&((const FileReadability*)left)->readability);
}


int command_stats(const wchar_t* workspace) {
    wchar_t full[PATH_CAPACITY];
    wchar_t source[PATH_CAPACITY];
    wchar_t stats_path[PATH_CAPACITY];
    FileList list = { 0 };
    Readability total;
    Readability last;

    GetFullPathNameW(workspace, PATH_CAPACITY, full, NULL);
    swprintf(source, PATH_CAPACITY, L"%ls\\source", full);
    swprintf(stats_path, PATH_CAPACITY, L"%ls\\stats.tsv", full);

    read_folder(source, wcslen(source), &list);

    if (list.count == 0) {
        fwprintf(stderr, L"No source in %ls: it's written by mt2sdk decompile\n", source);

        return 1;
    }

    add_up(&list, &total);

    bool has_last = read_last_total(stats_path, &last);

    printf("What still reads like machine code, in %d files (change since the last count):\n", list.count);

    for (int kind = 0; kind < READABILITY_KIND_COUNT; kind++) {
        print_row(READABILITY_NAMES[kind], total.counts[kind], has_last ? &last.counts[kind] : NULL);
    }

    print_row("lines", total.lines, has_last ? &last.lines : NULL);
    print_row("functions", total.functions, has_last ? &last.functions : NULL);

    qsort(list.items, (size_t)list.count, sizeof list.items[0], by_total);
    printf("Most left in:\n");

    for (int index = 0; index < list.count && index < WORST_SHOWN; index++) {
        printf("  %10d  %s\n", readability_total(&list.items[index].readability), list.items[index].path);
    }

    bool written = write_stats(stats_path, &list, &total);
    free(list.items);

    return written ? 0 : 1;
}
