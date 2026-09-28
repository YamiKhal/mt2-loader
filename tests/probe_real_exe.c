#include <stdio.h>
#include <windows.h>

#include "../src/core/game_build.h"
#include "../src/core/text_check.h"
#include "../src/common/import_patch.h"


static int report_slot(HMODULE exe, const char* dll_name, const char* function_name) {
    void** slot = import_find_slot(exe, dll_name, function_name);

    if (slot == NULL) {
        printf("%s: not found\n", function_name);

        return 1;
    }

    printf("%s: import slot at RVA %08llx\n", function_name, (unsigned long long)((BYTE*)slot - (BYTE*)exe));

    return 0;
}


int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        fwprintf(stderr, L"usage: probe_real_exe <MT2.exe>\n");

        return 2;
    }

    // Mapped as an image without resolving imports or running any of its code.
    HMODULE exe = LoadLibraryExW(argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES);

    if (exe == NULL) {
        printf("could not map the exe (error %lu)\n", GetLastError());

        return 2;
    }

    int failures = report_slot(exe, "SDL3.dll", "SDL_Init") + report_slot(exe, "SDL3.dll", "SDL_Quit")
        + report_slot(exe, "libphysfs.dll", "PHYSFS_getBaseDir");

    GameBuild build = game_build_identify(exe);
    printf("build: %s (linked %08x, image %u bytes)\n", build.name ? build.name : "unknown", build.link_timestamp, build.image_size);

    TextCheckResult result;

    if (text_check_run(exe, argv[1], &result)) {
        printf("code check: %zu bytes, %zu relocated, %zu different\n", result.compared_bytes, result.relocated_bytes, result.different_bytes);
    } else {
        printf("code check failed: %s\n", result.problem);
        failures++;
    }

    return failures;
}
