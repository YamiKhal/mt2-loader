#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "commands.h"
#include "enums.h"
#include "game_exe.h"
#include "game_image.h"
#include "places.h"


static void print_enum(const GameEnum* game_enum) {
    printf("enum %s\n", game_enum->name);

    for (int index = 0; index < game_enum->count; index++) {
        printf("    %3d  %s\n", index, game_enum->values[index][0] != '\0' ? game_enum->values[index] : "?");
    }
}


int command_enum(const wchar_t* exe, const wchar_t* written) {
    wchar_t path[GAME_PATH_CAPACITY];
    char name[PLACE_CAPACITY] = "";
    GameImage image;
    GameEnums enums;

    if (written != NULL) {
        WideCharToMultiByte(CP_UTF8, 0, written, -1, name, sizeof name, NULL, NULL);
    }

    if (!game_exe_load_symbols(exe, path) || !game_image_load(path, &image)) {
        return 1;
    }

    if (!enums_read(&image, &enums)) {
        fprintf(stderr, "No enum words were found in the game\n");
        game_image_free(&image);

        return 1;
    }

    const GameEnum* found = name[0] != '\0' ? enums_find(&enums, name) : NULL;

    if (found != NULL) {
        print_enum(found);
    } else {
        int shown = 0;

        for (int index = 0; index < enums.count; index++) {
            if (name[0] == '\0' || strstr(enums.items[index].name, name) != NULL) {
                printf("%-50s %d values\n", enums.items[index].name, enums.items[index].count);
                shown++;
            }
        }

        printf("%d enums. See one's values with: mt2sdk enum <name>\n", shown);
    }

    enums_free(&enums);
    game_image_free(&image);

    return 0;
}
