#include <stdio.h>
#include <windows.h>

#include "commands.h"
#include "game_exe.h"
#include "game_image.h"
#include "program_json.h"


int command_program(const wchar_t* exe, const wchar_t* output) {
    wchar_t path[GAME_PATH_CAPACITY];
    char build[64];
    GameImage image;

    if (!game_exe_load_symbols(exe, path) || !game_image_load(path, &image)) {
        return 1;
    }

    game_exe_build_name(path, build, sizeof build);

    bool written = program_json_write(&image, build, output);
    game_image_free(&image);

    return written ? 0 : 1;
}
