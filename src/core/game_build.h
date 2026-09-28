#ifndef CORE_GAME_BUILD_H
#define CORE_GAME_BUILD_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

typedef struct GameBuild {
    uint32_t link_timestamp;
    uint32_t image_size;
    const char* name;
} GameBuild;

GameBuild game_build_identify(HMODULE exe_module);
bool game_build_is_known(const GameBuild* build);
const char* game_build_newest_name(void);

#endif
