#include <windows.h>

#include "../include/mt2loader_host.h"
#include "../include/mt2loader_version.h"
#include "game_build.h"
#include "plugins.h"
#include "text_check.h"

static Mt2LoaderHost host;


static void log_game_build(const GameBuild* build) {
    if (game_build_is_known(build)) {
        host.log("Game build %s (linked %08x, image %u bytes)", build->name, build->link_timestamp, build->image_size);

        return;
    }

    host.log(
        "Game build not recognized (linked %08x, image %u bytes): the game was probably updated, so every feature stays disabled until the loader is checked against it",
        build->link_timestamp,
        build->image_size
    );
}

static void log_text_check(void) {
    TextCheckResult result;
    ULONGLONG start_tick = GetTickCount64();

    if (!text_check_run(host.exe_module, host.exe_path, &result)) {
        host.log("Code check skipped: %s", result.problem);

        return;
    }

    ULONGLONG elapsed = GetTickCount64() - start_tick;

    if (result.different_bytes == 0) {
        host.log(
            "Code in memory matches MT2.exe on disk (%zu bytes, %zu relocated, %llu ms)",
            result.compared_bytes,
            result.relocated_bytes,
            elapsed
        );

        return;
    }

    host.log(
        "Code in memory differs from MT2.exe on disk: %zu of %zu bytes, first at RVA %08x. Something else changed the game's code",
        result.different_bytes,
        result.compared_bytes,
        result.first_difference_rva
    );
}


__declspec(dllexport) int mt2loader_start(const Mt2LoaderHost* received_host) {
    if (received_host == NULL || received_host->size < sizeof host || received_host->abi != MT2LOADER_HOST_ABI) {
        return MT2LOADER_HOST_MISMATCH;
    }

    host = *received_host;
    host.log("MT2 Loader core %s started (proxy %s)", MT2LOADER_VERSION, host.proxy_version);

    GameBuild build = game_build_identify(host.exe_module);

    log_game_build(&build);
    log_text_check();
    plugins_prepare(&host, &build);

    return MT2LOADER_STARTED;
}
