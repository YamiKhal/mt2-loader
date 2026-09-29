#include "game_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../../src/common/pe_image.h"

#define MAX_IMAGE_SECTIONS (sizeof ((GameImage*)0)->sections / sizeof ((GameImage*)0)->sections[0])
#define MAX_TEXT_LENGTH 65536


static uint8_t* read_file(const wchar_t* path, size_t* size) {
    FILE* file = _wfopen(path, L"rb");

    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint8_t* bytes = length > 0 ? malloc((size_t)length) : NULL;
    bool complete = bytes != NULL && fread(bytes, 1, (size_t)length, file) == (size_t)length;
    fclose(file);

    if (!complete) {
        free(bytes);

        return NULL;
    }

    *size = (size_t)length;

    return bytes;
}

static bool read_sections(GameImage* image) {
    IMAGE_NT_HEADERS64* headers = pe_nt_headers((HMODULE)image->file);

    if (headers == NULL) {
        return false;
    }

    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(headers);
    int count = headers->FileHeader.NumberOfSections;

    for (int index = 0; index < count && index < (int)MAX_IMAGE_SECTIONS; index++, section++) {
        ImageSection* kept = &image->sections[image->section_count++];

        memcpy(kept->name, section->Name, IMAGE_SIZEOF_SHORT_NAME);
        kept->name[IMAGE_SIZEOF_SHORT_NAME] = '\0';
        kept->rva = section->VirtualAddress;
        kept->size = section->Misc.VirtualSize;
        kept->file_offset = section->PointerToRawData;
        kept->file_size = section->SizeOfRawData;
        kept->is_code = (section->Characteristics & IMAGE_SCN_CNT_CODE) != 0;
    }

    return image->section_count > 0;
}


bool game_image_load(const wchar_t* path, GameImage* image) {
    memset(image, 0, sizeof *image);
    image->file = read_file(path, &image->file_size);

    if (image->file == NULL) {
        fwprintf(stderr, L"%ls couldn't be read\n", path);

        return false;
    }

    if (!read_sections(image)) {
        fwprintf(stderr, L"%ls isn't a 64-bit Windows program\n", path);
        game_image_free(image);

        return false;
    }

    return true;
}

void game_image_free(GameImage* image) {
    free(image->file);
    memset(image, 0, sizeof *image);
}

const ImageSection* game_image_section_of(const GameImage* image, uint32_t rva) {
    for (int index = 0; index < image->section_count; index++) {
        const ImageSection* section = &image->sections[index];
        uint32_t size = section->size > section->file_size ? section->size : section->file_size;

        if (rva >= section->rva && rva - section->rva < size) {
            return section;
        }
    }

    return NULL;
}

const ImageSection* game_image_section(const GameImage* image, const char* name) {
    for (int index = 0; index < image->section_count; index++) {
        if (strcmp(image->sections[index].name, name) == 0) {
            return &image->sections[index];
        }
    }

    return NULL;
}

const uint8_t* game_image_at(const GameImage* image, uint32_t rva, size_t size) {
    const ImageSection* section = game_image_section_of(image, rva);

    if (section == NULL) {
        return NULL;
    }

    uint32_t inside = rva - section->rva;
    bool in_file = inside + size <= section->file_size && section->file_offset + inside + size <= image->file_size;

    return in_file ? image->file + section->file_offset + inside : NULL;
}

bool game_image_is_code(const GameImage* image, uint32_t rva) {
    const ImageSection* section = game_image_section_of(image, rva);

    return section != NULL && section->is_code;
}

bool game_image_rva_of_pointer(const GameImage* image, uint64_t pointer, uint32_t* rva) {
    if (pointer < PREFERRED_BASE || pointer - PREFERRED_BASE > UINT32_MAX) {
        return false;
    }

    *rva = (uint32_t)(pointer - PREFERRED_BASE);

    return game_image_section_of(image, *rva) != NULL;
}

bool game_image_text_at(const GameImage* image, uint32_t rva, char* text, size_t capacity) {
    const ImageSection* section = game_image_section_of(image, rva);

    if (section == NULL || section->is_code) {
        return false;
    }

    // A text longer than capacity is cut short with "...", but must still end in a NUL to count.
    size_t length = 0;
    size_t kept = 0;
    const uint8_t* character = game_image_at(image, rva, 1);

    while (character != NULL && *character != '\0') {
        bool readable = (*character >= 32 && *character < 127) || *character == '\n' || *character == '\t';

        if (!readable || length > MAX_TEXT_LENGTH) {
            return false;
        }

        if (kept + 4 < capacity) {
            text[kept++] = (char)*character;
        }

        length++;
        character = game_image_at(image, rva + (uint32_t)length, 1);
    }

    if (kept < length) {
        memcpy(text + kept, "...", 3);
        kept += 3;
    }

    text[kept] = '\0';

    // One letter counts only right after another text's end, where a text of its own starts.
    const uint8_t* before = rva > 0 ? game_image_at(image, rva - 1, 1) : NULL;
    size_t shortest = before != NULL && *before == '\0' ? 1 : 2;

    return character != NULL && length >= shortest;
}
