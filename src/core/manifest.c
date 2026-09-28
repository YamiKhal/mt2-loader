#include "manifest.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CJSON_HIDE_SYMBOLS
#include <cjson/cJSON.h>


static ManifestResult invalid(ModManifest* manifest, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(manifest->problem, sizeof manifest->problem, format, arguments);
    va_end(arguments);

    return MANIFEST_INVALID;
}

// The same rule as the mod manager's: 2-24 characters of a-z, 0-9 and _, starting with a letter.
static bool is_valid_id(const char* id) {
    size_t length = strlen(id);

    if (length < 2 || length > 24 || !islower((unsigned char)id[0]) || id[length - 1] == '_' || strstr(id, "__") != NULL) {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        char character = id[index];

        if (!islower((unsigned char)character) && !isdigit((unsigned char)character) && character != '_') {
            return false;
        }
    }

    return true;
}

static bool is_valid_build(const char* build) {
    size_t length = strlen(build);

    if (length == 0 || length >= MANIFEST_ID_CAPACITY) {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        char character = build[index];

        if (!isalnum((unsigned char)character) && strchr(".-_", character) == NULL) {
            return false;
        }
    }

    return true;
}

// A path inside the mod folder: no drive, no leading separator, no "." or ".." parts, and a .dll at the end.
static bool is_safe_plugin_path(const char* path) {
    size_t length = strlen(path);

    if (length < 5 || length >= MANIFEST_PATH_CAPACITY || strchr(path, ':') != NULL || _stricmp(path + length - 4, ".dll") != 0) {
        return false;
    }

    const char* part = path;

    while (true) {
        size_t part_length = strcspn(part, "/\\");

        if (part_length == 0 || (part_length == 1 && part[0] == '.') || (part_length == 2 && part[0] == '.' && part[1] == '.')) {
            return false;
        }

        if (part[part_length] == '\0') {
            return true;
        }

        part += part_length + 1;
    }
}


static ManifestResult read_plugins(const cJSON* loader, ModManifest* manifest) {
    const cJSON* plugins = cJSON_GetObjectItemCaseSensitive(loader, "plugins");

    if (plugins == NULL || (cJSON_IsArray(plugins) && cJSON_GetArraySize(plugins) == 0)) {
        return MANIFEST_NO_PLUGINS;
    }

    if (!cJSON_IsArray(plugins) || cJSON_GetArraySize(plugins) > MANIFEST_MAX_PLUGINS) {
        return invalid(manifest, "loader.plugins must be a list of at most %d paths", MANIFEST_MAX_PLUGINS);
    }

    const cJSON* plugin;

    cJSON_ArrayForEach(plugin, plugins) {
        if (!cJSON_IsString(plugin) || !is_safe_plugin_path(plugin->valuestring)) {
            return invalid(manifest, "loader.plugins has an entry that isn't a .dll path inside the mod");
        }

        strcpy(manifest->plugins[manifest->plugin_count++], plugin->valuestring);
    }

    return MANIFEST_HAS_PLUGINS;
}

static ManifestResult read_builds(const cJSON* loader, ModManifest* manifest) {
    const cJSON* builds = cJSON_GetObjectItemCaseSensitive(loader, "game_builds");

    if (!cJSON_IsArray(builds) || cJSON_GetArraySize(builds) == 0 || cJSON_GetArraySize(builds) > MANIFEST_MAX_BUILDS) {
        return invalid(manifest, "loader.game_builds must list the game builds the plugins were made for, like [\"0.30.7\"]");
    }

    const cJSON* build;

    cJSON_ArrayForEach(build, builds) {
        if (!cJSON_IsString(build) || !is_valid_build(build->valuestring)) {
            return invalid(manifest, "loader.game_builds has an entry that isn't a game build name");
        }

        strcpy(manifest->builds[manifest->build_count++], build->valuestring);
    }

    return MANIFEST_HAS_PLUGINS;
}


ManifestResult manifest_parse(const char* text, ModManifest* manifest) {
    memset(manifest, 0, sizeof *manifest);

    cJSON* root = cJSON_Parse(text);

    if (root == NULL) {
        return invalid(manifest, "manifest.json isn't valid JSON");
    }

    ManifestResult result = MANIFEST_NO_PLUGINS;
    const cJSON* loader = cJSON_GetObjectItemCaseSensitive(root, "loader");
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "id");

    if (cJSON_IsObject(loader)) {
        result = read_plugins(loader, manifest);
    } else if (loader != NULL) {
        result = invalid(manifest, "\"loader\" must be an object");
    }

    if (result == MANIFEST_HAS_PLUGINS) {
        result = read_builds(loader, manifest);
    }

    if (result == MANIFEST_HAS_PLUGINS && (!cJSON_IsString(id) || !is_valid_id(id->valuestring))) {
        result = invalid(manifest, "\"id\" must be 2-24 characters of a-z, 0-9 and _, starting with a letter");
    }

    if (result == MANIFEST_HAS_PLUGINS) {
        strcpy(manifest->id, id->valuestring);
    }

    cJSON_Delete(root);

    return result;
}
