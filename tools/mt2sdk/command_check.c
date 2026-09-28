#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../../src/core/game_build.h"
#include "../../src/core/manifest.h"
#include "commands.h"
#include "game_exe.h"

#define MAX_MANIFEST (256 * 1024)

typedef struct CheckCount {
    int problems;
    int notes;
} CheckCount;


static void problem(CheckCount* count, const char* format, const char* detail) {
    printf("  problem: ");
    printf(format, detail);
    printf("\n");
    count->problems++;
}

static void note(CheckCount* count, const char* format, const char* detail) {
    printf("  note: ");
    printf(format, detail);
    printf("\n");
    count->notes++;
}

static char* read_manifest(const wchar_t* folder) {
    wchar_t path[GAME_PATH_CAPACITY];
    swprintf(path, GAME_PATH_CAPACITY, L"%ls\\manifest.json", folder);

    FILE* file = _wfopen(path, L"rb");

    if (file == NULL) {
        return NULL;
    }

    char* text = malloc(MAX_MANIFEST + 1);
    size_t length = text != NULL ? fread(text, 1, MAX_MANIFEST, file) : 0;
    fclose(file);

    if (text != NULL) {
        text[length] = '\0';
    }

    return text;
}

static bool is_64_bit_dll(const wchar_t* path) {
    FILE* file = _wfopen(path, L"rb");
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    bool ok = file != NULL && fread(&dos, sizeof dos, 1, file) == 1 && dos.e_magic == IMAGE_DOS_SIGNATURE;

    ok = ok && fseek(file, dos.e_lfanew, SEEK_SET) == 0 && fread(&nt, sizeof nt, 1, file) == 1;
    ok = ok && nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64;
    ok = ok && (nt.FileHeader.Characteristics & IMAGE_FILE_DLL) != 0;

    if (file != NULL) {
        fclose(file);
    }

    return ok;
}

static void check_plugin(const wchar_t* folder, const char* rel, CheckCount* count) {
    wchar_t rel_wide[MANIFEST_PATH_CAPACITY];
    wchar_t path[GAME_PATH_CAPACITY];

    MultiByteToWideChar(CP_UTF8, 0, rel, -1, rel_wide, MANIFEST_PATH_CAPACITY);
    swprintf(path, GAME_PATH_CAPACITY, L"%ls\\%ls", folder, rel_wide);

    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        problem(count, "%s isn't there. Build the plugin first; its DLL goes in mod\\native", rel);

        return;
    }

    if (!is_64_bit_dll(path)) {
        problem(count, "%s isn't a 64-bit Windows DLL", rel);

        return;
    }

    // Mapped without running its DllMain or loading what it imports.
    HMODULE module = LoadLibraryExW(path, NULL, DONT_RESOLVE_DLL_REFERENCES);
    bool has_init = module != NULL && GetProcAddress(module, "plugin_init") != NULL;

    if (module != NULL) {
        FreeLibrary(module);
    }

    if (!has_init) {
        problem(count, "%s doesn't export plugin_init. Declare it with PLUGIN_EXPORT, as mt2loader.h shows", rel);

        return;
    }

    printf("  ok: %s\n", rel);
}

static void check_build(const ModManifest* manifest, const wchar_t* exe, CheckCount* count) {
    wchar_t path[GAME_PATH_CAPACITY];

    if (!game_exe_find(exe, path)) {
        return;
    }

    HMODULE module = LoadLibraryExW(path, NULL, DONT_RESOLVE_DLL_REFERENCES);
    GameBuild build = module != NULL ? game_build_identify(module) : (GameBuild){ 0, 0, NULL };

    if (module != NULL) {
        FreeLibrary(module);
    }

    if (!game_build_is_known(&build)) {
        note(count, "%s", "your game build isn't known to this SDK, so the loader won't start plugins on it until it's updated");

        return;
    }

    for (int index = 0; index < manifest->build_count; index++) {
        if (strcmp(manifest->builds[index], build.name) == 0) {
            printf("  ok: made for your game build, %s\n", build.name);

            return;
        }
    }

    note(count, "game_builds doesn't list your game build (%s), so the loader won't start the plugins on it", build.name);
}


int command_check(const wchar_t* mod_folder, const wchar_t* exe) {
    CheckCount count = { 0, 0 };
    char* text = read_manifest(mod_folder);

    wprintf(L"%ls\n", mod_folder);

    if (text == NULL) {
        printf("  problem: no manifest.json. The mod folder is the one with manifest.json in it (mod\\ in a plugin project)\n");

        return 1;
    }

    ModManifest manifest;
    ManifestResult result = manifest_parse(text, &manifest);
    free(text);

    if (result == MANIFEST_INVALID) {
        problem(&count, "%s", manifest.problem);
    }

    if (result == MANIFEST_NO_PLUGINS) {
        note(&count, "%s", "manifest.json lists no loader.plugins, so the loader leaves this mod to the game");
    }

    for (int index = 0; result == MANIFEST_HAS_PLUGINS && index < manifest.plugin_count; index++) {
        check_plugin(mod_folder, manifest.plugins[index], &count);
    }

    if (result == MANIFEST_HAS_PLUGINS) {
        check_build(&manifest, exe, &count);
    }

    printf("%d problem(s), %d note(s)\n", count.problems, count.notes);

    return count.problems > 0 ? 1 : 0;
}
