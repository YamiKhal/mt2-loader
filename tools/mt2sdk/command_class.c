#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../../src/core/symbols.h"
#include "class_sizes.h"
#include "commands.h"
#include "game_exe.h"
#include "game_image.h"
#include "mappings.h"
#include "places.h"
#include "reflection.h"
#include "rtti.h"
#include "source_files.h"

#define MAX_FIELDS 1024

typedef struct ShownField {
    uint32_t offset;
    char type[REFLECTION_TYPE_CAPACITY];
    char name[REFLECTION_NAME_CAPACITY];
    const char* note;
    bool reflected;
} ShownField;

typedef struct FileVote {
    const char* class_prefix;
    const SourceFiles* sources;
    int* votes;
    int method_count;
} FileVote;


static void vote_for_file(size_t index, const char* readable, void* opaque) {
    FileVote* vote = opaque;

    if (strncmp(readable, vote->class_prefix, strlen(vote->class_prefix)) != 0) {
        return;
    }

    int file = source_file_of(vote->sources, symbols_rva_of(index));

    if (file >= 0) {
        vote->votes[file]++;
        vote->method_count++;
    }
}

// The file most of the class's own functions come from: its .cpp.
static int class_source_file(const SourceFiles* sources, const char* class_name, int* method_count) {
    char prefix[PLACE_CAPACITY + 8];
    int* votes = calloc((size_t)sources->file_count, sizeof *votes);
    int best = -1;

    snprintf(prefix, sizeof prefix, "%s::", class_name);

    FileVote vote = { prefix, sources, votes, 0 };

    if (votes != NULL) {
        symbols_each_containing(prefix, vote_for_file, &vote);
    }

    for (int index = 0; votes != NULL && index < sources->file_count; index++) {
        if (votes[index] > 0 && (best < 0 || votes[index] > votes[best])) {
            best = index;
        }
    }

    *method_count = vote.method_count;
    free(votes);

    return best;
}

static int compare_fields(const void* left, const void* right) {
    uint32_t left_offset = ((const ShownField*)left)->offset;
    uint32_t right_offset = ((const ShownField*)right)->offset;

    return left_offset < right_offset ? -1 : left_offset > right_offset;
}

static int add_reflected(const Reflection* reflection, const char* class_name, ShownField* fields, int count) {
    for (int index = 0; index < reflection->count && count < MAX_FIELDS; index++) {
        const ReflectedField* field = &reflection->fields[index];

        if (strcmp(field->class_name, class_name) != 0 || !field->has_offset) {
            continue;
        }

        ShownField* shown = &fields[count++];
        memset(shown, 0, sizeof *shown);
        shown->offset = field->offset;
        shown->reflected = true;
        snprintf(shown->type, sizeof shown->type, "%s", field->type[0] != '\0' ? field->type : "?");
        snprintf(shown->name, sizeof shown->name, "%s", field->name);
    }

    return count;
}

// A mapped field the game also reflects adds its doc to that line; others get lines of their own.
static int add_mapped(const Entry* mapping, ShownField* fields, int count) {
    for (int index = 0; mapping != NULL && index < mapping->member_count; index++) {
        const Entry* member = &mapping->members[index];
        const Note* doc = mappings_note(member, "doc");
        bool merged = false;

        if (member->kind != ENTRY_FIELD) {
            continue;
        }

        for (int existing = 0; existing < count; existing++) {
            if (fields[existing].offset == member->offset && strcmp(fields[existing].name, member->name) == 0) {
                fields[existing].note = doc != NULL ? doc->value : NULL;
                merged = true;
            }
        }

        if (!merged && count < MAX_FIELDS) {
            ShownField* shown = &fields[count++];
            memset(shown, 0, sizeof *shown);
            shown->offset = member->offset;
            shown->note = doc != NULL ? doc->value : "";
            snprintf(shown->type, sizeof shown->type, "%s", member->type);
            snprintf(shown->name, sizeof shown->name, "%s", member->name);
        }
    }

    return count;
}

static const Entry* find_mapping(const Mappings* mappings, const char* class_name) {
    for (int index = 0; mappings != NULL && index < mappings->entry_count; index++) {
        if (mappings->entries[index].kind == ENTRY_CLASS && strcmp(mappings->entries[index].name, class_name) == 0) {
            return &mappings->entries[index];
        }
    }

    return NULL;
}

static void print_heading(const GameImage* image, const char* class_name, const Entry* mapping) {
    RttiBase bases[RTTI_MAX_BASES];
    int base_count = rtti_bases(image, class_name, bases, RTTI_MAX_BASES);
    const Note* size_note = mapping != NULL ? mappings_note(mapping, "size") : NULL;
    CodeSize size = { 0, 0 };

    printf("class %s", class_name);

    for (int index = 0; index < base_count; index++) {
        printf("%s%s%s", index == 0 ? " : " : ", ", bases[index].is_virtual ? "virtual " : "", bases[index].name);
    }

    if (size_note != NULL) {
        printf("    0x%s bytes (mt2-mappings)\n", size_note->value + 2);
    } else {
        class_sizes_from_code(image, &class_name, 1, &size);
        printf(size.size > 0 ? "    0x%x bytes\n" : "\n", size.size);
    }

    const Note* doc = mapping != NULL ? mappings_note(mapping, "doc") : NULL;

    if (doc != NULL) {
        printf("  %s\n", doc->value);
    }
}


int command_class(const wchar_t* exe, const wchar_t* written, const wchar_t* mappings_folder) {
    wchar_t path[GAME_PATH_CAPACITY];
    char class_name[PLACE_CAPACITY];
    GameImage image;
    Mappings mappings;
    bool have_mappings = false;

    WideCharToMultiByte(CP_UTF8, 0, written, -1, class_name, sizeof class_name, NULL, NULL);

    if (!game_exe_load_symbols(exe, path) || !game_image_load(path, &image)) {
        return 1;
    }

    if (mappings_folder != NULL) {
        have_mappings = mappings_load(mappings_folder, &mappings);
    }

    const Entry* mapping = have_mappings ? find_mapping(&mappings, class_name) : NULL;
    print_heading(&image, class_name, mapping);

    SourceFiles sources;
    int method_count = 0;

    if (source_files_read(&image, &sources)) {
        int file = class_source_file(&sources, class_name, &method_count);

        if (file >= 0) {
            const SourceFile* source = &sources.files[file];
            printf("  source: %s\n", source->path[0] != '\0' ? source->path : source->name);
        }

        source_files_free(&sources);
    }

    Reflection reflection;
    ShownField* fields = calloc(MAX_FIELDS, sizeof *fields);
    int count = 0;

    if (fields != NULL && reflection_read(&image, &reflection)) {
        count = add_reflected(&reflection, class_name, fields, 0);
        reflection_free(&reflection);
    }

    count = fields != NULL ? add_mapped(mapping, fields, count) : 0;
    qsort(fields, (size_t)count, sizeof *fields, compare_fields);

    printf("  %d functions, %d fields%s\n", method_count, count, count > 0 ? ":" : "");

    for (int index = 0; index < count; index++) {
        const ShownField* field = &fields[index];
        printf("    +0x%-5x %-34s %-26s%s\n", field->offset, field->type, field->name, field->reflected ? "" : "  (mt2-mappings)");

        if (field->note != NULL && field->note[0] != '\0') {
            printf("             %s\n", field->note);
        }
    }

    free(fields);

    if (have_mappings) {
        mappings_free(&mappings);
    }

    game_image_free(&image);

    return 0;
}
