#include "mappings.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define INDENT 4
#define MAX_LINE 4096
#define PATH_CAPACITY 1024

typedef struct Parser {
    Mappings* mappings;
    const char* file;
    int line;
    Entry* top;
    Entry* member;
} Parser;

static const char* const CLASS_NOTES[] = { "doc", "size", "seen", "unsure", NULL };
static const char* const FIELD_NOTES[] = { "doc", "seen", "unsure", NULL };
static const char* const FUNCTION_NOTES[] = { "returns", "param", "doc", "seen", "unsure", "inlined", NULL };
static const char* const GLOBAL_NOTES[] = { "type", "doc", "seen", "unsure", NULL };


static char* copy_text(const char* text) {
    size_t length = strlen(text);
    char* copy = malloc(length + 1);

    if (copy != NULL) {
        memcpy(copy, text, length + 1);
    }

    return copy;
}

static bool is_listed(const char* const* keywords, const char* keyword) {
    for (; *keywords != NULL; keywords++) {
        if (strcmp(*keywords, keyword) == 0) {
            return true;
        }
    }

    return false;
}

static const char* const* notes_for(EntryKind kind) {
    switch (kind) {
    case ENTRY_CLASS:
        return CLASS_NOTES;
    case ENTRY_FIELD:
        return FIELD_NOTES;
    case ENTRY_METHOD:
    case ENTRY_FUNCTION:
        return FUNCTION_NOTES;
    case ENTRY_GLOBAL:
        return GLOBAL_NOTES;
    }

    return FIELD_NOTES;
}

static bool parse_hex(const char* text, uint32_t* value) {
    if (strncmp(text, "0x", 2) != 0 || !isxdigit((unsigned char)text[2])) {
        return false;
    }

    char* end = NULL;
    unsigned long parsed = strtoul(text + 2, &end, 16);

    if (*end != '\0' && !isspace((unsigned char)*end)) {
        return false;
    }

    *value = (uint32_t)parsed;

    return true;
}

// A place is a name, "+0x", and an offset: mmoCharacter::EquipWeaponModel(bool)+0x5a
static bool is_place(const char* value) {
    const char* plus = strstr(value, "+0x");
    uint32_t offset = 0;

    while (plus != NULL && strstr(plus + 1, "+0x") != NULL) {
        plus = strstr(plus + 1, "+0x");
    }

    return plus != NULL && plus != value && parse_hex(plus + 1, &offset) && strchr(plus + 3, ' ') == NULL;
}

static bool note_value_is_valid(Parser* parser, const char* keyword, const char* value) {
    uint32_t number = 0;

    if (value[0] == '\0') {
        mappings_problem(parser->mappings, parser->file, parser->line, "%s needs a value", keyword);

        return false;
    }

    if (strcmp(keyword, "size") == 0 && !parse_hex(value, &number)) {
        mappings_problem(parser->mappings, parser->file, parser->line, "size is a hex number, like 0x9c0");

        return false;
    }

    if ((strcmp(keyword, "seen") == 0 || strcmp(keyword, "inlined") == 0) && !is_place(value)) {
        mappings_problem(parser->mappings, parser->file, parser->line,
            "%s is a function and the instruction's offset, like mmoCharacter::EquipWeaponModel(bool)+0x5a", keyword);

        return false;
    }

    if (strcmp(keyword, "param") == 0) {
        char name[256] = "";
        int index = 0;

        if (sscanf(value, "%d %255s", &index, name) != 2 || index < 1) {
            mappings_problem(parser->mappings, parser->file, parser->line, "param is a number from 1, then a name: param 1 part");

            return false;
        }
    }

    return true;
}

static void add_note(Parser* parser, Entry* entry, const char* keyword, const char* value) {
    if (!is_listed(notes_for(entry->kind), keyword)) {
        mappings_problem(parser->mappings, parser->file, parser->line, "%s doesn't belong under %s %s", keyword,
            entry->kind == ENTRY_CLASS ? "class" : entry->kind == ENTRY_FIELD ? "field" : entry->kind == ENTRY_GLOBAL ? "global" : "a function",
            entry->name);

        return;
    }

    if (!note_value_is_valid(parser, keyword, value)) {
        return;
    }

    Note* notes = realloc(entry->notes, (size_t)(entry->note_count + 1) * sizeof *notes);

    if (notes == NULL) {
        return;
    }

    entry->notes = notes;
    entry->notes[entry->note_count++] = (Note){ copy_text(keyword), copy_text(value), parser->line };
}

static Entry* add_entry(Entry** entries, int* count, EntryKind kind, const char* name, const Parser* parser) {
    Entry* grown = realloc(*entries, (size_t)(*count + 1) * sizeof *grown);

    if (grown == NULL) {
        return NULL;
    }

    *entries = grown;

    Entry* entry = &grown[(*count)++];
    memset(entry, 0, sizeof *entry);
    entry->kind = kind;
    entry->name = copy_text(name);
    entry->file = parser->file;
    entry->line = parser->line;

    return entry;
}

static void parse_top(Parser* parser, const char* keyword, const char* value) {
    EntryKind kind = ENTRY_CLASS;

    if (strcmp(keyword, "function") == 0) {
        kind = ENTRY_FUNCTION;
    } else if (strcmp(keyword, "global") == 0) {
        kind = ENTRY_GLOBAL;
    } else if (strcmp(keyword, "class") != 0) {
        mappings_problem(parser->mappings, parser->file, parser->line, "a file's top lines are class, function or global, not %s", keyword);
        parser->top = NULL;

        return;
    }

    if (value[0] == '\0') {
        mappings_problem(parser->mappings, parser->file, parser->line, "%s needs a name", keyword);
        parser->top = NULL;

        return;
    }

    parser->top = add_entry(&parser->mappings->entries, &parser->mappings->entry_count, kind, value, parser);
    parser->member = NULL;
}

static void parse_field(Parser* parser, const char* value) {
    char name[256] = "";
    char offset_text[64] = "";
    int used = 0;
    uint32_t offset = 0;

    if (sscanf(value, "%63s %255s %n", offset_text, name, &used) != 2 || !parse_hex(offset_text, &offset) || value[used] == '\0') {
        mappings_problem(parser->mappings, parser->file, parser->line, "field is an offset, a name and a type: field 0x170 shard mmoShard*");
        parser->member = NULL;

        return;
    }

    parser->member = add_entry(&parser->top->members, &parser->top->member_count, ENTRY_FIELD, name, parser);

    if (parser->member != NULL) {
        parser->member->offset = offset;
        parser->member->type = copy_text(value + used);
    }
}

static void parse_member(Parser* parser, const char* keyword, const char* value) {
    bool in_class = parser->top->kind == ENTRY_CLASS;

    if (in_class && strcmp(keyword, "field") == 0) {
        parse_field(parser, value);

        return;
    }

    if (in_class && strcmp(keyword, "method") == 0) {
        if (value[0] == '\0' || strchr(value, '(') == NULL) {
            mappings_problem(parser->mappings, parser->file, parser->line, "method is a name with its (parameters): method GetColor() const");
            parser->member = NULL;

            return;
        }

        parser->member = add_entry(&parser->top->members, &parser->top->member_count, ENTRY_METHOD, value, parser);

        return;
    }

    parser->member = NULL;
    add_note(parser, parser->top, keyword, value);
}

static void parse_line(Parser* parser, char* line) {
    size_t spaces = 0;

    while (line[spaces] == ' ') {
        spaces++;
    }

    if (line[spaces] == '\t') {
        mappings_problem(parser->mappings, parser->file, parser->line, "indent with 4 spaces, not tabs");

        return;
    }

    if (line[spaces] == '\0' || line[spaces] == '#') {
        return;
    }

    if (spaces % INDENT != 0) {
        mappings_problem(parser->mappings, parser->file, parser->line, "indent by 4 spaces a level (this line has %zu)", spaces);

        return;
    }

    char* keyword = line + spaces;
    char* value = keyword + strcspn(keyword, " ");

    if (*value != '\0') {
        *value++ = '\0';
    }

    while (*value == ' ') {
        value++;
    }

    size_t level = spaces / INDENT;

    if (level == 0) {
        parse_top(parser, keyword, value);
    } else if (level == 1 && parser->top != NULL) {
        parse_member(parser, keyword, value);
    } else if (level == 2 && parser->member != NULL) {
        add_note(parser, parser->member, keyword, value);
    } else if (parser->top != NULL) {
        mappings_problem(parser->mappings, parser->file, parser->line, "this line is indented deeper than what it could belong to");
    }
}

static void parse_file(Mappings* mappings, const wchar_t* path, const char* shown_path) {
    FILE* file = _wfopen(path, L"rb");

    if (file == NULL) {
        mappings_problem(mappings, shown_path, 0, "couldn't be read");

        return;
    }

    char** files = realloc(mappings->files, (size_t)(mappings->file_count + 1) * sizeof *files);

    if (files == NULL) {
        fclose(file);

        return;
    }

    mappings->files = files;
    mappings->files[mappings->file_count] = copy_text(shown_path);

    Parser parser = { .mappings = mappings, .file = mappings->files[mappings->file_count++] };
    char line[MAX_LINE];

    while (fgets(line, sizeof line, file) != NULL) {
        parser.line++;
        line[strcspn(line, "\r\n")] = '\0';

        size_t length = strlen(line);

        while (length > 0 && line[length - 1] == ' ') {
            line[--length] = '\0';
        }

        parse_line(&parser, line);
    }

    fclose(file);
}

static void parse_folder(Mappings* mappings, const wchar_t* folder, const wchar_t* relative) {
    wchar_t pattern[PATH_CAPACITY];
    WIN32_FIND_DATAW found;

    swprintf(pattern, PATH_CAPACITY, L"%ls%ls%ls\\*", folder, relative[0] != L'\0' ? L"\\" : L"", relative);

    HANDLE search = FindFirstFileW(pattern, &found);

    if (search == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        const wchar_t* name = found.cFileName;
        wchar_t inside[PATH_CAPACITY];

        if (name[0] == L'.') {
            continue;
        }

        swprintf(inside, PATH_CAPACITY, L"%ls%ls%ls", relative, relative[0] != L'\0' ? L"/" : L"", name);

        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            parse_folder(mappings, folder, inside);
            continue;
        }

        size_t length = wcslen(name);

        if (length > 8 && _wcsicmp(name + length - 8, L".mapping") == 0) {
            wchar_t path[PATH_CAPACITY];
            char shown[PATH_CAPACITY];

            swprintf(path, PATH_CAPACITY, L"%ls\\%ls", folder, inside);
            WideCharToMultiByte(CP_UTF8, 0, inside, -1, shown, sizeof shown, NULL, NULL);
            parse_file(mappings, path, shown);
        }
    } while (FindNextFileW(search, &found));

    FindClose(search);
}

static void free_entries(Entry* entries, int count) {
    for (int index = 0; index < count; index++) {
        Entry* entry = &entries[index];

        for (int note = 0; note < entry->note_count; note++) {
            free(entry->notes[note].keyword);
            free(entry->notes[note].value);
        }

        free(entry->notes);
        free(entry->name);
        free(entry->type);
        free_entries(entry->members, entry->member_count);
    }

    free(entries);
}


bool mappings_load(const wchar_t* folder, Mappings* mappings) {
    memset(mappings, 0, sizeof *mappings);

    DWORD attributes = GetFileAttributesW(folder);

    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        fwprintf(stderr, L"%ls isn't a folder\n", folder);

        return false;
    }

    parse_folder(mappings, folder, L"");

    if (mappings->file_count == 0) {
        fwprintf(stderr, L"%ls has no .mapping files\n", folder);

        return false;
    }

    return true;
}

void mappings_free(Mappings* mappings) {
    free_entries(mappings->entries, mappings->entry_count);

    for (int index = 0; index < mappings->file_count; index++) {
        free(mappings->files[index]);
    }

    free(mappings->files);
    memset(mappings, 0, sizeof *mappings);
}

void mappings_problem(Mappings* mappings, const char* file, int line, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);

    printf("%s:%d: ", file, line);
    vprintf(format, arguments);
    printf("\n");
    mappings->problem_count++;

    va_end(arguments);
}

const Note* mappings_note(const Entry* entry, const char* keyword) {
    for (int index = 0; index < entry->note_count; index++) {
        if (strcmp(entry->notes[index].keyword, keyword) == 0) {
            return &entry->notes[index];
        }
    }

    return NULL;
}
