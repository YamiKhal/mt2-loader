#include "mod_settings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <windows.h>

#define CJSON_HIDE_SYMBOLS
#include <cjson/cJSON.h>

#define MAX_MODS 256
#define FOLDER_CAPACITY 1024
#define MAX_FILE_SIZE (1024 * 1024)

// A mod's two files, read once.
typedef struct ModFiles {
    wchar_t folder[FOLDER_CAPACITY];
    cJSON* config;
    cJSON* values;
    char problem[200];
} ModFiles;

static ModFiles mods[MAX_MODS];
static int mod_count;
static SRWLOCK lock = SRWLOCK_INIT;


static char* read_text(const wchar_t* folder, const wchar_t* name) {
    wchar_t path[FOLDER_CAPACITY + 32];
    swprintf(path, sizeof path / sizeof path[0], L"%ls\\%ls", folder, name);

    FILE* file = _wfopen(path, L"rb");

    if (file == NULL) {
        return NULL;
    }

    char* text = malloc(MAX_FILE_SIZE + 1);
    size_t length = text != NULL ? fread(text, 1, MAX_FILE_SIZE, file) : 0;
    fclose(file);

    if (text != NULL) {
        text[length] = '\0';
    }

    return text;
}

static void read_mod(ModFiles* mod) {
    char* config_text = read_text(mod->folder, L"config.json");
    char* values_text = read_text(mod->folder, L"settings.json");

    if (config_text == NULL) {
        snprintf(mod->problem, sizeof mod->problem, "the mod has no config.json, so it declares no settings");
    } else {
        mod->config = cJSON_Parse(config_text);

        if (!cJSON_IsArray(mod->config)) {
            snprintf(mod->problem, sizeof mod->problem, "the mod's config.json isn't a list of settings (MT2 Mod Manager's Check shows why)");
        }
    }

    // Written by the mod manager; without it (or if it's broken), every setting has its default.
    if (values_text != NULL) {
        mod->values = cJSON_Parse(values_text);
    }

    free(config_text);
    free(values_text);
}

static ModFiles* files_of(const wchar_t* folder) {
    for (int index = 0; index < mod_count; index++) {
        if (wcscmp(mods[index].folder, folder) == 0) {
            return &mods[index];
        }
    }

    if (mod_count == MAX_MODS || wcslen(folder) >= FOLDER_CAPACITY) {
        return NULL;
    }

    ModFiles* mod = &mods[mod_count++];
    wcscpy(mod->folder, folder);
    read_mod(mod);

    return mod;
}

static const cJSON* declared(const cJSON* config, const char* key) {
    const cJSON* option = NULL;

    cJSON_ArrayForEach(option, config) {
        const cJSON* option_key = cJSON_GetObjectItemCaseSensitive(option, "key");

        if (cJSON_IsString(option_key) && strcmp(option_key->valuestring, key) == 0) {
            return option;
        }
    }

    return NULL;
}

static double clamped(const cJSON* option, double number) {
    const cJSON* lowest = cJSON_GetObjectItemCaseSensitive(option, "min");
    const cJSON* highest = cJSON_GetObjectItemCaseSensitive(option, "max");

    if (cJSON_IsNumber(lowest) && number < lowest->valuedouble) {
        number = lowest->valuedouble;
    }

    if (cJSON_IsNumber(highest) && number > highest->valuedouble) {
        number = highest->valuedouble;
    }

    return number;
}

// A JSON value as the text a choice compares: strings as they are, numbers without a needless ".0".
static bool plain_text(const cJSON* value, char* text, size_t size) {
    if (cJSON_IsString(value)) {
        snprintf(text, size, "%s", value->valuestring);
    } else if (cJSON_IsNumber(value) && value->valuedouble == floor(value->valuedouble) && fabs(value->valuedouble) < 1e15) {
        snprintf(text, size, "%lld", (long long)value->valuedouble);
    } else if (cJSON_IsNumber(value)) {
        snprintf(text, size, "%.17g", value->valuedouble);
    } else if (cJSON_IsBool(value)) {
        snprintf(text, size, "%s", cJSON_IsTrue(value) ? "true" : "false");
    } else {
        return false;
    }

    return true;
}

static bool is_choice(const cJSON* option, const char* text) {
    const cJSON* choices = cJSON_GetObjectItemCaseSensitive(option, "options");
    const cJSON* choice = NULL;
    char choice_text[256];

    cJSON_ArrayForEach(choice, choices) {
        const cJSON* value = cJSON_IsObject(choice) ? cJSON_GetObjectItemCaseSensitive(choice, "value") : choice;

        if (plain_text(value, choice_text, sizeof choice_text) && strcmp(choice_text, text) == 0) {
            return true;
        }
    }

    return false;
}

// "#fe0" or "FDFE0C" as "#FDFE0C", the way the mod manager writes colors; false for anything else.
static bool hex_color(const char* text, char* color, size_t size) {
    const char* digits = text[0] == '#' ? text + 1 : text;
    size_t length = strlen(digits);

    if ((length != 3 && length != 6) || size < 8) {
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        if (!isxdigit((unsigned char)digits[i])) {
            return false;
        }
    }

    color[0] = '#';

    for (size_t i = 0; i < 6; i++) {
        char digit = length == 3 ? digits[i / 2] : digits[i];
        color[i + 1] = (char)toupper((unsigned char)digit);
    }

    color[7] = '\0';

    return true;
}

// The setting's value as text, if value fits the kind; false otherwise.
static bool render(const cJSON* option, const char* kind, const cJSON* value, char* text, size_t size) {
    if (value == NULL) {
        return false;
    }

    if (strcmp(kind, "int") == 0 && cJSON_IsNumber(value)) {
        snprintf(text, size, "%lld", (long long)llround(clamped(option, value->valuedouble)));
    } else if (strcmp(kind, "float") == 0 && cJSON_IsNumber(value)) {
        snprintf(text, size, "%.17g", clamped(option, value->valuedouble));
    } else if (strcmp(kind, "bool") == 0 && cJSON_IsBool(value)) {
        snprintf(text, size, "%s", cJSON_IsTrue(value) ? "true" : "false");
    } else if (strcmp(kind, "string") == 0 && cJSON_IsString(value)) {
        snprintf(text, size, "%s", value->valuestring);
    } else if (strcmp(kind, "choice") == 0 && plain_text(value, text, size)) {
        return is_choice(option, text);
    } else if (strcmp(kind, "color") == 0 && cJSON_IsString(value)) {
        return hex_color(value->valuestring, text, size);
    } else {
        return false;
    }

    return true;
}

// What the mod manager uses when config.json gives no default.
static void fallback(const cJSON* option, const char* kind, char* text, size_t size) {
    const cJSON* choices = cJSON_GetObjectItemCaseSensitive(option, "options");
    const cJSON* first = cJSON_GetArrayItem(choices, 0);

    if (strcmp(kind, "int") == 0 || strcmp(kind, "float") == 0) {
        cJSON* zero = cJSON_CreateNumber(0);
        render(option, kind, zero, text, size);
        cJSON_Delete(zero);
    } else if (strcmp(kind, "bool") == 0) {
        snprintf(text, size, "false");
    } else if (strcmp(kind, "color") == 0) {
        snprintf(text, size, "#FFFFFF");
    } else if (strcmp(kind, "choice") == 0 && first != NULL) {
        plain_text(cJSON_IsObject(first) ? cJSON_GetObjectItemCaseSensitive(first, "value") : first, text, size);
    } else {
        text[0] = '\0';
    }
}

static bool get(ModFiles* mod, const char* key, char* kind, size_t kind_size, char* value, size_t value_size, char* problem, size_t problem_size) {
    if (mod->problem[0] != '\0') {
        snprintf(problem, problem_size, "%s", mod->problem);

        return false;
    }

    const cJSON* option = declared(mod->config, key);
    const cJSON* option_kind = cJSON_GetObjectItemCaseSensitive(option, "type");

    if (option == NULL || !cJSON_IsString(option_kind)) {
        snprintf(problem, problem_size, "config.json declares no setting '%s'", key);

        return false;
    }

    snprintf(kind, kind_size, "%s", option_kind->valuestring);

    const cJSON* chosen = cJSON_GetObjectItemCaseSensitive(mod->values, key);
    const cJSON* default_value = cJSON_GetObjectItemCaseSensitive(option, "default");

    if (!render(option, kind, chosen, value, value_size) && !render(option, kind, default_value, value, value_size)) {
        fallback(option, kind, value, value_size);
    }

    return true;
}


bool mod_settings_get(const wchar_t* mod_folder, const char* key, char* kind, size_t kind_size, char* value, size_t value_size,
    char* problem, size_t problem_size) {
    if (mod_folder == NULL || key == NULL || kind == NULL || value == NULL || kind_size == 0 || value_size == 0) {
        snprintf(problem, problem_size, "the key and room for the answer must be given");

        return false;
    }

    AcquireSRWLockExclusive(&lock);

    ModFiles* mod = files_of(mod_folder);
    bool found = mod != NULL && get(mod, key, kind, kind_size, value, value_size, problem, problem_size);

    if (mod == NULL) {
        snprintf(problem, problem_size, "too many mods read settings");
    }

    ReleaseSRWLockExclusive(&lock);

    return found;
}
