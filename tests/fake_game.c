#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <zlib.h>

__declspec(dllimport) bool SDL_Init(uint32_t flags);
__declspec(dllimport) void SDL_Quit(void);
__declspec(dllimport) int fake_sdl_init_calls(void);
__declspec(dllimport) int fake_sdl_quit_calls(void);
__declspec(dllimport) int PHYSFS_setWriteDir(const char* directory);
__declspec(dllimport) const char* PHYSFS_getBaseDir(void);

void fake_write_results(const char* path);

enum ExitCode {
    EXIT_OK = 0,
    EXIT_ZLIB_FAILED = 10,
    EXIT_SDL_INIT_FAILED = 11,
    EXIT_SDL_CALLS_LOST = 12,
    EXIT_CRASH_SIMULATED = 13,
    EXIT_PHYSFS_FAILED = 14,
};


static bool zlib_round_trip(void) {
    const char* text = "MT2 Loader test: zlib must still work through the proxy.";
    uLong text_length = (uLong)strlen(text) + 1;
    Bytef compressed[256];
    uLongf compressed_length = sizeof compressed;
    char restored[256];
    uLongf restored_length = sizeof restored;

    if (compress(compressed, &compressed_length, (const Bytef*)text, text_length) != Z_OK) {
        return false;
    }

    if (uncompress((Bytef*)restored, &restored_length, compressed, compressed_length) != Z_OK) {
        return false;
    }

    return restored_length == text_length && strcmp(text, restored) == 0;
}

// Like vsSystem::InitPhysFS: the profile folder becomes the write folder, then the base folder is asked for.
// The profile folder is "profile" next to the exe, or MT2_FAKE_WRITE_DIR.
static bool init_physfs(void) {
    char write_directory[1024];
    const char* configured = getenv("MT2_FAKE_WRITE_DIR");

    if (configured != NULL) {
        snprintf(write_directory, sizeof write_directory, "%s/", configured);
    } else {
        GetModuleFileNameA(NULL, write_directory, sizeof write_directory);
        strcpy(strrchr(write_directory, '\\') + 1, "profile/");
    }

    return PHYSFS_setWriteDir(write_directory) && PHYSFS_getBaseDir() != NULL;
}


int main(int argc, char** argv) {
    bool crash = argc > 1 && strcmp(argv[1], "--crash") == 0;
    bool wait = argc > 1 && strcmp(argv[1], "--wait") == 0;

    if (!zlib_round_trip()) {
        return EXIT_ZLIB_FAILED;
    }

    if (!SDL_Init(0x20) || !SDL_Init(0x20)) {
        return EXIT_SDL_INIT_FAILED;
    }

    if (!init_physfs()) {
        return EXIT_PHYSFS_FAILED;
    }

    if (wait) {
        Sleep(15000);
    }

    if (crash) {
        TerminateProcess(GetCurrentProcess(), EXIT_CRASH_SIMULATED);
    }

    char results[1024];
    GetModuleFileNameA(NULL, results, sizeof results);
    strcpy(strrchr(results, '\\') + 1, "results.txt");
    fake_write_results(results);

    SDL_Quit();

    if (fake_sdl_init_calls() != 2 || fake_sdl_quit_calls() != 1) {
        return EXIT_SDL_CALLS_LOST;
    }

    return EXIT_OK;
}
