#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../../src/core/symbols.h"
#include "class_sizes.h"
#include "commands.h"
#include "game_fields.h"
#include "reflection.h"
#include "disassembly.h"
#include "game_exe.h"
#include "game_image.h"
#include "mappings.h"
#include "mappings_json.h"
#include "places.h"

typedef struct CheckCounts {
    int classes;
    int fields;
    int reflected_fields;
    int methods;
    int functions;
    int places;
    int sizes_from_code;
} CheckCounts;

typedef struct Checker {
    Mappings* mappings;
    const GameImage* image;
    // For each entry, the size the game gives operator new before calling the class's constructor, or 0.
    const CodeSize* code_sizes;
    // The fields the game names, to compare mapped ones with (NULL when they couldn't be read).
    const Reflection* reflection;
    CheckCounts counts;
} Checker;

typedef struct NameSearch {
    const char* text;
    bool found;
} NameSearch;


static bool is_template_pattern(const char* class_name) {
    return strstr(class_name, "<T>") != NULL || strstr(class_name, "<T,") != NULL;
}

static void note_found(size_t index, const char* readable, void* opaque) {
    (void)index;
    (void)readable;
    ((NameSearch*)opaque)->found = true;
}

static bool any_name_contains(const char* text) {
    NameSearch search = { text, false };
    symbols_each_containing(text, note_found, &search);

    return search.found;
}

// A class is in the game when one of its members, its vtable or its type info is: "mmoCharacter::", or
// "vsArray<" for any vsArray.
static bool class_exists(const char* class_name) {
    char text[PLACE_CAPACITY];

    if (is_template_pattern(class_name)) {
        snprintf(text, sizeof text, "%.*s<", (int)strcspn(class_name, "<"), class_name);

        return any_name_contains(text);
    }

    snprintf(text, sizeof text, "%s::", class_name);

    if (any_name_contains(text)) {
        return true;
    }

    snprintf(text, sizeof text, "typeinfo for %s", class_name);

    return any_name_contains(text);
}

static bool resolves(Checker* checker, const Entry* entry, const char* name) {
    SymbolMatch match = symbols_find(name);

    if (match.result == SYMBOL_FOUND) {
        return true;
    }

    if (match.result == SYMBOL_AMBIGUOUS) {
        mappings_problem(checker->mappings, entry->file, entry->line, "%s could be any of: %s", name, match.candidates);
    } else {
        mappings_problem(checker->mappings, entry->file, entry->line, "%s isn't a name in the game", name);
    }

    return false;
}

static bool is_instruction_start(const GameImage* image, uint32_t function, uint32_t end, uint32_t target) {
    Instruction instruction;
    uint32_t rva = function;

    while (rva < target && rva < end && disassembly_decode(image, rva, &instruction)) {
        rva += instruction.length;
    }

    return rva == target;
}

// seen and inlined places: the function must exist and the offset must start an instruction inside it.
static void check_place(Checker* checker, const Entry* entry, const Note* note) {
    char name[PLACE_CAPACITY];
    const char* plus = strstr(note->value, "+0x");

    while (strstr(plus + 1, "+0x") != NULL) {
        plus = strstr(plus + 1, "+0x");
    }

    snprintf(name, sizeof name, "%.*s", (int)(plus - note->value), note->value);

    uint32_t offset = (uint32_t)strtoul(plus + 3, NULL, 16);
    SymbolMatch match = symbols_find(name);
    Function function;

    checker->counts.places++;

    if (match.result != SYMBOL_FOUND || !place_function_at(match.rva, &function)) {
        mappings_problem(checker->mappings, entry->file, note->line, "%s isn't a function in the game", name);

        return;
    }

    if (function.start + offset >= function.end) {
        mappings_problem(checker->mappings, entry->file, note->line, "%s is only 0x%x bytes long", name, function.end - function.start);

        return;
    }

    if (!is_instruction_start(checker->image, function.start, function.end, function.start + offset)) {
        mappings_problem(checker->mappings, entry->file, note->line, "+0x%x isn't the start of an instruction in %s: see mt2sdk dis", offset, name);
    }
}

static void check_notes(Checker* checker, const Entry* entry) {
    for (int index = 0; index < entry->note_count; index++) {
        const Note* note = &entry->notes[index];

        if (strcmp(note->keyword, "seen") == 0 || strcmp(note->keyword, "inlined") == 0) {
            check_place(checker, entry, note);
        }
    }
}

// A mapped field can't sit where the game already names a field (its own or a base class's): same offset with another
// name, or inside a field whose size is known.
static void check_against_game(Checker* checker, const Entry* owner, const Entry* field) {
    if (checker->reflection == NULL || is_template_pattern(owner->name)) {
        return;
    }

    GameField* fields = malloc(GAME_FIELDS_MAX * sizeof *fields);
    int count = fields != NULL ? game_fields_of(checker->image, checker->reflection, checker->mappings, owner->name, fields, GAME_FIELDS_MAX) : 0;

    for (int index = 0; index < count; index++) {
        const GameField* game = &fields[index];
        bool same_place = game->offset == field->offset && strcmp(game->field->name, field->name) != 0;
        bool inside = game->size > 0 && field->offset > game->offset && field->offset < game->offset + game->size;

        if (same_place || inside) {
            mappings_problem(checker->mappings, field->file, field->line, "0x%x is %s the game's field %s::%s (%s at 0x%x)", field->offset,
                same_place ? "already" : "inside", game->field->class_name, game->field->name, game->field->type, game->offset);
            break;
        }
    }

    free(fields);
}

static void check_field(Checker* checker, const Entry* owner, const Entry* field, uint32_t size) {
    char property[PLACE_CAPACITY];

    checker->counts.fields++;

    if (size > 0 && field->offset >= size) {
        mappings_problem(checker->mappings, field->file, field->line, "0x%x is past the end of %s (0x%x bytes)", field->offset, owner->name, size);
    }

    // A field the game reflects keeps its name: its property is Class::s_<name>Property.
    snprintf(property, sizeof property, "%s::s_%sProperty", owner->name, field->name);

    if (!is_template_pattern(owner->name) && symbols_find(property).result == SYMBOL_FOUND) {
        checker->counts.reflected_fields++;
    }

    check_against_game(checker, owner, field);

    for (int index = 0; index < owner->member_count; index++) {
        const Entry* other = &owner->members[index];

        if (other == field) {
            break;
        }

        if (other->kind == ENTRY_FIELD && (other->offset == field->offset || strcmp(other->name, field->name) == 0)) {
            mappings_problem(checker->mappings, field->file, field->line, "%s already has a field %s at 0x%x (line %d)", owner->name,
                other->name, other->offset, other->line);
        }
    }

    check_notes(checker, field);
}

static void check_method(Checker* checker, const Entry* owner, const Entry* method) {
    char name[PLACE_CAPACITY];

    checker->counts.methods++;

    for (int index = 0; index < owner->member_count && &owner->members[index] != method; index++) {
        if (owner->members[index].kind == ENTRY_METHOD && strcmp(owner->members[index].name, method->name) == 0) {
            mappings_problem(checker->mappings, method->file, method->line, "%s is already described (line %d)", method->name, owner->members[index].line);
        }
    }

    if (!is_template_pattern(owner->name)) {
        snprintf(name, sizeof name, "%s::%s", owner->name, method->name);
        resolves(checker, method, name);
    }

    check_notes(checker, method);
}

static void check_class(Checker* checker, const Entry* entry) {
    const Note* size_note = mappings_note(entry, "size");
    uint32_t size = size_note != NULL ? (uint32_t)strtoul(size_note->value + 2, NULL, 16) : 0;
    CodeSize code = checker->code_sizes != NULL ? checker->code_sizes[entry - checker->mappings->entries] : (CodeSize){ 0, 0 };
    uint32_t code_size = code.size;
    // A smaller object than declared is certain; a bigger one only when several places agree (see CodeSize), and not
    // when it's room for a few of them (a std::vector growing to two vsLocArg asks for 2 * 0x68).
    bool is_array = size > 0 && code_size > size && code_size % size == 0;
    bool conflicts = code_size > 0 && code_size != size && (code_size < size || (code.places >= 2 && !is_array));

    checker->counts.classes++;

    if (size_note != NULL && conflicts) {
        mappings_problem(checker->mappings, entry->file, size_note->line,
            "the game makes each %s with 0x%x bytes (operator new before its constructor), not 0x%x", entry->name, code_size, size);
    }

    if (size_note == NULL && code_size > 0) {
        checker->counts.sizes_from_code++;
        size = code_size;
    }

    if (!class_exists(entry->name)) {
        mappings_problem(checker->mappings, entry->file, entry->line, "no function, global or type info in the game belongs to %s", entry->name);
    }

    check_notes(checker, entry);

    for (int index = 0; index < entry->member_count; index++) {
        const Entry* member = &entry->members[index];

        if (member->kind == ENTRY_FIELD) {
            check_field(checker, entry, member, size);
        } else {
            check_method(checker, entry, member);
        }
    }
}

static void check_duplicates(Checker* checker) {
    Mappings* mappings = checker->mappings;

    for (int index = 0; index < mappings->entry_count; index++) {
        for (int earlier = 0; earlier < index; earlier++) {
            const Entry* entry = &mappings->entries[index];
            const Entry* other = &mappings->entries[earlier];

            if (entry->kind == other->kind && strcmp(entry->name, other->name) == 0) {
                mappings_problem(mappings, entry->file, entry->line, "%s is already described in %s:%d", entry->name, other->file, other->line);
            }
        }
    }
}


int command_mappings_check(const wchar_t* exe, const wchar_t* folder) {
    wchar_t path[GAME_PATH_CAPACITY];
    Mappings mappings;
    GameImage image;

    if (!game_exe_load_symbols(exe, path) || !mappings_load(folder, &mappings)) {
        return 1;
    }

    if (!game_image_load(path, &image)) {
        mappings_free(&mappings);

        return 1;
    }

    CodeSize* code_sizes = class_sizes_for_mappings(&image, &mappings);
    Reflection reflection;
    bool have_reflection = reflection_read(&image, &reflection);
    Checker checker = { .mappings = &mappings, .image = &image, .code_sizes = code_sizes, .reflection = have_reflection ? &reflection : NULL };

    check_duplicates(&checker);

    for (int index = 0; index < mappings.entry_count; index++) {
        const Entry* entry = &mappings.entries[index];

        if (entry->kind == ENTRY_CLASS) {
            check_class(&checker, entry);
        } else {
            checker.counts.functions++;
            resolves(&checker, entry, entry->name);
            check_notes(&checker, entry);
        }
    }

    CheckCounts counts = checker.counts;
    printf("%d files: %d classes (%d sized from the code), %d fields (%d reflected), %d methods, %d functions and globals, "
        "%d places in the code. %d problem%s\n", mappings.file_count, counts.classes, counts.sizes_from_code, counts.fields,
        counts.reflected_fields, counts.methods, counts.functions, counts.places, mappings.problem_count,
        mappings.problem_count == 1 ? "" : "s");

    int problems = mappings.problem_count;
    free(code_sizes);

    if (have_reflection) {
        reflection_free(&reflection);
    }
    game_image_free(&image);
    mappings_free(&mappings);

    return problems == 0 ? 0 : 1;
}

int command_mappings_json(const wchar_t* exe, const wchar_t* folder, const wchar_t* output) {
    wchar_t path[GAME_PATH_CAPACITY];
    Mappings mappings;

    if (!game_exe_load_symbols(exe, path) || !mappings_load(folder, &mappings)) {
        return 1;
    }

    if (mappings.problem_count > 0) {
        printf("Fix these first: mt2sdk mappings check\n");
        mappings_free(&mappings);

        return 1;
    }

    GameImage image;
    bool written = game_image_load(path, &image) && mappings_json_write(&mappings, &image, folder, output);

    if (written) {
        wprintf(L"%d entries written to %ls\n", mappings.entry_count, output);
    }

    game_image_free(&image);
    mappings_free(&mappings);

    return written ? 0 : 1;
}
