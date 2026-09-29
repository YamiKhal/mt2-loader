#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../../src/core/symbols.h"
#include "commands.h"
#include "disassembly.h"
#include "game_exe.h"
#include "game_image.h"
#include "places.h"

#define MAX_TERMS 16
#define MAX_PLACES 2000
#define MAX_TEXTS 300
#define VTABLE_PREFIX "vtable for "
// A vtable's first two entries are the offset to the object's start and the class's type info.
#define VTABLE_HEADER 16

typedef struct Found {
    uint32_t from;
    ReferenceKind kind;
} Found;

typedef struct FoundList {
    Found* items;
    int count;
    int total;
} FoundList;

typedef struct RangeSearch {
    uint32_t start;
    uint32_t end;
    FoundList found;
} RangeSearch;

typedef struct Text {
    uint32_t start;
    uint32_t end;
    FoundList found;
} Text;

typedef struct TextSearch {
    Text* texts;
    int count;
} TextSearch;


static bool load(const wchar_t* exe, GameImage* image) {
    wchar_t path[GAME_PATH_CAPACITY];

    return game_exe_load_symbols(exe, path) && game_image_load(path, image);
}

static void add_found(FoundList* list, uint32_t from, ReferenceKind kind) {
    list->total++;

    if (list->items != NULL && list->count < MAX_PLACES) {
        list->items[list->count++] = (Found){ from, kind };
    }
}

static void print_found(const FoundList* list, const char* indent) {
    for (int index = 0; index < list->count; index++) {
        const Found* found = &list->items[index];
        char name[PLACE_CAPACITY];
        place_name(found->from, name, sizeof name);

        uint32_t offset = 0;
        const char* plus = strrchr(name, '+');
        bool in_vtable = strncmp(name, VTABLE_PREFIX, strlen(VTABLE_PREFIX)) == 0 && plus != NULL;
        offset = in_vtable ? (uint32_t)strtoul(plus + 1, NULL, 16) : 0;

        printf("%s%-70s 0x%llx  %s", indent, name, PREFERRED_BASE + found->from, reference_kind_word(found->kind));

        if (in_vtable && offset >= VTABLE_HEADER) {
            printf(" (virtual, slot %u)", (offset - VTABLE_HEADER) / 8);
        }

        printf("\n");
    }

    if (list->total > list->count) {
        printf("%s... and %d more\n", indent, list->total - list->count);
    }
}

static void found_in_range(uint32_t from, const Reference* reference, void* opaque) {
    RangeSearch* search = opaque;

    if (reference->target >= search->start && reference->target < search->end) {
        add_found(&search->found, from, reference->kind);
    }
}

// A function is only ever called at its start; a global is also read at its fields, so all of it counts.
static uint32_t end_of_target(const GameImage* image, uint32_t target) {
    uint32_t start = 0;
    uint32_t size = 0;

    if (game_image_is_code(image, target) || !symbols_extent_at(target, &start, &size) || start != target || size == 0) {
        return target + 1;
    }

    return target + size;
}


int command_refs(const wchar_t* exe, const wchar_t* written) {
    GameImage image;
    char name[PLACE_CAPACITY];
    uint32_t target = 0;

    WideCharToMultiByte(CP_UTF8, 0, written, -1, name, sizeof name, NULL, NULL);

    if (!load(exe, &image)) {
        return 1;
    }

    if (!place_find(name, &target)) {
        game_image_free(&image);

        return 1;
    }

    RangeSearch search = { .start = target, .end = end_of_target(&image, target) };
    search.found.items = malloc(MAX_PLACES * sizeof *search.found.items);

    if (search.found.items != NULL) {
        disassembly_each_reference(&image, found_in_range, &search);
    }

    place_name(target, name, sizeof name);
    printf("%s    0x%llx\n", name, PREFERRED_BASE + target);
    print_found(&search.found, "  ");
    printf("%d found\n", search.found.total);

    free(search.found.items);
    game_image_free(&image);

    return search.found.total > 0 ? 0 : 1;
}

static bool is_text_character(uint8_t character) {
    return (character >= 32 && character < 127) || character == '\n' || character == '\t';
}

static bool contains_all(const uint8_t* text, size_t length, int term_count, char terms[][256]) {
    char lowered[1024];
    size_t kept = length < sizeof lowered - 1 ? length : sizeof lowered - 1;

    for (size_t index = 0; index < kept; index++) {
        lowered[index] = (char)tolower(text[index]);
    }

    lowered[kept] = '\0';

    for (int index = 0; index < term_count; index++) {
        if (strstr(lowered, terms[index]) == NULL) {
            return false;
        }
    }

    return true;
}

static int find_texts(const GameImage* image, int term_count, char terms[][256], Text* texts) {
    int count = 0;

    for (int index = 0; index < image->section_count; index++) {
        const ImageSection* section = &image->sections[index];

        if (section->is_code || section->file_size == 0) {
            continue;
        }

        const uint8_t* bytes = image->file + section->file_offset;
        uint32_t size = section->size < section->file_size ? section->size : section->file_size;
        uint32_t start = 0;

        for (uint32_t at = 0; at < size; at++) {
            if (is_text_character(bytes[at])) {
                continue;
            }

            bool ends_text = bytes[at] == '\0' && at - start >= 2;

            if (ends_text && contains_all(bytes + start, at - start, term_count, terms)) {
                if (count < MAX_TEXTS) {
                    texts[count] = (Text){ section->rva + start, section->rva + at, { 0 } };
                }

                count++;
            }

            start = at + 1;
        }
    }

    return count;
}

static int compare_texts(const void* left, const void* right) {
    uint32_t left_start = ((const Text*)left)->start;
    uint32_t right_start = ((const Text*)right)->start;

    return left_start < right_start ? -1 : left_start > right_start;
}

static void found_in_texts(uint32_t from, const Reference* reference, void* opaque) {
    TextSearch* search = opaque;
    int low = 0;
    int high = search->count;

    while (low < high) {
        int middle = (low + high) / 2;

        if (search->texts[middle].end <= reference->target) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    // The linker keeps one copy of a text that ends another, so code can point into the middle of a longer one.
    if (low < search->count && reference->target >= search->texts[low].start) {
        add_found(&search->texts[low].found, from, reference->kind);
    }
}


int command_text(const wchar_t* exe, int term_count, wchar_t** terms) {
    GameImage image;
    char lowered_terms[MAX_TERMS][256];

    if (term_count > MAX_TERMS || !load(exe, &image)) {
        return 1;
    }

    for (int index = 0; index < term_count; index++) {
        WideCharToMultiByte(CP_UTF8, 0, terms[index], -1, lowered_terms[index], sizeof lowered_terms[index], NULL, NULL);

        for (char* character = lowered_terms[index]; *character != '\0'; character++) {
            *character = (char)tolower((unsigned char)*character);
        }
    }

    TextSearch search = { calloc(MAX_TEXTS, sizeof(Text)), 0 };
    int matched = search.texts != NULL ? find_texts(&image, term_count, lowered_terms, search.texts) : 0;
    search.count = matched < MAX_TEXTS ? matched : MAX_TEXTS;

    for (int index = 0; index < search.count; index++) {
        search.texts[index].found.items = malloc(MAX_PLACES * sizeof(Found));
    }

    qsort(search.texts, (size_t)search.count, sizeof(Text), compare_texts);

    if (search.count > 0) {
        disassembly_each_reference(&image, found_in_texts, &search);
    }

    for (int index = 0; index < search.count; index++) {
        Text* text = &search.texts[index];
        char content[240];
        char quoted[500];

        game_image_text_at(&image, text->start, content, sizeof content);
        place_quote_text(content, quoted, sizeof quoted);
        printf("%s    0x%llx\n", quoted, PREFERRED_BASE + text->start);

        if (text->found.total == 0) {
            printf("  (nothing points at it directly)\n");
        }

        print_found(&text->found, "  ");
        free(text->found.items);
    }

    if (matched > MAX_TEXTS) {
        printf("... %d more texts. Add more words to narrow it down\n", matched - MAX_TEXTS);
    }

    printf("%d texts found\n", matched);

    free(search.texts);
    game_image_free(&image);

    return matched > 0 ? 0 : 1;
}
