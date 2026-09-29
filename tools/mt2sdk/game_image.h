#ifndef MT2SDK_GAME_IMAGE_H
#define MT2SDK_GAME_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

// Where MT2.exe asks to be loaded. Disassemblers (Ghidra, IDA, x64dbg before it starts) show addresses from here.
#define PREFERRED_BASE 0x140000000ull

typedef struct ImageSection {
    char name[9];
    uint32_t rva;
    uint32_t size;
    uint32_t file_offset;
    uint32_t file_size;
    bool is_code;
} ImageSection;

// MT2.exe as it is on disk: absolute pointers in it read as disassemblers show them, and none of its code runs.
typedef struct GameImage {
    uint8_t* file;
    size_t file_size;
    ImageSection sections[96];
    int section_count;
} GameImage;

bool game_image_load(const wchar_t* path, GameImage* image);
void game_image_free(GameImage* image);

// The bytes at rva, if all size of them are in the file (not only in memory, like zeroed data).
const uint8_t* game_image_at(const GameImage* image, uint32_t rva, size_t size);
const ImageSection* game_image_section_of(const GameImage* image, uint32_t rva);
const ImageSection* game_image_section(const GameImage* image, const char* name);

bool game_image_is_code(const GameImage* image, uint32_t rva);

// A pointer as stored in the exe (a preferred-base address) turned into an rva inside the image, or false.
bool game_image_rva_of_pointer(const GameImage* image, uint64_t pointer, uint32_t* rva);

// The text at rva if it's a readable, NUL-terminated string (of two characters or more, or one after a NUL).
bool game_image_text_at(const GameImage* image, uint32_t rva, char* text, size_t capacity);

#endif
