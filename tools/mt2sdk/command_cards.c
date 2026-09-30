#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "commands.h"

#define PATH_CAPACITY 1024
#define LINE_CAPACITY 4096
#define NAME_CAPACITY 512
#define USERS_CAPACITY 2048
#define CARDS_WITHOUT_CLASS 100
#define LINES_PER_FUNCTION 2
#define LINES_PER_CARD 6
#define SHOWN_LINE_LENGTH 140
#define NEIGHBOR_DISTANCE 0x18

// One line of unnamed_fields.tsv: a field, one file it's used in, and the first few functions there.
typedef struct Use {
    char owner[NAME_CAPACITY];
    char offset[16];
    char type[NAME_CAPACITY];
    char file[PATH_CAPACITY];
    char users[USERS_CAPACITY];
    int uses;
    int functions;
    bool read;
} Use;

// The lines a card quotes, and which function each is in: only for the cards that are printed.
typedef struct CardText {
    char lines[LINES_PER_CARD][SHOWN_LINE_LENGTH + 1];
    char line_owners[LINES_PER_CARD][NAME_CAPACITY];
    int line_count;
} CardText;

typedef struct Card {
    const Use* first;
    int use_count;
    int uses;
    int functions;
    CardText* text;
} Card;


static bool read_uses(const wchar_t* path, Use** uses, int* count) {
    FILE* file = _wfopen(path, L"r");
    char line[LINE_CAPACITY];
    int capacity = 0;

    if (file == NULL) {
        fwprintf(stderr, L"%ls isn't there: it's written by mt2sdk decompile\n", path);

        return false;
    }

    while (fgets(line, sizeof line, file) != NULL) {
        char* columns[7];
        int found = 0;

        line[strcspn(line, "\r\n")] = '\0';

        for (char* column = strtok(line, "\t"); column != NULL && found < 7; column = strtok(NULL, "\t")) {
            columns[found++] = column;
        }

        if (found < 7) {
            continue;
        }

        if (*count == capacity) {
            capacity = capacity == 0 ? 4096 : capacity * 2;
            *uses = realloc(*uses, (size_t)capacity * sizeof **uses);
        }

        Use* use = &(*uses)[(*count)++];
        snprintf(use->owner, sizeof use->owner, "%s", columns[0]);
        snprintf(use->offset, sizeof use->offset, "%s", columns[1]);
        snprintf(use->type, sizeof use->type, "%s", columns[2]);
        snprintf(use->file, sizeof use->file, "%s", columns[3]);
        snprintf(use->users, sizeof use->users, "%s", columns[6]);
        use->read = false;
        use->uses = atoi(columns[4]);
        use->functions = atoi(columns[5]);
    }

    fclose(file);

    return true;
}

static int by_field(const void* left, const void* right) {
    const Use* a = left;
    const Use* b = right;
    int owners = strcmp(a->owner, b->owner);

    return owners != 0 ? owners : strcmp(a->offset, b->offset);
}

static int by_functions(const void* left, const void* right) {
    const Card* a = left;
    const Card* b = right;

    return a->functions != b->functions ? b->functions - a->functions : b->uses - a->uses;
}

// The uses, sorted by field, become one card per field.
static Card* make_cards(Use* uses, int use_count, int* card_count) {
    Card* cards = calloc((size_t)(use_count > 0 ? use_count : 1), sizeof *cards);

    qsort(uses, (size_t)use_count, sizeof *uses, by_field);
    *card_count = 0;

    for (int index = 0; index < use_count; index++) {
        Card* last = *card_count > 0 ? &cards[*card_count - 1] : NULL;

        if (last == NULL || by_field(last->first, &uses[index]) != 0) {
            last = &cards[(*card_count)++];
            last->first = &uses[index];
        }

        last->use_count++;
        last->uses += uses[index].uses;
        last->functions += uses[index].functions;
    }

    return cards;
}

// Whether the line reads this field: field_0x361 or field1_0x361, not field_0x3610.
static bool mentions_field(const char* line, const char* offset) {
    char wanted[32];
    size_t length;

    snprintf(wanted, sizeof wanted, "_%s", offset);
    length = strlen(wanted);

    for (const char* at = strstr(line, wanted); at != NULL; at = strstr(at + 1, wanted)) {
        const char* start = at;

        while (start > line && isdigit((unsigned char)start[-1])) {
            start--;
        }

        bool after_field = start - line >= 5 && strncmp(start - 5, "field", 5) == 0;
        bool whole = !isalnum((unsigned char)at[length]) && at[length] != '_';

        if (after_field && whole) {
            return true;
        }
    }

    return false;
}

// "void mmoDistrict::_RegenerateGeo(bool rebuild)" names mmoDistrict::_RegenerateGeo.
static void function_name(const char* signature, char* name) {
    const char* open = strchr(signature, '(');
    const char* start = open;

    name[0] = '\0';

    if (open == NULL) {
        return;
    }

    while (start > signature && start[-1] != ' ' && start[-1] != '*' && start[-1] != '&') {
        start--;
    }

    snprintf(name, NAME_CAPACITY, "%.*s", (int)(open - start), start);
}

static bool is_listed(const char* users, const char* name) {
    char copy[USERS_CAPACITY];

    snprintf(copy, sizeof copy, "%s", users);

    for (char* user = strtok(copy, ";"); user != NULL; user = strtok(NULL, ";")) {
        char* tag = strstr(user, "[abi:cxx11]");

        if (tag != NULL) {
            *tag = '\0';
        }

        if (strcmp(user, name) == 0) {
            return true;
        }
    }

    return false;
}

static void add_line(Card* card, const char* function, const char* line) {
    const char* text = line;

    while (*text == ' ' || *text == '\t') {
        text++;
    }

    for (int index = 0; index < card->text->line_count; index++) {
        if (strncmp(card->text->lines[index], text, SHOWN_LINE_LENGTH) == 0) {
            return;
        }
    }

    snprintf(card->text->lines[card->text->line_count], SHOWN_LINE_LENGTH + 1, "%.*s", SHOWN_LINE_LENGTH, text);
    card->text->lines[card->text->line_count][strcspn(card->text->lines[card->text->line_count], "\r\n")] = '\0';
    snprintf(card->text->line_owners[card->text->line_count], NAME_CAPACITY, "%s", function);
    card->text->line_count++;
}

// Reads one .cpp once for every card with a use in it.
static void collect_lines(const wchar_t* source, const char* file_path, Card** cards, const Use** uses, int count) {
    wchar_t path[PATH_CAPACITY];
    char line[LINE_CAPACITY];
    char current[NAME_CAPACITY] = "";
    int per_function[64] = { 0 };
    bool after_address = false;

    swprintf(path, PATH_CAPACITY, L"%ls\\%hs", source, file_path);

    FILE* file = _wfopen(path, L"r");

    while (file != NULL && fgets(line, sizeof line, file) != NULL) {
        if (strncmp(line, "// 0x", 5) == 0) {
            after_address = true;
            continue;
        }

        if (after_address && line[0] != '/' && line[0] != ' ' && strchr(line, '(') != NULL) {
            function_name(line, current);
            memset(per_function, 0, sizeof per_function);
            after_address = false;
            continue;
        }

        for (int index = 0; index < count && index < 64; index++) {
            Card* card = cards[index];

            if (card->text->line_count < LINES_PER_CARD && per_function[index] < LINES_PER_FUNCTION && is_listed(uses[index]->users, current)
                && mentions_field(line, uses[index]->offset)) {
                add_line(card, current, line);
                per_function[index]++;
            }
        }
    }

    if (file != NULL) {
        fclose(file);
    }
}

// The named fields around the offset, from the class's declaration in the .h beside the file.
static void print_neighbors(const wchar_t* source, const Use* use) {
    wchar_t path[PATH_CAPACITY];
    char header[PATH_CAPACITY];
    char line[LINE_CAPACITY];
    char opening[NAME_CAPACITY];
    const char* short_name = strrchr(use->owner, ':') != NULL ? strrchr(use->owner, ':') + 1 : use->owner;
    long offset = strtol(use->offset, NULL, 16);
    bool inside = false;
    int shown = 0;

    snprintf(header, sizeof header, "%s", use->file);

    char* dot = strrchr(header, '.');

    if (dot != NULL) {
        strcpy(dot, ".h");
    }

    snprintf(opening, sizeof opening, "class %s", short_name);
    swprintf(path, PATH_CAPACITY, L"%ls\\%hs", source, header);

    FILE* file = _wfopen(path, L"r");

    while (file != NULL && fgets(line, sizeof line, file) != NULL) {
        size_t opening_length = strlen(opening);

        if (strncmp(line, opening, opening_length) == 0 && (line[opening_length] == ' ' || line[opening_length] == '\n')) {
            inside = true;
            continue;
        }

        if (inside && strncmp(line, "};", 2) == 0) {
            break;
        }

        char* place = inside ? strstr(line, "// +0x") : NULL;

        if (place == NULL) {
            continue;
        }

        long at = strtol(place + 6, NULL, 16);

        if (labs(at - offset) <= NEIGHBOR_DISTANCE) {
            char* declaration = line;

            while (*declaration == ' ') {
                declaration++;
            }

            char* end = strchr(declaration, ';');

            printf("%s +0x%lx %.*s", shown == 0 ? "  near:" : ";", at, end != NULL ? (int)(end - declaration) : 0, declaration);
            shown++;
        }
    }

    if (shown > 0) {
        printf("\n");
    }

    if (file != NULL) {
        fclose(file);
    }
}

static void print_card(const wchar_t* source, const Card* card) {
    const Use* use = card->first;
    size_t owner_length = strlen(use->owner);

    printf("%s +%s %s | %d functions, %d uses\n", use->owner, use->offset, use->type, card->functions, card->uses);
    print_neighbors(source, use);

    for (int index = 0; index < card->text->line_count; index++) {
        const char* function = card->text->line_owners[index];

        // A method of the field's own class is written without the class.
        if (strncmp(function, use->owner, owner_length) == 0 && strncmp(function + owner_length, "::", 2) == 0) {
            function += owner_length + 2;
        }

        printf("  %s: %s\n", function, card->text->lines[index]);
    }

    printf("\n");
}


int command_mappings_cards(const wchar_t* workspace, const wchar_t* only_class) {
    wchar_t full[PATH_CAPACITY];
    wchar_t source[PATH_CAPACITY];
    wchar_t path[PATH_CAPACITY];
    char wanted[NAME_CAPACITY] = "";
    Use* uses = NULL;
    int use_count = 0;
    int card_count = 0;

    GetFullPathNameW(workspace, PATH_CAPACITY, full, NULL);
    swprintf(source, PATH_CAPACITY, L"%ls\\source", full);
    swprintf(path, PATH_CAPACITY, L"%ls\\unnamed_fields.tsv", source);

    if (only_class != NULL) {
        WideCharToMultiByte(CP_UTF8, 0, only_class, -1, wanted, sizeof wanted, NULL, NULL);
    }

    if (!read_uses(path, &uses, &use_count)) {
        return 1;
    }

    Card* cards = make_cards(uses, use_count, &card_count);

    qsort(cards, (size_t)card_count, sizeof *cards, by_functions);

    int chosen_count = 0;

    for (int index = 0; index < card_count; index++) {
        bool wanted_card = wanted[0] != '\0' ? strcmp(cards[index].first->owner, wanted) == 0
            : chosen_count < CARDS_WITHOUT_CLASS && !is_library_class(cards[index].first->owner);

        if (wanted_card) {
            cards[chosen_count++] = cards[index];
        }
    }

    CardText* texts = calloc((size_t)(chosen_count > 0 ? chosen_count : 1), sizeof *texts);

    for (int index = 0; index < chosen_count; index++) {
        cards[index].text = &texts[index];
    }

    // Each file is read once, for every chosen card with a use in it (at most 64 at a time).
    for (int start = 0; start < chosen_count; start++) {
        for (int use_index = 0; use_index < cards[start].use_count; use_index++) {
            const Use* use = cards[start].first + use_index;
            Card* batch[64];
            const Use* batch_uses[64];
            int batch_count = 0;

            if (use->read) {
                continue;
            }

            for (int other = start; other < chosen_count && batch_count < 64; other++) {
                for (int other_use = 0; other_use < cards[other].use_count; other_use++) {
                    const Use* candidate = cards[other].first + other_use;

                    if (!candidate->read && strcmp(candidate->file, use->file) == 0 && batch_count < 64) {
                        batch[batch_count] = &cards[other];
                        batch_uses[batch_count++] = candidate;
                    }
                }
            }

            char file_path[PATH_CAPACITY];
            snprintf(file_path, sizeof file_path, "%s", use->file);
            collect_lines(source, file_path, batch, batch_uses, batch_count);

            for (int done = 0; done < batch_count; done++) {
                ((Use*)batch_uses[done])->read = true;
            }
        }
    }

    for (int index = 0; index < chosen_count; index++) {
        print_card(source, &cards[index]);
    }

    if (chosen_count == 0) {
        printf("%s has no unnamed fields the code uses\n", wanted[0] != '\0' ? wanted : "The source");
    }

    free(texts);
    free(cards);
    free(uses);

    return 0;
}
