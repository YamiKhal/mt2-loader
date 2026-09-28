#include "startup.h"

#include <stdbool.h>
#include <stdint.h>

#include "../include/mt2loader_host.h"
#include "../include/mt2loader_version.h"
#include "core_loader.h"
#include "../common/import_patch.h"
#include "log.h"
#include "session.h"

#define SDL_DLL_NAME "SDL3.dll"

typedef bool (*SdlInitFunction)(uint32_t flags);
typedef void (*SdlQuitFunction)(void);

static ProxyPaths proxy_paths;
static SessionHistory session_history;
static bool proxy_active = false;
static ULONGLONG attach_tick = 0;
static void** sdl_init_slot = NULL;
static void** sdl_quit_slot = NULL;
static SdlInitFunction real_sdl_init = NULL;
static SdlQuitFunction real_sdl_quit = NULL;
static volatile LONG core_started = 0;


static bool redirected_sdl_init(uint32_t flags) {
    if (InterlockedCompareExchange(&core_started, 1, 0) == 0) {
        import_replace(sdl_init_slot, (void*)real_sdl_init);

        log_line(
            "SDL_Init called on thread %lu, %llu ms after the proxy was attached: the game is past its DRM stub",
            GetCurrentThreadId(),
            GetTickCount64() - attach_tick
        );

        core_loader_start(&proxy_paths, &session_history);
    }

    return real_sdl_init(flags);
}

static void redirected_sdl_quit(void) {
    log_line("SDL_Quit called: the game is shutting down normally");
    session_mark_clean_exit();

    real_sdl_quit();
}


static bool redirect_import(const char* function_name, void* replacement, void*** slot, void** original) {
    HMODULE exe_module = GetModuleHandleW(NULL);
    *slot = import_find_slot(exe_module, SDL_DLL_NAME, function_name);

    if (*slot == NULL) {
        log_line("%s isn't in MT2.exe's import table: the loader stays inactive", function_name);

        return false;
    }

    if (!import_slot_is_filled(*slot, SDL_DLL_NAME)) {
        log_line("%s's import slot doesn't point into %s yet: the loader stays inactive", function_name, SDL_DLL_NAME);

        return false;
    }

    *original = **slot;

    if (!import_replace(*slot, replacement)) {
        log_line("%s's import slot couldn't be made writable (error %lu): the loader stays inactive", function_name, GetLastError());

        return false;
    }

    return true;
}

static bool redirect_sdl(void) {
    void* original_init = NULL;
    void* original_quit = NULL;

    if (!redirect_import("SDL_Quit", (void*)redirected_sdl_quit, &sdl_quit_slot, &original_quit)) {
        return false;
    }

    real_sdl_quit = (SdlQuitFunction)original_quit;

    if (!redirect_import("SDL_Init", (void*)redirected_sdl_init, &sdl_init_slot, &original_init)) {
        import_replace(sdl_quit_slot, original_quit);
        sdl_quit_slot = NULL;

        return false;
    }

    real_sdl_init = (SdlInitFunction)original_init;

    return true;
}


static void log_previous_session(void) {
    if (!session_history.previous_session_crashed) {
        return;
    }

    if (!session_history.previous_session_had_core) {
        log_line("The previous session didn't exit cleanly, but the loader's code wasn't running in it");

        return;
    }

    log_line("The previous session didn't exit cleanly (%d in a row with the loader running)", session_history.crash_streak);
}


void startup_attach(HMODULE proxy_module) {
    attach_tick = GetTickCount64();

    if (!paths_find(proxy_module, &proxy_paths) || !paths_host_is_game(&proxy_paths)) {
        return;
    }

    // Without the loader folder (removed, or never installed) the proxy only forwards to zlib.
    if (!paths_is_folder(proxy_paths.loader_dir) || !log_open(proxy_paths.loader_dir)) {
        return;
    }

    log_line("MT2 Loader proxy %s attached on thread %lu", MT2LOADER_VERSION, GetCurrentThreadId());

    wchar_t disabled_flag[PATH_CAPACITY];

    if (paths_join(disabled_flag, proxy_paths.loader_dir, L"disabled") && paths_exists(disabled_flag)) {
        log_line("The loader is disabled (mt2loader\\disabled exists): the game runs without it");
        log_close();

        return;
    }

    session_begin(proxy_paths.loader_dir, &session_history);
    log_previous_session();

    if (!redirect_sdl()) {
        session_mark_clean_exit();

        return;
    }

    proxy_active = true;
    log_line("Waiting for the game to call SDL_Init");
}

void startup_detach(void) {
    if (proxy_active) {
        log_line("Process is exiting");
    }

    log_close();
}
