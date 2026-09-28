#include <windows.h>

#include "../include/mt2loader_version.h"
#include "startup.h"

__attribute__((used)) static const char version_marker[] = MT2LOADER_VERSION_MARKER;


__declspec(dllexport) const char* mt2loader_proxy_version(void) {
    return MT2LOADER_VERSION;
}


BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        startup_attach(instance);
    }

    if (reason == DLL_PROCESS_DETACH) {
        startup_detach();
    }

    return TRUE;
}
