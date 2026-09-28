// Runs a plugin's plugin_init against the real MT2.exe, mapped into this process without running any of it.
// The plugin gets the core's real PluginApi, so its lookups and checks run on the real game code; what it
// writes lands in this process's copy only. Then, for each RVA given, prints the bytes there, and for a call
// instruction, where it goes now: a call a plugin hooked leads into the plugin, and "call:<rva>" also calls it
// the way the game would (with no arguments: only for hooks that ignore them) and prints what it returns.
// "saves:<class>" says whether plugins can keep values on objects of that class in saved games.
//
//     probe_plugin_real_exe <MT2.exe> <plugin.dll> <mod id> [rva | call:rva | saves:class ...]
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../src/core/game_build.h"
#include "../src/core/plugin_api.h"
#include "../src/core/saved_data.h"
#include "../src/core/symbols.h"

#define PRINTED_BYTES 4
#define OPCODE_CALL 0xe8
#define STUB_SLOT_OFFSET 8
#define VTABLE_ADDRESS_POINT 16


static void print_log(const char* format, ...) {
    va_list arguments;

    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    printf("\n");
}

// A hooked call goes to a stub near the game (jmp [slot]); the slot holds the newest hook.
static void* hooked_destination(const unsigned char* call) {
    int32_t offset = 0;
    memcpy(&offset, call + 1, sizeof offset);

    const unsigned char* stub = call + 5 + offset;

    if (stub[0] != 0xff || stub[1] != 0x25) {
        return (void*)stub;
    }

    return *(void* const*)(stub + STUB_SLOT_OFFSET);
}

static void print_destination(HMODULE plugin, const unsigned char* call, bool call_it) {
    void* destination = hooked_destination(call);
    MEMORY_BASIC_INFORMATION information;
    bool in_plugin = VirtualQuery(destination, &information, sizeof information) != 0 && information.AllocationBase == (void*)plugin;

    printf(" -> %s", in_plugin ? "the plugin" : "not the plugin");

    if (call_it && in_plugin) {
        bool (*hook)(void) = (bool (*)(void))destination;
        printf(", returns %d", hook() ? 1 : 0);
    }
}

// An object of the class as far as saving goes: just its vtable, where the class's objects point to it.
static void print_saves(HMODULE exe, const wchar_t* class_name) {
    char name[256];
    char vtable_name[300];
    char problem[256] = "";

    WideCharToMultiByte(CP_UTF8, 0, class_name, -1, name, sizeof name, NULL, NULL);
    snprintf(vtable_name, sizeof vtable_name, "vtable for %s", name);

    SymbolMatch vtable = symbols_find(vtable_name);

    if (vtable.result != SYMBOL_FOUND) {
        printf("saves %s: no %s\n", name, vtable_name);

        return;
    }

    void* object[1] = { (uint8_t*)exe + vtable.rva + VTABLE_ADDRESS_POINT };
    bool kept = saved_data_set(object, "probe/check", "1", problem, sizeof problem);

    saved_data_set(object, "probe/check", NULL, problem, sizeof problem);
    printf("saves %s: %s\n", name, kept ? "yes" : "no");
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        fwprintf(stderr, L"usage: probe_plugin_real_exe <MT2.exe> <plugin.dll> <mod id> [rva ...]\n");

        return 2;
    }

    HMODULE exe = LoadLibraryExW(argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES);
    HMODULE plugin = LoadLibraryW(argv[2]);

    if (exe == NULL || plugin == NULL) {
        printf("couldn't map the exe or load the plugin (error %lu)\n", GetLastError());

        return 2;
    }

    char mod_id[64];
    WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, mod_id, sizeof mod_id, NULL, NULL);

    Mt2LoaderHost host = {
        .size = sizeof host,
        .abi = MT2LOADER_HOST_ABI,
        .proxy_version = "probe",
        .exe_path = argv[1],
        .exe_module = exe,
        .log = print_log,
    };
    GameBuild build = game_build_identify(exe);
    PluginApi api;

    plugin_api_setup(&host, &build);
    plugin_api_load_symbols();

    // The mod's folder, as the loader gives it: the plugin sits in <mod folder>\native\.
    wchar_t mod_folder[MAX_PATH];
    wcsncpy(mod_folder, argv[2], MAX_PATH - 1);
    mod_folder[MAX_PATH - 1] = L'\0';

    for (int part = 0; part < 2; part++) {
        wchar_t* separator = wcsrchr(mod_folder, L'\\');

        if (separator != NULL) {
            *separator = L'\0';
        }
    }

    plugin_api_fill(&api, mod_id, mod_folder);

    PluginInitFunction init = (PluginInitFunction)(void*)GetProcAddress(plugin, "plugin_init");
    int result = init != NULL ? init(&api) : -1;

    printf("plugin_init returned %d\n", result);
    plugin_api_start_saved_data();

    for (int index = 4; index < argc; index++) {
        if (wcsncmp(argv[index], L"saves:", 6) == 0) {
            print_saves(exe, argv[index] + 6);

            continue;
        }

        bool call_it = wcsncmp(argv[index], L"call:", 5) == 0;
        unsigned long rva = wcstoul(argv[index] + (call_it ? 5 : 0), NULL, 16);
        const unsigned char* bytes = (const unsigned char*)exe + rva;

        printf("rva %08lx:", rva);

        for (int offset = 0; offset < PRINTED_BYTES; offset++) {
            printf(" %02x", bytes[offset]);
        }

        if (bytes[0] == OPCODE_CALL) {
            print_destination(plugin, bytes, call_it);
        }

        printf("\n");
    }

    return result == 0 ? 0 : 1;
}
