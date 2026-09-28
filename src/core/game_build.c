#include "game_build.h"

#include <stddef.h>

#include "../common/pe_image.h"

typedef struct KnownBuild {
    uint32_t link_timestamp;
    uint32_t image_size;
    const char* name;
} KnownBuild;

// Newest first.
static const KnownBuild known_builds[] = {
    { 0x6a918d95, 30986240, "0.30.7" },
#ifdef MT2LOADER_TEST_IMAGE_SIZE
    // Only in the test build of the core (tests/run_tests.sh): the fake MT2.exe, linked without a timestamp.
    { 0, MT2LOADER_TEST_IMAGE_SIZE, "test" },
#endif
};


GameBuild game_build_identify(HMODULE exe_module) {
    GameBuild build = { 0, 0, NULL };
    IMAGE_NT_HEADERS64* nt_headers = pe_nt_headers(exe_module);

    if (nt_headers == NULL) {
        return build;
    }

    build.link_timestamp = nt_headers->FileHeader.TimeDateStamp;
    build.image_size = nt_headers->OptionalHeader.SizeOfImage;

    for (size_t index = 0; index < sizeof known_builds / sizeof known_builds[0]; index++) {
        const KnownBuild* known = &known_builds[index];

        if (known->link_timestamp == build.link_timestamp && known->image_size == build.image_size) {
            build.name = known->name;
        }
    }

    return build;
}

const char* game_build_newest_name(void) {
    return known_builds[0].name;
}

bool game_build_is_known(const GameBuild* build) {
    return build->name != NULL;
}
