#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "commands.h"
#include "mappings.h"

#define SEEN_OFFSET_MARK "+0x"


static void write_text(FILE* file, const char* text) {
    fputc('"', file);

    for (const char* character = text; *character != '\0'; character++) {
        if (*character == '"' || *character == '\\') {
            fputc('\\', file);
        }

        fputc(*character, file);
    }

    fputc('"', file);
}

// A seen place, "mmoGizmoDefinition::GetVariant(std::string const&) const+0x1a", as the function and the offset.
static void write_place(FILE* file, const char* place) {
    const char* mark = NULL;

    for (const char* found = strstr(place, SEEN_OFFSET_MARK); found != NULL; found = strstr(found + 1, SEEN_OFFSET_MARK)) {
        mark = found;
    }

    size_t name_length = mark != NULL ? (size_t)(mark - place) : strlen(place);
    char name[1024];
    snprintf(name, sizeof name, "%.*s", (int)name_length, place);

    write_text(file, name);
    fprintf(file, ", %s", mark != NULL ? mark + 1 : "0x0");
}

static int write_field(FILE* file, const Entry* owner, const Entry* field) {
    int written = 0;

    for (int index = 0; index < field->note_count; index++) {
        const Note* note = &field->notes[index];

        if (strcmp(note->keyword, "seen") != 0) {
            continue;
        }

        char name[1024];
        snprintf(name, sizeof name, "%s::%s", owner->name, field->name);

        fputs("    { ", file);
        write_text(file, name);
        fprintf(file, ", 0x%x, ", field->offset);
        write_text(file, field->type);
        fputs(", ", file);
        write_place(file, note->value);
        fputs(" },\n", file);
        written++;
    }

    return written;
}


int command_mappings_fields(const wchar_t* folder, const wchar_t* output) {
    Mappings mappings;

    if (!mappings_load(folder, &mappings)) {
        return 1;
    }

    if (mappings.problem_count > 0) {
        printf("Fix these first: mt2sdk mappings check\n");
        mappings_free(&mappings);

        return 1;
    }

    FILE* file = _wfopen(output, L"w");

    if (file == NULL) {
        wprintf(L"Couldn't write %ls\n", output);
        mappings_free(&mappings);

        return 1;
    }

    fputs("// Made by mt2sdk mappings fields from mt2-mappings: each field the game doesn't name, once per place its code\n"
          "// uses it.\n"
          "#include \"mapped_fields.h\"\n"
          "\n"
          "const MappedField MAPPED_FIELDS[] = {\n", file);

    int written = 0;

    for (int index = 0; index < mappings.entry_count; index++) {
        const Entry* entry = &mappings.entries[index];

        for (int member = 0; entry->kind == ENTRY_CLASS && member < entry->member_count; member++) {
            if (entry->members[member].kind == ENTRY_FIELD) {
                written += write_field(file, entry, &entry->members[member]);
            }
        }
    }

    fputs("    { 0 },\n};\n", file);
    fclose(file);
    mappings_free(&mappings);
    wprintf(L"%d places of mapped fields written to %ls\n", written, output);

    return 0;
}
