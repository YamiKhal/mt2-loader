#include "plugins.h"

#include <shellapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "manifest.h"
#include "physfs_hook.h"
#include "plugin_api.h"

#define PLUGIN_INIT_NAME "plugin_init"
#define PLUGIN_READY_NAME "plugin_ready"
#define MAX_PLUGINS 64
#define MAX_MANIFEST_BYTES (256 * 1024)
#define PATH_CAPACITY 1024

typedef struct LoadedPlugin {
    PluginApi api;
    char mod_id[MANIFEST_ID_CAPACITY];
    char rel[MANIFEST_PATH_CAPACITY];
    wchar_t mod_folder[PATH_CAPACITY];
    PluginReadyFunction ready;
} LoadedPlugin;

static const Mt2LoaderHost* loader_host;
static GameBuild game_build;
static LoadedPlugin loaded_plugins[MAX_PLUGINS];
static int loaded_count = 0;

static bool mods_disabled_on_command_line(void) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool disabled = false;

    for (int index = 0; arguments != NULL && index < count; index++) {
        disabled = disabled || wcscmp(arguments[index], L"--no-mods") == 0;
    }

    LocalFree(arguments);

    return disabled;
}

static char* read_small_file(const wchar_t* path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    LARGE_INTEGER size;
    char* text = NULL;
    DWORD read = 0;

    if (GetFileSizeEx(file, &size) && size.QuadPart < MAX_MANIFEST_BYTES) {
        text = malloc((size_t)size.QuadPart + 1);
    }

    if (text != NULL && ReadFile(file, text, (DWORD)size.QuadPart, &read, NULL) && read == size.QuadPart) {
        text[read] = '\0';
    } else {
        free(text);
        text = NULL;
    }

    CloseHandle(file);

    return text;
}


static bool build_is_listed(const ModManifest* manifest) {
    for (int index = 0; index < manifest->build_count; index++) {
        if (strcmp(manifest->builds[index], game_build.name) == 0) {
            return true;
        }
    }

    return false;
}

static bool already_started(const char* mod_id) {
    for (int index = 0; index < loaded_count; index++) {
        if (strcmp(loaded_plugins[index].mod_id, mod_id) == 0) {
            return true;
        }
    }

    return false;
}

static bool plugin_path(const wchar_t* mod_folder, const char* rel, wchar_t* path) {
    wchar_t rel_wide[MANIFEST_PATH_CAPACITY];
    wchar_t joined[PATH_CAPACITY];

    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rel, -1, rel_wide, MANIFEST_PATH_CAPACITY) == 0) {
        return false;
    }

    for (wchar_t* character = rel_wide; *character != L'\0'; character++) {
        if (*character == L'/') {
            *character = L'\\';
        }
    }

    if (swprintf(joined, PATH_CAPACITY, L"%ls\\%ls", mod_folder, rel_wide) < 0) {
        return false;
    }

    DWORD length = GetFullPathNameW(joined, PATH_CAPACITY, path, NULL);
    size_t folder_length = wcslen(mod_folder);

    // manifest_parse already refuses ".." parts; this also catches anything Windows resolves differently.
    return length > 0 && length < PATH_CAPACITY && _wcsnicmp(path, mod_folder, folder_length) == 0 && path[folder_length] == L'\\';
}


static LoadedPlugin* add_plugin(const ModManifest* manifest, const wchar_t* mod_folder, const char* rel, PluginReadyFunction ready) {
    LoadedPlugin* plugin = &loaded_plugins[loaded_count++];

    strcpy(plugin->mod_id, manifest->id);
    snprintf(plugin->rel, sizeof plugin->rel, "%s", rel);
    wcsncpy(plugin->mod_folder, mod_folder, PATH_CAPACITY - 1);
    plugin->ready = ready;
    plugin_api_fill(&plugin->api, plugin->mod_id, plugin->mod_folder);

    return plugin;
}

static void start_plugin(const ModManifest* manifest, const wchar_t* mod_folder, const char* rel) {
    const char* id = manifest->id;
    wchar_t path[PATH_CAPACITY];

    if (loaded_count == MAX_PLUGINS) {
        loader_host->log("[%s] %s not started: more than %d plugins", id, rel, MAX_PLUGINS);

        return;
    }

    if (!plugin_path(mod_folder, rel, path)) {
        loader_host->log("[%s] %s not started: it isn't a path inside the mod's folder", id, rel);

        return;
    }

    DWORD attributes = GetFileAttributesW(path);

    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        loader_host->log("[%s] %s not started: the file isn't there", id, rel);

        return;
    }

    // Libraries the plugin ships next to itself are found; the game folder and the current folder are not searched.
    HMODULE module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);

    if (module == NULL) {
        loader_host->log("[%s] %s not started: Windows couldn't load it, so a library it needs may be missing (error %lu)", id, rel, GetLastError());

        return;
    }

    PluginInitFunction init = (PluginInitFunction)(void*)GetProcAddress(module, PLUGIN_INIT_NAME);
    PluginReadyFunction ready = (PluginReadyFunction)(void*)GetProcAddress(module, PLUGIN_READY_NAME);

    if (init == NULL) {
        loader_host->log("[%s] %s not started: it has no %s. A plugin gets it from mt2loader.hpp, by defining void plugin::init()", id, rel, PLUGIN_INIT_NAME);
        FreeLibrary(module);

        return;
    }

    LoadedPlugin* plugin = add_plugin(manifest, mod_folder, rel, ready);
    plugin_api_load_symbols();

    // Logged before the call, so a crash inside the plugin leaves its name as the log's last line.
    loader_host->log("[%s] starting %s", id, rel);

    int result = init(&plugin->api);

    if (result != PLUGIN_OK) {
        loader_host->log("[%s] %s reported a problem while starting (code %d)", id, rel, result);
    }
}

static void start_mod(const wchar_t* mod_folder, const wchar_t* folder_name) {
    wchar_t manifest_path[PATH_CAPACITY];

    if (swprintf(manifest_path, PATH_CAPACITY, L"%ls\\manifest.json", mod_folder) < 0) {
        return;
    }

    char* text = read_small_file(manifest_path);

    if (text == NULL) {
        return;
    }

    ModManifest manifest;
    ManifestResult result = manifest_parse(text, &manifest);
    free(text);

    if (result == MANIFEST_INVALID) {
        loader_host->log("Mod folder %ls: %s. Its plugins don't run", folder_name, manifest.problem);

        return;
    }

    if (result == MANIFEST_NO_PLUGINS) {
        return;
    }

    if (already_started(manifest.id)) {
        loader_host->log("[%s] mod folder %ls skipped: a mod with this id already started its plugins (two copies of the mod?)", manifest.id, folder_name);

        return;
    }

    if (!build_is_listed(&manifest)) {
        loader_host->log("[%s] plugins not started: made for game build %s, and this is %s", manifest.id, manifest.builds[0], game_build.name);

        return;
    }

    for (int index = 0; index < manifest.plugin_count; index++) {
        start_plugin(&manifest, mod_folder, manifest.plugins[index]);
    }
}


static bool mods_folder_from(const char* write_directory, wchar_t* mods_folder) {
    wchar_t write_wide[PATH_CAPACITY];

    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, write_directory, -1, write_wide, PATH_CAPACITY) == 0) {
        return false;
    }

    size_t length = wcslen(write_wide);

    for (size_t index = 0; index < length; index++) {
        if (write_wide[index] == L'/') {
            write_wide[index] = L'\\';
        }
    }

    const wchar_t* separator = length > 0 && write_wide[length - 1] == L'\\' ? L"" : L"\\";

    return swprintf(mods_folder, PATH_CAPACITY, L"%ls%lsmod", write_wide, separator) > 0;
}

static void tell_plugins_ready(void) {
    for (int index = 0; index < loaded_count; index++) {
        LoadedPlugin* plugin = &loaded_plugins[index];

        if (plugin->ready == NULL) {
            continue;
        }

        loader_host->log("[%s] %s: plugin_ready", plugin->mod_id, plugin->rel);
        plugin->ready(&plugin->api);
    }
}

static void on_write_directory_ready(const char* write_directory) {
    wchar_t mods_folder[PATH_CAPACITY];

    if (write_directory == NULL || !mods_folder_from(write_directory, mods_folder)) {
        loader_host->log("The game's mods folder couldn't be found: no plugin runs");

        return;
    }

    loader_host->log("Looking for plugins in %ls", mods_folder);

    wchar_t pattern[PATH_CAPACITY];
    WIN32_FIND_DATAW found;
    swprintf(pattern, PATH_CAPACITY, L"%ls\\*", mods_folder);

    HANDLE search = FindFirstFileW(pattern, &found);
    int folders = 0;

    while (search != INVALID_HANDLE_VALUE) {
        bool is_folder = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool is_special = wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0;
        wchar_t mod_folder[PATH_CAPACITY];

        // Every folder in mod\ is an active mod to the game (vsSystem::InitPhysFS), so the same goes for plugins.
        if (is_folder && !is_special && swprintf(mod_folder, PATH_CAPACITY, L"%ls\\%ls", mods_folder, found.cFileName) > 0) {
            folders++;
            start_mod(mod_folder, found.cFileName);
        }

        if (!FindNextFileW(search, &found)) {
            FindClose(search);
            search = INVALID_HANDLE_VALUE;
        }
    }

    loader_host->log("Plugins: %d started, from %d mod folders", loaded_count, folders);

    if (loaded_count > 0) {
        plugin_api_start_saved_data();
    }

    tell_plugins_ready();
}


void plugins_prepare(const Mt2LoaderHost* host, const GameBuild* build) {
    loader_host = host;
    game_build = *build;
    plugin_api_setup(host, build);

    if (!game_build_is_known(build)) {
        host->log("No plugin runs: the game build isn't recognized, so the loader waits for an update that knows it");

        return;
    }

    if (mods_disabled_on_command_line()) {
        host->log("No plugin runs: the game was started with --no-mods");

        return;
    }

    if (!physfs_hook_install(host->exe_module, on_write_directory_ready)) {
        host->log("No plugin runs: MT2.exe doesn't import PHYSFS_getBaseDir as expected");

        return;
    }

    host->log("Plugins start when the game looks for its mods");
}
