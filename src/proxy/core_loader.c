#include "core_loader.h"

#include "../include/mt2loader_host.h"
#include "../include/mt2loader_version.h"
#include "log.h"

#define CORE_FILE_NAME L"mt2loader.dll"
#define CORE_START_NAME "mt2loader_start"

static Mt2LoaderHost host;


static bool safe_mode_requested(const SessionHistory* history) {
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        log_line("Shift is held: safe launch, the loader's features stay disabled for this session");

        return true;
    }

    if (history->crash_streak >= SESSION_CRASHES_BEFORE_SAFE_MODE) {
        log_line(
            "The game crashed %d times in a row with the loader running: safe launch, the loader's features stay disabled for this session. "
            "They are enabled again on the next launch",
            history->crash_streak
        );

        return true;
    }

    return false;
}

static void fill_host(const ProxyPaths* paths) {
    host.size = sizeof host;
    host.abi = MT2LOADER_HOST_ABI;
    host.proxy_version = MT2LOADER_VERSION;
    host.game_dir = paths->game_dir;
    host.loader_dir = paths->loader_dir;
    host.exe_path = paths->exe_path;
    host.exe_module = GetModuleHandleW(NULL);
    host.log = log_line;
}


void core_loader_start(const ProxyPaths* paths, const SessionHistory* history) {
    if (safe_mode_requested(history)) {
        return;
    }

    wchar_t core_path[PATH_CAPACITY];

    if (!paths_join(core_path, paths->loader_dir, CORE_FILE_NAME)) {
        log_line("The game folder's path is too long for the loader: the game runs without it");

        return;
    }

    HMODULE core = LoadLibraryExW(core_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);

    if (core == NULL) {
        log_line("mt2loader\\mt2loader.dll couldn't be loaded (error %lu): the game runs without the loader", GetLastError());

        return;
    }

    Mt2LoaderStartFunction start = (Mt2LoaderStartFunction)(void*)GetProcAddress(core, CORE_START_NAME);

    if (start == NULL) {
        log_line("mt2loader\\mt2loader.dll has no %s: it isn't a loader core, the game runs without it", CORE_START_NAME);
        FreeLibrary(core);

        return;
    }

    fill_host(paths);
    session_mark_core_loaded();

    int result = start(&host);

    if (result != MT2LOADER_STARTED) {
        log_line("The loader core refused to start (code %d): the game runs without it", result);
    }
}
