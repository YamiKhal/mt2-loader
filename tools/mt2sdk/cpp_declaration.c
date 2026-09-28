#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cpp_declaration.h"

#define PART_CAPACITY 512

typedef struct Output {
    char* text;
    size_t capacity;
    size_t length;
} Output;

typedef struct TypeRule {
    const char* game_type;
    const char* cpp_type;
} TypeRule;

// Types that mean the same in the game and in any compiler, and the game's string, which mt2loader.hpp provides.
static const TypeRule KNOWN_TYPES[] = {
    { "bool", "bool" },
    { "char", "char" },
    { "signed char", "signed char" },
    { "unsigned char", "unsigned char" },
    { "short", "short" },
    { "unsigned short", "unsigned short" },
    { "int", "int" },
    { "unsigned int", "unsigned int" },
    { "long", "long" },
    { "unsigned long", "unsigned long" },
    { "long long", "long long" },
    { "unsigned long long", "unsigned long long" },
    { "float", "float" },
    { "double", "double" },
    { "wchar_t", "wchar_t" },
    { "char const*", "const char*" },
    { "char*", "char*" },
    { "std::string const&", "const game::String&" },
    { "std::string&", "game::String&" },
    { "std::string const*", "const game::String*" },
    { "std::string*", "game::String*" },
    { "std::string", "game::String /* by value: not supported yet */" },
};

#define KNOWN_TYPE_COUNT (sizeof KNOWN_TYPES / sizeof KNOWN_TYPES[0])


static void add(Output* output, const char* format, ...) {
    va_list arguments;

    if (output->length >= output->capacity) {
        return;
    }

    va_start(arguments, format);
    int written = vsnprintf(output->text + output->length, output->capacity - output->length, format, arguments);
    va_end(arguments);

    if (written > 0) {
        output->length += (size_t)written;
    }
}

static bool ends_with(const char* text, const char* suffix) {
    size_t length = strlen(text);
    size_t suffix_length = strlen(suffix);

    return length >= suffix_length && strcmp(text + length - suffix_length, suffix) == 0;
}

// The '(' that opens the parameter list ending at the last ')', or NULL for a global (no parameter list).
static const char* parameter_list_start(const char* name, const char** close) {
    *close = strrchr(name, ')');

    if (*close == NULL) {
        return NULL;
    }

    int depth = 0;

    for (const char* position = *close; position >= name; position--) {
        if (*position == ')' || *position == '>') {
            depth++;
        } else if ((*position == '(' || *position == '<') && --depth == 0 && *position == '(') {
            return position;
        }
    }

    return NULL;
}

static void add_parameter(Output* output, const char* type) {
    for (size_t index = 0; index < KNOWN_TYPE_COUNT; index++) {
        if (strcmp(type, KNOWN_TYPES[index].game_type) == 0) {
            add(output, "%s", KNOWN_TYPES[index].cpp_type);

            return;
        }
    }

    // The game's own classes are passed as pointers: C++ references are pointers underneath.
    if (ends_with(type, "const&") || ends_with(type, "const*")) {
        add(output, "const void* /* %s */", type);
    } else if (ends_with(type, "&") || ends_with(type, "*")) {
        add(output, "void* /* %s */", type);
    } else {
        add(output, "int /* %s, if it's an enum */", type);
    }
}

static void add_parameters(Output* output, const char* start, const char* end) {
    char type[PART_CAPACITY];
    size_t length = 0;
    int depth = 0;
    bool first = true;

    for (const char* position = start; position <= end; position++) {
        bool at_end = position == end;
        char character = at_end ? ',' : *position;

        if (character == '(' || character == '<') {
            depth++;
        } else if (character == ')' || character == '>') {
            depth--;
        }

        if (character != ',' || depth > 0) {
            if (length + 1 < sizeof type && !(length == 0 && character == ' ')) {
                type[length++] = character;
            }

            continue;
        }

        type[length] = '\0';

        if (length > 0 && strcmp(type, "void") != 0) {
            if (!first) {
                add(output, ", ");
            }

            add_parameter(output, type);
            first = false;
        }

        length = 0;
    }
}

// "mmoCharacter::EquipWeaponModel" -> "equip_weapon_model"
static void add_variable_name(Output* output, const char* qualified, size_t length) {
    const char* start = qualified;

    for (size_t index = 0; index + 1 < length; index++) {
        if (qualified[index] == ':' && qualified[index + 1] == ':') {
            start = qualified + index + 2;
        }
    }

    const char* end = qualified + length;

    if (strncmp(start, "operator", 8) == 0 || *start == '~') {
        add(output, "%s", *start == '~' ? "destructor" : "operator_function");

        return;
    }

    bool previous_lower = false;
    bool written = false;
    bool separate = false;

    for (const char* position = start; position < end; position++) {
        char character = *position;

        if (!isalnum((unsigned char)character)) {
            separate = written;

            continue;
        }

        if (separate || (isupper((unsigned char)character) && previous_lower && written)) {
            add(output, "_");
            separate = false;
        }

        add(output, "%c", (char)tolower((unsigned char)character));
        previous_lower = islower((unsigned char)character) || isdigit((unsigned char)character);
        written = true;
    }
}


bool cpp_declaration(const char* name, char* declaration, size_t capacity) {
    Output output = { declaration, capacity, 0 };
    const char* close = NULL;
    const char* open = parameter_list_start(name, &close);

    declaration[0] = '\0';

    if (open == NULL) {
        add(&output, "game::Address ");
        add_variable_name(&output, name, strlen(name));
        add(&output, " = game::find(\"%s\");", name);

        return false;
    }

    size_t qualified_length = (size_t)(open - name);
    bool is_method = strstr(name, "::") != NULL && strstr(name, "::") < open;
    bool is_const = ends_with(close, ") const");

    char parameters[PART_CAPACITY];
    Output parameter_output = { parameters, sizeof parameters, 0 };

    parameters[0] = '\0';
    add_parameters(&parameter_output, open + 1, close);

    // A method's parameters follow the object.
    add(&output, "game::Function<RESULT(");

    if (is_method) {
        add(&output, "%s%s", is_const ? "const void* object" : "void* object", parameters[0] != '\0' ? ", " : "");
    }

    add(&output, "%s", parameters);
    add(&output, ")> ");
    add_variable_name(&output, name, qualified_length);
    add(&output, "{\"%s\"};", name);

    return true;
}
