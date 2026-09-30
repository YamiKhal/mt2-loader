#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "commands.h"

#define PATH_CAPACITY 1024
#define LINE_CAPACITY 2048
#define NAME_CAPACITY 512
#define SHOWN_WITHOUT_CLASS 60

typedef struct UnnamedField {
    char owner[NAME_CAPACITY];
    char offset[16];
    char type[NAME_CAPACITY];
    char first_user[NAME_CAPACITY];
    int uses;
    int functions;
} UnnamedField;

typedef struct FieldList {
    UnnamedField* items;
    int count;
    int capacity;
} FieldList;


// A line: class, offset, type, file, uses, functions, the first few functions (tabs between).
static void add_line(FieldList* list, char* line) {
    char* columns[7];
    int count = 0;

    line[strcspn(line, "\r\n")] = '\0';

    for (char* column = strtok(line, "\t"); column != NULL && count < 7; column = strtok(NULL, "\t")) {
        columns[count++] = column;
    }

    if (count < 7) {
        return;
    }

    if (list->count == list->capacity) {
        list->capacity = list->capacity == 0 ? 1024 : list->capacity * 2;
        list->items = realloc(list->items, (size_t)list->capacity * sizeof list->items[0]);
    }

    UnnamedField* field = &list->items[list->count++];

    snprintf(field->owner, sizeof field->owner, "%s", columns[0]);
    snprintf(field->offset, sizeof field->offset, "%s", columns[1]);
    snprintf(field->type, sizeof field->type, "%s", columns[2]);
    // The functions that use it, with ; between: the first is enough here.
    columns[6][strcspn(columns[6], ";")] = '\0';
    snprintf(field->first_user, sizeof field->first_user, "%s", columns[6]);
    field->uses = atoi(columns[4]);
    field->functions = atoi(columns[5]);
}

static int by_field(const void* left, const void* right) {
    const UnnamedField* a = left;
    const UnnamedField* b = right;
    int owners = strcmp(a->owner, b->owner);

    return owners != 0 ? owners : strcmp(a->offset, b->offset);
}

// A field used in several files has a line per file: one entry each, with the counts added up.
static void merge_files(FieldList* list) {
    qsort(list->items, (size_t)list->count, sizeof list->items[0], by_field);

    int kept = 0;

    for (int index = 0; index < list->count; index++) {
        UnnamedField* field = &list->items[index];
        UnnamedField* last = kept > 0 ? &list->items[kept - 1] : NULL;

        if (last == NULL || by_field(last, field) != 0) {
            list->items[kept++] = *field;
            continue;
        }

        if (strcmp(last->type, "?") == 0) {
            snprintf(last->type, sizeof last->type, "%s", field->type);
        }

        last->uses += field->uses;
        last->functions += field->functions;
    }

    list->count = kept;
}

static bool read_fields(const wchar_t* path, FieldList* list) {
    FILE* file = _wfopen(path, L"r");
    char line[LINE_CAPACITY];

    if (file == NULL) {
        fwprintf(stderr, L"%ls isn't there: it's written by mt2sdk decompile\n", path);

        return false;
    }

    while (fgets(line, sizeof line, file) != NULL) {
        add_line(list, line);
    }

    fclose(file);

    return true;
}

// Used in the most functions first: those are the fields most code reads better for.
static int by_use(const void* left, const void* right) {
    const UnnamedField* a = left;
    const UnnamedField* b = right;

    if (a->functions != b->functions) {
        return b->functions - a->functions;
    }

    return b->uses - a->uses;
}


// The standard library's classes (std::ios, std::wstring): their insides are the compiler's library's, not the game's,
// so they're only listed when asked for by name.
bool is_library_class(const char* owner) {
    return strncmp(owner, "std::", 5) == 0 || strncmp(owner, "__gnu_cxx::", 11) == 0;
}

int command_mappings_todo(const wchar_t* workspace, const wchar_t* only_class) {
    wchar_t full[PATH_CAPACITY];
    wchar_t path[PATH_CAPACITY];
    char wanted[NAME_CAPACITY] = "";
    FieldList list = { 0 };

    GetFullPathNameW(workspace, PATH_CAPACITY, full, NULL);
    swprintf(path, PATH_CAPACITY, L"%ls\\source\\unnamed_fields.tsv", full);

    if (only_class != NULL) {
        WideCharToMultiByte(CP_UTF8, 0, only_class, -1, wanted, sizeof wanted, NULL, NULL);
    }

    if (!read_fields(path, &list)) {
        return 1;
    }

    merge_files(&list);
    qsort(list.items, (size_t)list.count, sizeof list.items[0], by_use);

    int shown = 0;

    for (int index = 0; index < list.count; index++) {
        const UnnamedField* field = &list.items[index];

        if (wanted[0] != '\0' ? strcmp(field->owner, wanted) != 0 : shown == SHOWN_WITHOUT_CLASS || is_library_class(field->owner)) {
            continue;
        }

        if (shown == 0) {
            printf("%9s %6s  %s\n", "functions", "uses", "field");
        }

        printf("%9d %6d  %s +%s %s   first in %s\n", field->functions, field->uses, field->owner, field->offset, field->type,
            field->first_user);
        shown++;
    }

    if (wanted[0] == '\0' && list.count > shown) {
        printf("%d more; give a class to see all of its: mt2sdk mappings todo [workspace] <class>\n", list.count - shown);
    }

    if (wanted[0] != '\0' && shown == 0) {
        printf("%s has no unnamed fields the code uses\n", wanted);
    }

    free(list.items);

    return 0;
}
