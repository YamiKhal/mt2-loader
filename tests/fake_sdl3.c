#include <stdbool.h>
#include <stdint.h>

static int init_calls = 0;
static int quit_calls = 0;


__declspec(dllexport) bool SDL_Init(uint32_t flags) {
    (void)flags;
    init_calls++;

    return true;
}

__declspec(dllexport) bool SDL_InitSubSystem(uint32_t flags) {
    (void)flags;

    return true;
}

__declspec(dllexport) void SDL_Quit(void) {
    quit_calls++;
}

__declspec(dllexport) int fake_sdl_init_calls(void) {
    return init_calls;
}

__declspec(dllexport) int fake_sdl_quit_calls(void) {
    return quit_calls;
}
