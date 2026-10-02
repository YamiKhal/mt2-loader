#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "commands.h"
#include "enums.h"
#include "game_exe.h"
#include "game_image.h"
#include "mappings.h"
#include "places.h"
#include "reflection.h"
#include "rtti.h"

#define MAX_CLASSES 4096
#define NAME_CAPACITY 1300
#define TYPE_CAPACITY (NAME_CAPACITY * 3)

typedef enum Access {
    ACCESS_NONE,
    ACCESS_VALUE,
    ACCESS_TEXT,
    ACCESS_ENUM,
    ACCESS_OBJECTS,
    ACCESS_LINK,
    ACCESS_POINTER,
    ACCESS_INSIDE,
} Access;

// One field as the header gives it: how it's reached (by name, or by offset for a mapped one the loader can't check)
// and as what.
typedef struct Accessor {
    char name[NAME_CAPACITY];
    char lookup[NAME_CAPACITY];
    uint32_t offset;
    bool by_offset;
    Access access;
    char cpp_type[NAME_CAPACITY];
    char target[NAME_CAPACITY];
    char game_type[NAME_CAPACITY];
    const char* doc;
} Accessor;

typedef struct ClassSet {
    char* names[MAX_CLASSES];
    int count;
} ClassSet;

typedef struct Generator {
    const GameImage* image;
    const Reflection* reflection;
    const GameEnums* enums;
    const Mappings* mappings;
    ClassSet classes;
    FILE* out;
} Generator;

typedef struct KnownValue {
    const char* game;
    const char* cpp;
} KnownValue;

static const char* const RESERVED[] = {
    "get", "self", "game_class", "new", "delete", "class", "default", "switch", "case", "int", "float", "bool", "char",
    "double", "long", "short", "signed", "unsigned", "void", "const", "static", "return", "if", "else", "for", "while", "do",
    "break", "continue", "goto", "struct", "union", "enum", "template", "typename", "operator", "private", "public",
    "protected", "virtual", "friend", "namespace", "using", "true", "false", "sizeof", "auto", "register", "this", "export",
};

static const KnownValue VALUE_TYPES[] = {
    { "vsLocString", "game::LocalizedText" },
    { "bool", "bool" }, { "char", "char" }, { "unsigned char", "unsigned char" }, { "short", "short" },
    { "unsigned short", "unsigned short" }, { "int", "int" }, { "unsigned int", "unsigned int" }, { "long long", "long long" },
    { "unsigned long long", "unsigned long long" }, { "float", "float" }, { "double", "double" },
    { "vsColor", "game::Color" }, { "vsVector2D", "vsVector2D" }, { "vsVector3D", "vsVector3D" }, { "vsVector4D", "vsVector4D" },
    { "vsQuaternion", "vsQuaternion" },
};

static const char* const PREAMBLE =
    "// A C++ class for each of the game's classes, with a function for each of its fields: character.level(),\n"
    "// npc.state(), quest.questGiver(). Fields the game names for its saves are reached by those names, so they keep\n"
    "// working after game updates. Fields from mt2-mappings are reached by their mapped names, which the loader checks\n"
    "// against the game's code; the few it can't check (marked) are reached by offset and can move with an update.\n"
    "// Each class wraps a pointer to the game's object: mt2::mmoNPC npc(pointer), and npc.get() gives it back.\n"
    "\n"
    "#pragma once\n"
    "\n"
    "#include <mt2loader.hpp>\n"
    "\n"
    "namespace mt2 {\n"
    "\n"
    "// The engine's small value types, as they are in its public source.\n"
    "struct vsVector2D { float x = 0, y = 0; };\n"
    "struct vsVector3D { float x = 0, y = 0, z = 0; };\n"
    "struct vsVector4D { float x = 0, y = 0, z = 0, w = 0; };\n"
    "struct vsQuaternion { float x = 0, y = 0, z = 0, w = 1; };\n"
    "\n"
    "// What every class here is: a pointer to one of the game's objects, or none.\n"
    "class Object {\n"
    "public:\n"
    "    explicit Object(void* object = nullptr) : self(object) {}\n"
    "\n"
    "    void* get() const { return self; }\n"
    "    explicit operator bool() const { return self != nullptr; }\n"
    "\n"
    "protected:\n"
    "    void* self;\n"
    "};\n";


static bool has_class(const ClassSet* set, const char* name) {
    for (int index = 0; index < set->count; index++) {
        if (strcmp(set->names[index], name) == 0) {
            return true;
        }
    }

    return false;
}

static void add_class(ClassSet* set, const char* name) {
    if (set->count < MAX_CLASSES && name[0] != '\0' && strchr(name, '<') == NULL && !has_class(set, name)) {
        set->names[set->count] = malloc(strlen(name) + 1);
        strcpy(set->names[set->count++], name);
    }
}

// mmoNewGameWindow::LayoutTask can't be a C++ name at the top level: mmoNewGameWindow_LayoutTask.
static void identifier(const char* name, char* out, size_t capacity) {
    size_t used = 0;

    for (const char* character = name; *character != '\0' && used + 1 < capacity; character++) {
        if (character[0] == ':' && character[1] == ':') {
            out[used++] = '_';
            character++;
        } else {
            out[used++] = *character;
        }
    }

    out[used] = '\0';
}

// The class a game class is really built on: vsObject<mmoNPC, mmoCharacter> and vsAbstractObject<...> are the engine's
// way of writing "built on mmoCharacter".
static bool real_base(const GameImage* image, const char* name, char* base, size_t capacity) {
    RttiBase bases[RTTI_MAX_BASES];

    for (int depth = 0; depth < 8; depth++) {
        int count = rtti_bases(image, depth == 0 ? name : base, bases, RTTI_MAX_BASES);

        if (count <= 0 || bases[0].offset != 0) {
            return false;
        }

        snprintf(base, capacity, "%s", bases[0].name);

        if (strncmp(base, "vsObject<", 9) != 0 && strncmp(base, "vsAbstractObject<", 17) != 0) {
            return true;
        }
    }

    return false;
}

static const GameEnum* find_enum(const GameEnums* enums, const char* name) {
    return enums != NULL ? enums_find(enums, name) : NULL;
}

// "vsWeakObjectLink<mmoNPC>" gives mmoNPC.
static void template_argument(const char* type, char* argument, size_t capacity) {
    const char* open = strchr(type, '<');
    const char* close = strrchr(type, '>');

    snprintf(argument, capacity, "%.*s", open != NULL && close > open ? (int)(close - open - 1) : 0, open != NULL ? open + 1 : "");
}

static void choose_value(Generator* generator, const char* type, Accessor* accessor) {
    for (size_t index = 0; index < sizeof VALUE_TYPES / sizeof VALUE_TYPES[0]; index++) {
        if (strcmp(VALUE_TYPES[index].game, type) == 0) {
            accessor->access = ACCESS_VALUE;
            snprintf(accessor->cpp_type, sizeof accessor->cpp_type, "%s", VALUE_TYPES[index].cpp);

            return;
        }
    }

    if (strcmp(type, "std::string") == 0) {
        accessor->access = ACCESS_TEXT;
    } else if (find_enum(generator->enums, type) != NULL) {
        accessor->access = ACCESS_ENUM;
        snprintf(accessor->target, sizeof accessor->target, "%s", type);
    }
}

static void choose_access_for_reflected(Generator* generator, const ReflectedField* field, Accessor* accessor) {
    if (strcmp(field->kind, "vsProperty") == 0) {
        choose_value(generator, field->type, accessor);
    } else if (strcmp(field->kind, "vsPropertyObject") == 0 && strncmp(field->type, "vsWeakObjectLink<", 17) == 0) {
        accessor->access = ACCESS_LINK;
        template_argument(field->type, accessor->target, sizeof accessor->target);
    } else if (strcmp(field->kind, "vsPropertyObject") == 0 && strstr(field->type, "Array<") != NULL) {
        accessor->access = ACCESS_OBJECTS;
        template_argument(field->type, accessor->target, sizeof accessor->target);
    } else if (strcmp(field->kind, "vsPropertyObject") == 0) {
        accessor->access = ACCESS_INSIDE;
        snprintf(accessor->target, sizeof accessor->target, "%s", field->type);
    } else if (strcmp(field->kind, "vsPropertyObjectPointer") == 0 || strcmp(field->kind, "vsPropertyObjectLink") == 0) {
        accessor->access = ACCESS_POINTER;
        snprintf(accessor->target, sizeof accessor->target, "%.*s", (int)strcspn(field->type, "*"), field->type);
    }
}

static void choose_access_for_mapped(Generator* generator, const char* type, Accessor* accessor) {
    size_t length = strlen(type);

    if (length > 0 && type[length - 1] == '*') {
        accessor->access = ACCESS_POINTER;
        snprintf(accessor->target, sizeof accessor->target, "%.*s", (int)(length - 1), type);
    } else if (strncmp(type, "vsWeakObjectLink<", 17) == 0) {
        accessor->access = ACCESS_LINK;
        template_argument(type, accessor->target, sizeof accessor->target);
    } else {
        choose_value(generator, type, accessor);
    }
}

// What a function returns for an object: its class here, or the plain pointer when the class isn't here.
static void wrapper_of(const Generator* generator, const char* game_class, char* wrapper, size_t capacity) {
    if (has_class(&generator->classes, game_class)) {
        identifier(game_class, wrapper, capacity);
    } else {
        snprintf(wrapper, capacity, "Object");
    }
}

// A field named like a C++ word, or like a member every class here has, gets an underscore: default_.
static void accessor_name(const char* field, char* name, size_t capacity) {
    for (size_t index = 0; index < sizeof RESERVED / sizeof RESERVED[0]; index++) {
        if (strcmp(RESERVED[index], field) == 0) {
            snprintf(name, capacity, "%s_", field);

            return;
        }
    }

    snprintf(name, capacity, "%s", field);
}

static int compare_accessors(const void* left, const void* right) {
    uint32_t left_offset = ((const Accessor*)left)->offset;
    uint32_t right_offset = ((const Accessor*)right)->offset;

    return left_offset < right_offset ? -1 : left_offset > right_offset;
}

// The class's own fields, in the order they sit in the object.
static int collect_accessors(Generator* generator, const char* class_name, Accessor* accessors, int capacity) {
    int count = 0;

    for (int index = 0; index < generator->reflection->count && count < capacity; index++) {
        const ReflectedField* field = &generator->reflection->fields[index];

        if (!field->has_offset || strcmp(field->class_name, class_name) != 0) {
            continue;
        }

        Accessor* accessor = &accessors[count];
        memset(accessor, 0, sizeof *accessor);
        accessor_name(field->name, accessor->name, sizeof accessor->name);
        snprintf(accessor->lookup, sizeof accessor->lookup, "%s::%s", class_name, field->name);
        snprintf(accessor->game_type, sizeof accessor->game_type, "%s", field->type);
        accessor->offset = field->offset;
        choose_access_for_reflected(generator, field, accessor);
        count++;
    }

    for (int index = 0; generator->mappings != NULL && index < generator->mappings->entry_count; index++) {
        const Entry* entry = &generator->mappings->entries[index];

        if (entry->kind != ENTRY_CLASS || strcmp(entry->name, class_name) != 0) {
            continue;
        }

        for (int member = 0; member < entry->member_count && count < capacity; member++) {
            const Entry* field = &entry->members[member];
            bool already = false;

            for (int existing = 0; existing < count && field->kind == ENTRY_FIELD; existing++) {
                if (accessors[existing].offset == field->offset) {
                    const Note* doc = mappings_note(field, "doc");
                    accessors[existing].doc = doc != NULL ? doc->value : NULL;
                    already = true;
                }
            }

            if (field->kind != ENTRY_FIELD || already) {
                continue;
            }

            Accessor* accessor = &accessors[count];
            const Note* doc = mappings_note(field, "doc");
            memset(accessor, 0, sizeof *accessor);
            accessor_name(field->name, accessor->name, sizeof accessor->name);
            snprintf(accessor->lookup, sizeof accessor->lookup, "%s::%s", class_name, field->name);
            snprintf(accessor->game_type, sizeof accessor->game_type, "%s", field->type);
            accessor->offset = field->offset;
            // The loader checks a mapped field at a place its code uses it; one without such a place can't be checked.
            accessor->by_offset = mappings_note(field, "seen") == NULL;
            accessor->doc = doc != NULL ? doc->value : NULL;
            choose_access_for_mapped(generator, field->type, accessor);
            count++;
        }
    }

    qsort(accessors, (size_t)count, sizeof *accessors, compare_accessors);

    return count;
}

// A word that isn't a C++ name (a C++ word like int, or "event:/UI/button click") becomes one: int_,
// event__UI_button_click. A value without a word, or whose name repeats, is named by its number.
static void value_name(const GameEnum* game_enum, int index, char* name, size_t capacity) {
    const char* word = game_enum->values[index];
    size_t used = 0;

    if (word[0] >= '0' && word[0] <= '9') {
        name[used++] = '_';
    }

    for (const char* character = word; *character != '\0' && used + 2 < capacity; character++) {
        bool keeps = (*character >= 'a' && *character <= 'z') || (*character >= 'A' && *character <= 'Z')
            || (*character >= '0' && *character <= '9') || *character == '_';
        name[used++] = keeps ? *character : '_';
    }

    name[used] = '\0';

    char word_name[ENUM_VALUE_CAPACITY * 2];
    snprintf(word_name, sizeof word_name, "%s", name);
    accessor_name(word_name, name, capacity);

    if (name[0] == '\0') {
        snprintf(name, capacity, "Value%d", index);
    }
}

static void enum_values(const GameEnum* game_enum, FILE* out) {
    char (*names)[ENUM_VALUE_CAPACITY * 2] = calloc(ENUM_MAX_VALUES, sizeof *names);

    for (int index = 0; names != NULL && index < game_enum->count; index++) {
        value_name(game_enum, index, names[index], sizeof names[index]);

        for (int earlier = 0; earlier < index; earlier++) {
            if (strcmp(names[earlier], names[index]) == 0) {
                snprintf(names[index], sizeof names[index], "Value%d", index);
            }
        }

        fprintf(out, "%s%s = %d", index > 0 ? ", " : "", names[index], index);
    }

    free(names);
}

static void write_own_enums(Generator* generator, const char* class_name) {
    size_t length = strlen(class_name);

    for (int index = 0; index < generator->enums->count; index++) {
        const GameEnum* game_enum = &generator->enums->items[index];

        if (strncmp(game_enum->name, class_name, length) != 0 || strncmp(game_enum->name + length, "::", 2) != 0
            || strchr(game_enum->name + length + 2, ':') != NULL) {
            continue;
        }

        char full[NAME_CAPACITY];
        identifier(game_enum->name, full, sizeof full);
        fprintf(generator->out, "    using %s = %s;\n", game_enum->name + length + 2, full);
    }
}

// The C++ type a function gives for the field: a reference to change it in place where the game keeps it plainly.
static void result_type(const Generator* generator, const Accessor* accessor, char* type, size_t capacity) {
    char wrapper[NAME_CAPACITY];

    switch (accessor->access) {
    case ACCESS_VALUE:
        snprintf(type, capacity, "%s&", accessor->cpp_type);
        break;
    case ACCESS_TEXT:
        snprintf(type, capacity, "game::String&");
        break;
    case ACCESS_ENUM:
        // Every enum is at the top by its full name: mmoNPC::State is mmoNPC_State.
        identifier(accessor->target, wrapper, sizeof wrapper);
        snprintf(type, capacity, "%s&", wrapper);
        break;
    case ACCESS_OBJECTS:
        snprintf(type, capacity, "game::Objects&");
        break;
    case ACCESS_LINK:
    case ACCESS_POINTER:
    case ACCESS_INSIDE:
        wrapper_of(generator, accessor->target, type, capacity);
        break;
    case ACCESS_NONE:
        type[0] = '\0';
        break;
    }
}

static void write_declarations(Generator* generator, const Accessor* accessors, int count) {
    char type[TYPE_CAPACITY];

    for (int index = 0; index < count; index++) {
        const Accessor* accessor = &accessors[index];

        result_type(generator, accessor, type, sizeof type);

        if (accessor->access == ACCESS_NONE) {
            fprintf(generator->out, "    // %s: %s at +0x%x, a kind of field this header doesn't reach yet\n", accessor->name,
                accessor->game_type, accessor->offset);
            continue;
        }

        if (accessor->doc != NULL) {
            fprintf(generator->out, "    // %s\n", accessor->doc);
        }

        fprintf(generator->out, "    %s %s() const;%s\n", type, accessor->name, accessor->by_offset ? " // mt2-mappings, by offset" : "");

        if (accessor->access == ACCESS_POINTER) {
            fprintf(generator->out, "    void set_%s(%s value) const;\n", accessor->name, type);
        }
    }
}

static void write_class(Generator* generator, const char* class_name) {
    char wrapper[NAME_CAPACITY];
    char base[NAME_CAPACITY];
    char base_wrapper[NAME_CAPACITY] = "Object";
    Accessor* accessors = malloc(512 * sizeof *accessors);
    int count = accessors != NULL ? collect_accessors(generator, class_name, accessors, 512) : 0;

    identifier(class_name, wrapper, sizeof wrapper);

    if (real_base(generator->image, class_name, base, sizeof base) && has_class(&generator->classes, base)) {
        identifier(base, base_wrapper, sizeof base_wrapper);
    }

    fprintf(generator->out, "\n// %s\nclass %s : public %s {\npublic:\n    using %s::%s;\n", class_name, wrapper, base_wrapper, base_wrapper,
        base_wrapper);
    fprintf(generator->out, "    static constexpr const char* game_class = \"%s\";\n", class_name);
    write_own_enums(generator, class_name);

    if (count > 0) {
        fprintf(generator->out, "\n");
        write_declarations(generator, accessors, count);
    }

    fprintf(generator->out, "};\n");
    free(accessors);
}

static void write_definition(Generator* generator, const char* class_name, const Accessor* accessor) {
    char wrapper[NAME_CAPACITY];
    char type[TYPE_CAPACITY];
    char place[NAME_CAPACITY];
    FILE* out = generator->out;

    identifier(class_name, wrapper, sizeof wrapper);
    result_type(generator, accessor, type, sizeof type);

    if (accessor->by_offset) {
        snprintf(place, sizeof place, "0x%x", accessor->offset);
    } else {
        snprintf(place, sizeof place, "\"%s\"", accessor->lookup);
    }

    fprintf(out, "inline %s %s::%s() const { ", type, wrapper, accessor->name);

    switch (accessor->access) {
    case ACCESS_VALUE:
    case ACCESS_ENUM: {
        char value_type[TYPE_CAPACITY];
        snprintf(value_type, sizeof value_type, "%.*s", (int)strlen(type) - 1, type);
        fprintf(out, "return game::field<%s>(self, %s); }\n", value_type, place);
        break;
    }
    case ACCESS_TEXT:
        fprintf(out, "return game::field<game::String>(self, %s); }\n", place);
        break;
    case ACCESS_OBJECTS:
        fprintf(out, "return game::field<game::Objects>(self, %s); }\n", place);
        break;
    case ACCESS_LINK:
        fprintf(out, "return %s(game::field<game::Link>(self, %s).get()); }\n", type, place);
        break;
    case ACCESS_POINTER:
        fprintf(out, "return %s(game::field<void*>(self, %s)); }\n", type, place);
        fprintf(out, "inline void %s::set_%s(%s value) const { game::field<void*>(self, %s) = value.get(); }\n", wrapper, accessor->name,
            type, place);
        break;
    case ACCESS_INSIDE:
        if (accessor->by_offset) {
            fprintf(out, "return %s(static_cast<char*>(self) + %s); }\n", type, place);
        } else {
            fprintf(out, "return %s(game::object_in(self, %s)); }\n", type, place);
        }

        break;
    case ACCESS_NONE:
        break;
    }
}

static void write_definitions(Generator* generator, const char* class_name) {
    Accessor* accessors = malloc(512 * sizeof *accessors);
    int count = accessors != NULL ? collect_accessors(generator, class_name, accessors, 512) : 0;

    for (int index = 0; index < count; index++) {
        if (accessors[index].access != ACCESS_NONE) {
            write_definition(generator, class_name, &accessors[index]);
        }
    }

    free(accessors);
}

// Bases first, so each class can build on the one before.
static void write_in_order(Generator* generator, const char* class_name, ClassSet* written) {
    char base[NAME_CAPACITY];

    if (has_class(written, class_name)) {
        return;
    }

    add_class(written, class_name);

    if (real_base(generator->image, class_name, base, sizeof base) && has_class(&generator->classes, base)) {
        write_in_order(generator, base, written);
    }

    write_class(generator, class_name);
}

static void choose_classes(Generator* generator) {
    char base[NAME_CAPACITY];

    for (int index = 0; index < generator->reflection->count; index++) {
        if (generator->reflection->fields[index].has_offset) {
            add_class(&generator->classes, generator->reflection->fields[index].class_name);
        }
    }

    for (int index = 0; generator->mappings != NULL && index < generator->mappings->entry_count; index++) {
        const Entry* entry = &generator->mappings->entries[index];

        if (entry->kind == ENTRY_CLASS && strncmp(entry->name, "std::", 5) != 0 && find_enum(generator->enums, entry->name) == NULL) {
            add_class(&generator->classes, entry->name);
        }
    }

    // Each class's bases, so it can build on them.
    for (int index = 0; index < generator->classes.count; index++) {
        if (real_base(generator->image, generator->classes.names[index], base, sizeof base)) {
            add_class(&generator->classes, base);
        }
    }
}

// Every enum by its full name (mmoNPC::State is mmoNPC_State), before the classes, so any class can use any enum; each
// class also names its own ones (mt2::mmoNPC::State).
static void write_enums(Generator* generator) {
    for (int index = 0; index < generator->enums->count; index++) {
        const GameEnum* game_enum = &generator->enums->items[index];
        char name[NAME_CAPACITY];

        identifier(game_enum->name, name, sizeof name);
        fprintf(generator->out, "enum class %s : int { ", name);
        enum_values(game_enum, generator->out);
        fprintf(generator->out, " };\n");
    }
}

static bool is_value_type(const char* name) {
    for (size_t index = 0; index < sizeof VALUE_TYPES / sizeof VALUE_TYPES[0]; index++) {
        if (strcmp(VALUE_TYPES[index].game, name) == 0) {
            return true;
        }
    }

    return false;
}


int command_headers(const wchar_t* exe, const wchar_t* output, const wchar_t* mappings_folder) {
    wchar_t path[GAME_PATH_CAPACITY];
    char build[64];
    GameImage image;
    Reflection reflection;
    GameEnums enums;
    Mappings mappings;
    bool have_mappings = false;

    if (!game_exe_load_symbols(exe, path) || !game_image_load(path, &image)) {
        return 1;
    }

    if (mappings_folder != NULL && !(have_mappings = mappings_load(mappings_folder, &mappings))) {
        game_image_free(&image);

        return 1;
    }

    game_exe_build_name(path, build, sizeof build);

    bool read = reflection_read(&image, &reflection);
    bool have_enums = read && enums_read(&image, &enums);
    Generator* generator = calloc(1, sizeof *generator);
    FILE* out = read && have_enums && generator != NULL ? _wfopen(output, L"wb") : NULL;

    if (out == NULL) {
        fwprintf(stderr, L"%ls couldn't be written\n", output);
        free(generator);
        game_image_free(&image);

        return 1;
    }

    *generator = (Generator){ &image, &reflection, &enums, have_mappings ? &mappings : NULL, { { 0 }, 0 }, out };
    choose_classes(generator);

    // The engine's small value types are written by hand above, not as classes.
    for (int index = 0; index < generator->classes.count; index++) {
        if (is_value_type(generator->classes.names[index])) {
            generator->classes.names[index][0] = '\0';
        }
    }

    fprintf(out, "// mt2game.hpp: MMORPG Tycoon 2 %s's classes for plugins, made by mt2sdk headers from MT2.exe%s.\n", build,
        have_mappings ? " and mt2-mappings" : "");
    fputs(PREAMBLE, out);
    fprintf(out, "\n// The game's enums, with the words its data files and saves use for each value.\n");
    write_enums(generator);
    fprintf(out, "\n");

    for (int index = 0; index < generator->classes.count; index++) {
        char wrapper[NAME_CAPACITY];

        if (generator->classes.names[index][0] != '\0') {
            identifier(generator->classes.names[index], wrapper, sizeof wrapper);
            fprintf(out, "class %s;\n", wrapper);
        }
    }

    ClassSet* written = calloc(1, sizeof *written);

    for (int index = 0; written != NULL && index < generator->classes.count; index++) {
        if (generator->classes.names[index][0] != '\0') {
            write_in_order(generator, generator->classes.names[index], written);
        }
    }

    fprintf(out, "\n// The functions above, after every class, since they return each other.\n\n");

    for (int index = 0; index < generator->classes.count; index++) {
        if (generator->classes.names[index][0] != '\0') {
            write_definitions(generator, generator->classes.names[index]);
        }
    }

    fprintf(out, "\n}\n");
    fclose(out);
    wprintf(L"%d classes written to %ls\n", generator->classes.count, output);

    for (int index = 0; written != NULL && index < written->count; index++) {
        free(written->names[index]);
    }

    for (int index = 0; index < generator->classes.count; index++) {
        free(generator->classes.names[index]);
    }

    free(written);
    free(generator);
    enums_free(&enums);
    reflection_free(&reflection);

    if (have_mappings) {
        mappings_free(&mappings);
    }

    game_image_free(&image);

    return 0;
}
