#include "readability.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define LINE_CAPACITY 16384
#define WORD_CAPACITY 256

const char* const READABILITY_NAMES[READABILITY_KIND_COUNT] = {
    "unnamed fields", "unnamed locals", "register leftovers", "stack temporaries", "gotos", "unfolded asserts",
};


static bool starts_with(const char* text, const char* start) {
    return strncmp(text, start, strlen(start)) == 0;
}

static bool all_of(const char* text, int (*test)(int)) {
    if (*text == '\0') {
        return false;
    }

    for (; *text != '\0'; text++) {
        if (!test((unsigned char)*text)) {
            return false;
        }
    }

    return true;
}

static int is_hex_digit(int character) {
    return isdigit(character) || (character >= 'a' && character <= 'f');
}

static int is_lower(int character) {
    return islower(character);
}

// field_0x4c, field1_0x4.
static bool is_unnamed_field(const char* word) {
    if (!starts_with(word, "field")) {
        return false;
    }

    const char* rest = word + 5;

    while (isdigit((unsigned char)*rest)) {
        rest++;
    }

    return starts_with(rest, "_0x") && all_of(rest + 3, is_hex_digit);
}

// A name Ghidra made up: iVar3, pcVar12, local_48, auStack_28, param_2.
static bool is_unnamed_local(const char* word) {
    const char* var = strstr(word, "Var");

    if (var != NULL && var > word && var - word <= 4 && all_of(var + 3, isdigit)) {
        char prefix[8] = { 0 };
        memcpy(prefix, word, (size_t)(var - word));

        return all_of(prefix, is_lower);
    }

    const char* stack = strstr(word, "Stack_");

    if (stack != NULL && stack > word && stack - word <= 3 && all_of(stack + 6, is_hex_digit)) {
        return true;
    }

    return (starts_with(word, "local_") && all_of(word + 6, is_hex_digit)) || (starts_with(word, "param_") && all_of(word + 6, isdigit));
}

static bool is_register_leftover(const char* word) {
    return (starts_with(word, "in_") && isupper((unsigned char)word[3])) || starts_with(word, "extraout_") || starts_with(word, "unaff_");
}

static void count_word(const char* word, Readability* readability) {
    if (is_unnamed_field(word)) {
        readability->counts[UNNAMED_FIELD]++;
    } else if (is_unnamed_local(word)) {
        readability->counts[UNNAMED_LOCAL]++;
    } else if (is_register_leftover(word)) {
        readability->counts[REGISTER_LEFTOVER]++;
    } else if (starts_with(word, "stack0x")) {
        readability->counts[STACK_TEMPORARY]++;
    } else if (strcmp(word, "goto") == 0) {
        readability->counts[GOTO]++;
    } else if (starts_with(word, "vsFailedAssert")) {
        readability->counts[UNFOLDED_ASSERT]++;
    }
}

// Words outside comments and strings.
static void count_line(const char* line, Readability* readability) {
    char word[WORD_CAPACITY];
    size_t length = 0;
    bool in_string = false;

    for (const char* at = line;; at++) {
        char character = *at;
        bool is_word_character = isalnum((unsigned char)character) || character == '_';

        if (!in_string && is_word_character) {
            if (length + 1 < sizeof word) {
                word[length++] = character;
            }

            continue;
        }

        if (length > 0) {
            word[length] = '\0';
            count_word(word, readability);
            length = 0;
        }

        if (character == '\0' || (!in_string && character == '/' && at[1] == '/')) {
            return;
        }

        if (character == '"' && (at == line || at[-1] != '\\')) {
            in_string = !in_string;
        }
    }
}


bool readability_of_file(const wchar_t* path, Readability* readability) {
    static char line[LINE_CAPACITY];
    FILE* file = _wfopen(path, L"r");

    memset(readability, 0, sizeof *readability);

    if (file == NULL) {
        return false;
    }

    while (fgets(line, sizeof line, file) != NULL) {
        readability->lines++;

        if (starts_with(line, "// 0x")) {
            readability->functions++;
        }

        count_line(line, readability);
    }

    fclose(file);

    return true;
}

int readability_total(const Readability* readability) {
    int total = 0;

    for (int kind = 0; kind < READABILITY_KIND_COUNT; kind++) {
        total += readability->counts[kind];
    }

    return total;
}
