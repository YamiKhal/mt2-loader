#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../../src/core/symbols.h"
#include "class_sizes.h"
#include "commands.h"
#include "enums.h"
#include "game_exe.h"
#include "game_image.h"
#include "mappings.h"
#include "places.h"
#include "reflection.h"
#include "rtti.h"
#include "source_files.h"
#include "vtables.h"

#define NAME_CAPACITY 1300

typedef struct Member {
    char* owner;
    char* name;
    uint32_t rva;
} Member;

typedef struct MemberList {
    Member* items;
    int count;
    int capacity;
} MemberList;

typedef struct Catalog {
    const GameImage* image;
    const Reflection* reflection;
    const GameEnums* enums;
    const Mappings* mappings;
    SourceFiles sources;
    MemberList members;
    char** classes;
    CodeSize* sizes;
    int class_count;
    wchar_t folder[MAX_PATH];
} Catalog;


static bool is_game_class(const char* name) {
    bool own = strncmp(name, "mmo", 3) == 0 || strncmp(name, "vs", 2) == 0 || strncmp(name, "core", 4) == 0;

    return own && strchr(name, '<') == NULL && strchr(name, ' ') == NULL && strchr(name, '(') == NULL;
}

// "mmoNPC::Generate(mmoCharacterTypeID const&, bool)" is mmoNPC's function Generate(...).
static void collect_member(size_t index, const char* readable, void* opaque) {
    MemberList* list = opaque;
    const char* open = strchr(readable, '(');
    const char* split = NULL;

    for (const char* at = readable; open != NULL && at < open; at++) {
        if (at[0] == ':' && at[1] == ':') {
            split = at;
        }
    }

    if (split == NULL || strstr(readable, "[clone") != NULL) {
        return;
    }

    char owner[NAME_CAPACITY];
    snprintf(owner, sizeof owner, "%.*s", (int)(split - readable), readable);

    if (!is_game_class(owner)) {
        return;
    }

    if (list->count == list->capacity) {
        list->capacity = list->capacity == 0 ? 65536 : list->capacity * 2;
        list->items = realloc(list->items, (size_t)list->capacity * sizeof *list->items);
    }

    Member* member = &list->items[list->count++];
    member->owner = _strdup(owner);
    member->name = _strdup(split + 2);
    member->rva = symbols_rva_of(index);
}

static int compare_members(const void* left, const void* right) {
    const Member* first = left;
    const Member* second = right;
    int by_owner = strcmp(first->owner, second->owner);

    return by_owner != 0 ? by_owner : strcmp(first->name, second->name);
}

static int compare_names(const void* left, const void* right) {
    return strcmp(*(char* const*)left, *(char* const*)right);
}

static void safe_file_name(const char* name, char* file, size_t capacity) {
    size_t used = 0;

    for (const char* character = name; *character != '\0' && used + 1 < capacity; character++) {
        file[used++] = strchr("<>:\"/\\|?* ", *character) != NULL ? '_' : *character;
    }

    file[used] = '\0';
}

static const Entry* mapping_of(const Catalog* reference, const char* name) {
    for (int index = 0; reference->mappings != NULL && index < reference->mappings->entry_count; index++) {
        const Entry* entry = &reference->mappings->entries[index];

        if (entry->kind == ENTRY_CLASS && strcmp(entry->name, name) == 0) {
            return entry;
        }
    }

    return NULL;
}

static bool has_page(const Catalog* reference, const char* name) {
    return bsearch(&name, reference->classes, (size_t)reference->class_count, sizeof *reference->classes, compare_names) != NULL;
}

static void link_to(const Catalog* reference, const char* name, FILE* out) {
    char file[NAME_CAPACITY];

    if (has_page(reference, name)) {
        safe_file_name(name, file, sizeof file);
        fprintf(out, "[%s](%s.md)", name, file);
    } else {
        fprintf(out, "%s", name);
    }
}

static const char* source_of(const Catalog* reference, const char* name);

// The second template argument of vsObject<Self, Base> or vsAbstractObject<Self, Base>: the class it's built on.
static bool engine_wrapped_base(const char* base, char* real, size_t capacity) {
    const char* comma = strchr(base, ',');
    const char* close = strrchr(base, '>');
    bool wraps = strncmp(base, "vsObject<", 9) == 0 || strncmp(base, "vsAbstractObject<", 17) == 0;

    if (!wraps || comma == NULL || close == NULL || close < comma) {
        return false;
    }

    const char* start = comma + 1;

    while (*start == ' ') {
        start++;
    }

    snprintf(real, capacity, "%.*s", (int)(close - start), start);

    return strchr(real, '<') == NULL;
}

static void write_heading(const Catalog* reference, const char* name, int index, FILE* out) {
    RttiBase bases[RTTI_MAX_BASES];
    int base_count = rtti_bases(reference->image, name, bases, RTTI_MAX_BASES);
    const Entry* mapping = mapping_of(reference, name);
    const Note* size_note = mapping != NULL ? mappings_note(mapping, "size") : NULL;

    fprintf(out, "# %s\n\n", name);

    for (int base = 0; base < base_count; base++) {
        char real[NAME_CAPACITY];

        fprintf(out, "%s", base == 0 ? "Built on " : ", ");

        // vsObject<mmoNPC, mmoCharacter> is the engine's way of saying "built on mmoCharacter".
        if (engine_wrapped_base(bases[base].name, real, sizeof real)) {
            link_to(reference, real, out);
            fprintf(out, " (through `%s`)", bases[base].name);
        } else {
            link_to(reference, bases[base].name, out);
        }
    }

    if (size_note != NULL) {
        fprintf(out, "%s0x%s bytes", base_count > 0 ? " · " : "", size_note->value + 2);
    } else if (reference->sizes[index].size > 0) {
        fprintf(out, "%s0x%x bytes", base_count > 0 ? " · " : "", reference->sizes[index].size);
    }

    const char* source = source_of(reference, name);

    if (source[0] != '\0') {
        fprintf(out, " · `%s`", source);
    }

    fprintf(out, "\n\n");

    for (int note = 0; mapping != NULL && note < mapping->note_count; note++) {
        if (strcmp(mapping->notes[note].keyword, "doc") == 0) {
            fprintf(out, "%s\n\n", mapping->notes[note].value);
        }
    }
}

static void write_enums(const Catalog* reference, const char* name, FILE* out) {
    size_t length = strlen(name);
    bool any = false;

    for (int index = 0; index < reference->enums->count; index++) {
        const GameEnum* game_enum = &reference->enums->items[index];

        if (strncmp(game_enum->name, name, length) != 0 || strncmp(game_enum->name + length, "::", 2) != 0) {
            continue;
        }

        fprintf(out, "%s- `%s`: ", any ? "" : "## Enums\n\nThe words are the ones data files and saves use.\n\n", game_enum->name + length + 2);

        for (int value = 0; value < game_enum->count; value++) {
            fprintf(out, "%s%s", value > 0 ? ", " : "", game_enum->values[value][0] != '\0' ? game_enum->values[value] : "?");
        }

        fprintf(out, "\n");
        any = true;
    }

    if (any) {
        fprintf(out, "\n");
    }
}

typedef struct FieldRow {
    uint32_t offset;
    const char* name;
    const char* type;
    const char* doc;
    bool mapped;
} FieldRow;

static int compare_rows(const void* left, const void* right) {
    uint32_t left_offset = ((const FieldRow*)left)->offset;
    uint32_t right_offset = ((const FieldRow*)right)->offset;

    return left_offset < right_offset ? -1 : left_offset > right_offset;
}

static const char* mapped_doc_at(const Entry* mapping, uint32_t offset) {
    for (int member = 0; mapping != NULL && member < mapping->member_count; member++) {
        const Entry* field = &mapping->members[member];
        const Note* doc = field->kind == ENTRY_FIELD && field->offset == offset ? mappings_note(field, "doc") : NULL;

        if (doc != NULL) {
            return doc->value;
        }
    }

    return NULL;
}

// The game's own fields (with a mapping's doc when it has one) and the mapped ones it doesn't name, by offset.
static void write_fields(const Catalog* reference, const char* name, FILE* out) {
    const Entry* mapping = mapping_of(reference, name);
    FieldRow* rows = malloc(1024 * sizeof *rows);
    int count = 0;

    for (int index = 0; rows != NULL && index < reference->reflection->count && count < 1024; index++) {
        const ReflectedField* field = &reference->reflection->fields[index];

        if (field->has_offset && strcmp(field->class_name, name) == 0) {
            rows[count++] = (FieldRow){ field->offset, field->name, field->type, mapped_doc_at(mapping, field->offset), false };
        }
    }

    int reflected = count;

    for (int member = 0; rows != NULL && mapping != NULL && member < mapping->member_count && count < 1024; member++) {
        const Entry* field = &mapping->members[member];
        bool named_by_game = false;

        for (int row = 0; row < reflected; row++) {
            named_by_game = named_by_game || rows[row].offset == field->offset;
        }

        if (field->kind == ENTRY_FIELD && !named_by_game) {
            const Note* doc = mappings_note(field, "doc");
            rows[count++] = (FieldRow){ field->offset, field->name, field->type, doc != NULL ? doc->value : NULL, true };
        }
    }

    if (count > 0) {
        qsort(rows, (size_t)count, sizeof *rows, compare_rows);
        fprintf(out, "## Fields\n\nThe game names these for its data files and saves (plugins reach them by name), except those "
            "marked mt2-mappings.\n\n| Offset | Name | Type | Notes |\n|---|---|---|---|\n");
    }

    for (int row = 0; row < count; row++) {
        fprintf(out, "| 0x%x | %s | `%s` | %s%s%s |\n", rows[row].offset, rows[row].name, rows[row].type, rows[row].mapped ? "mt2-mappings" : "",
            rows[row].mapped && rows[row].doc != NULL ? ": " : "", rows[row].doc != NULL ? rows[row].doc : "");
    }

    if (count > 0) {
        fprintf(out, "\n");
    }

    free(rows);
}
static void write_virtuals(const Catalog* reference, const char* name, FILE* out) {
    VtableEntry* entries = malloc(VTABLE_MAX_ENTRIES * sizeof *entries);
    uint32_t table = 0;
    int count = entries != NULL && place_class_symbol("_ZTV", name, &table) ? vtables_read(reference->image, table, entries, VTABLE_MAX_ENTRIES) : 0;
    char function[PLACE_CAPACITY];
    size_t length = strlen(name);
    bool any = false;

    for (int index = 0; index < count; index++) {
        if (entries[index].table != 0) {
            continue;
        }

        place_name(entries[index].function, function, sizeof function);

        if (!any) {
            fprintf(out, "## Virtual functions\n\nIts own ones; the others it has from the classes it's built on (mt2sdk vtable %s).\n\n"
                "| Slot | Function |\n|---|---|\n", name);
            any = true;
        }

        if (strncmp(function, name, length) == 0 && strncmp(function + length, "::", 2) == 0) {
            fprintf(out, "| %d | `%s` |\n", entries[index].slot, function + length + 2);
        }
    }

    if (any) {
        fprintf(out, "\n");
    }

    free(entries);
}

// Where a class's functions start in the list, which is sorted by class: -1 when it has none.
static int first_member_of(const Catalog* reference, const char* name) {
    int low = 0;
    int high = reference->members.count;

    while (low < high) {
        int middle = (low + high) / 2;

        if (strcmp(reference->members.items[middle].owner, name) < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    return low < reference->members.count && strcmp(reference->members.items[low].owner, name) == 0 ? low : -1;
}

static void write_method_notes(const Entry* mapping, const char* method_name, FILE* out) {
    for (int index = 0; mapping != NULL && index < mapping->member_count; index++) {
        const Entry* method = &mapping->members[index];

        for (int note = 0; method->kind == ENTRY_METHOD && strcmp(method->name, method_name) == 0 && note < method->note_count; note++) {
            const char* keyword = method->notes[note].keyword;

            if (strcmp(keyword, "doc") == 0 || strcmp(keyword, "returns") == 0) {
                fprintf(out, "  %s%s\n", strcmp(keyword, "returns") == 0 ? "Returns " : "", method->notes[note].value);
            }
        }
    }
}

static void write_functions(const Catalog* reference, const char* name, FILE* out) {
    const Entry* mapping = mapping_of(reference, name);
    int first = first_member_of(reference, name);

    if (first < 0) {
        return;
    }

    fprintf(out, "## Functions\n\n");

    for (int index = first; index < reference->members.count && strcmp(reference->members.items[index].owner, name) == 0; index++) {
        const Member* member = &reference->members.items[index];

        if (index > first && strcmp(member[-1].name, member->name) == 0) {
            continue;
        }

        fprintf(out, "- `%s`\n", member->name);
        write_method_notes(mapping, member->name, out);
    }

    fprintf(out, "\n");
}
static void write_class_page(const Catalog* reference, int index) {
    const char* name = reference->classes[index];
    char file[NAME_CAPACITY];
    wchar_t path[MAX_PATH * 2];

    safe_file_name(name, file, sizeof file);
    swprintf(path, MAX_PATH * 2, L"%ls\\classes\\%hs.md", reference->folder, file);

    FILE* out = _wfopen(path, L"w");

    if (out == NULL) {
        return;
    }

    write_heading(reference, name, index, out);
    write_enums(reference, name, out);
    write_fields(reference, name, out);
    write_virtuals(reference, name, out);
    write_functions(reference, name, out);
    fclose(out);
}

// The source file most of a class's functions come from.
static const char* source_of(const Catalog* reference, const char* name) {
    int first = first_member_of(reference, name);
    int best = -1;
    int best_votes = 0;

    for (int index = first; first >= 0 && index < reference->members.count && strcmp(reference->members.items[index].owner, name) == 0; index++) {
        int file = source_file_of(&reference->sources, reference->members.items[index].rva);
        int votes = 0;

        if (file < 0 || file == best) {
            continue;
        }

        for (int other = first; other < reference->members.count && strcmp(reference->members.items[other].owner, name) == 0; other++) {
            votes += source_file_of(&reference->sources, reference->members.items[other].rva) == file ? 1 : 0;
        }

        if (votes > best_votes) {
            best = file;
            best_votes = votes;
        }
    }

    if (best < 0) {
        return "";
    }

    const SourceFile* source = &reference->sources.files[best];

    return source->path[0] != '\0' ? source->path : source->name;
}
static void write_index(const Catalog* reference, const char* build) {
    wchar_t path[MAX_PATH * 2];
    swprintf(path, MAX_PATH * 2, L"%ls\\README.md", reference->folder);

    FILE* out = _wfopen(path, L"w");

    if (out == NULL) {
        return;
    }

    fprintf(out, "# MMORPG Tycoon 2 %s: classes\n\n", build);
    fprintf(out, "Made by `mt2sdk reference` from MT2.exe%s: names, fields, enums and functions, no game code. %d classes.\n\n",
        reference->mappings != NULL ? " and mt2-mappings" : "", reference->class_count);
    fprintf(out, "| Class | Source file |\n|---|---|\n");

    for (int index = 0; index < reference->class_count; index++) {
        char file[NAME_CAPACITY];

        safe_file_name(reference->classes[index], file, sizeof file);
        fprintf(out, "| [%s](classes/%s.md) | `%s` |\n", reference->classes[index], file, source_of(reference, reference->classes[index]));
    }

    fclose(out);
}

static void add_class(Catalog* reference, const char* name, int* capacity) {
    if (!is_game_class(name)) {
        return;
    }

    if (reference->class_count == *capacity) {
        *capacity = *capacity == 0 ? 4096 : *capacity * 2;
        reference->classes = realloc(reference->classes, (size_t)*capacity * sizeof *reference->classes);
    }

    reference->classes[reference->class_count++] = _strdup(name);
}

static void choose_classes(Catalog* reference) {
    int capacity = 0;

    for (int index = 0; index < reference->members.count; index++) {
        if (index == 0 || strcmp(reference->members.items[index - 1].owner, reference->members.items[index].owner) != 0) {
            add_class(reference, reference->members.items[index].owner, &capacity);
        }
    }

    for (int index = 0; index < reference->reflection->count; index++) {
        add_class(reference, reference->reflection->fields[index].class_name, &capacity);
    }

    qsort(reference->classes, (size_t)reference->class_count, sizeof *reference->classes, compare_names);

    int kept = 0;

    for (int index = 0; index < reference->class_count; index++) {
        if (kept > 0 && strcmp(reference->classes[kept - 1], reference->classes[index]) == 0) {
            free(reference->classes[index]);
        } else {
            reference->classes[kept++] = reference->classes[index];
        }
    }

    reference->class_count = kept;
}


int command_reference(const wchar_t* exe, const wchar_t* folder, const wchar_t* mappings_folder) {
    wchar_t path[GAME_PATH_CAPACITY];
    wchar_t classes_folder[MAX_PATH * 2];
    char build[64];
    GameImage image;
    Reflection reflection;
    GameEnums enums;
    Mappings mappings;
    Catalog* reference = calloc(1, sizeof *reference);

    if (reference == NULL || !game_exe_load_symbols(exe, path) || !game_image_load(path, &image)) {
        free(reference);

        return 1;
    }

    bool have_mappings = mappings_folder != NULL && mappings_load(mappings_folder, &mappings);

    if (!reflection_read(&image, &reflection) || !enums_read(&image, &enums) || !source_files_read(&image, &reference->sources)) {
        fprintf(stderr, "The game's structure couldn't be read from its exe\n");

        return 1;
    }

    game_exe_build_name(path, build, sizeof build);
    GetFullPathNameW(folder, MAX_PATH, reference->folder, NULL);
    swprintf(classes_folder, MAX_PATH * 2, L"%ls\\classes", reference->folder);
    CreateDirectoryW(reference->folder, NULL);
    CreateDirectoryW(classes_folder, NULL);

    reference->image = &image;
    reference->reflection = &reflection;
    reference->enums = &enums;
    reference->mappings = have_mappings ? &mappings : NULL;

    symbols_each_containing("::", collect_member, &reference->members);
    qsort(reference->members.items, (size_t)reference->members.count, sizeof(Member), compare_members);
    choose_classes(reference);

    reference->sizes = calloc((size_t)reference->class_count + 1, sizeof *reference->sizes);
    class_sizes_from_code(&image, (const char* const*)reference->classes, reference->class_count, reference->sizes);

    for (int index = 0; index < reference->class_count; index++) {
        write_class_page(reference, index);
    }

    write_index(reference, build);
    wprintf(L"%d class pages written to %ls (start with README.md)\n", reference->class_count, reference->folder);

    for (int index = 0; index < reference->members.count; index++) {
        free(reference->members.items[index].owner);
        free(reference->members.items[index].name);
    }

    for (int index = 0; index < reference->class_count; index++) {
        free(reference->classes[index]);
    }

    free(reference->members.items);
    free(reference->classes);
    free(reference->sizes);
    source_files_free(&reference->sources);
    enums_free(&enums);
    reflection_free(&reflection);

    if (have_mappings) {
        mappings_free(&mappings);
    }

    game_image_free(&image);
    free(reference);

    return 0;
}
