#include "plugin_api.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../common/pe_image.h"
#include "../include/mt2loader_version.h"
#include "before_stub.h"
#include "code.h"
#include "hooks.h"
#include "memory.h"
#include "mod_settings.h"
#include "near_memory.h"
#include "saved_data.h"
#include "shared.h"
#include "symbols.h"

#define MESSAGE_CAPACITY 1600
#define MAX_TEXT_COMPARED 256
#define LABEL_CAPACITY (SYMBOL_NAME_CAPACITY + 16)

static const Mt2LoaderHost* loader_host;
static GameBuild game_build;
static uint8_t* game_base;
static size_t game_size;


static bool rva_of(const void* address, uint32_t* rva) {
    const uint8_t* byte = address;

    if (byte < game_base || byte >= game_base + game_size) {
        return false;
    }

    *rva = (uint32_t)(byte - game_base);

    return true;
}

// "mmoCharacter::EquipWeaponModel(bool)+0x53", or the bare address outside the game's image.
static void describe_address(const void* address, char* text, size_t size) {
    uint32_t rva = 0;
    uint32_t offset = 0;
    char name[SYMBOL_NAME_CAPACITY];

    if (!rva_of(address, &rva) || !symbols_name_at(rva, name, sizeof name, &offset)) {
        snprintf(text, size, "%p", address);

        return;
    }

    if (offset == 0) {
        snprintf(text, size, "%s", name);
    } else {
        snprintf(text, size, "%s+0x%x", name, offset);
    }
}


static void api_log(const PluginApi* api, const char* format, ...) {
    char message[MESSAGE_CAPACITY];
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(message, sizeof message, format, arguments);
    va_end(arguments);

    loader_host->log("[%s] %s", api->mod_id, message);
}

// Readable names are worked out on the first lookup that needs them, which takes a moment, so it's logged.
static void prepare_readable_names(void) {
    if (!symbols_loaded() || symbols_readable_names_ready()) {
        return;
    }

    ULONGLONG started = GetTickCount64();
    symbols_prepare_readable_names();
    loader_host->log("Readable names of the game's symbols ready (%llu ms)", GetTickCount64() - started);
}

static void* api_find(const PluginApi* api, const char* name) {
    prepare_readable_names();

    SymbolMatch match = symbols_find(name);

    switch (match.result) {
        case SYMBOL_FOUND:
            return game_base + match.rva;

        case SYMBOL_AMBIGUOUS:
            api_log(api, "'%s' matches %d game symbols: %s. Add the parameters to pick one", name, match.candidate_count, match.candidates);
            break;

        case SYMBOL_MISSING:
            api_log(api, "No game symbol is named '%s'. Search the names with: mt2sdk find <part of the name>", name != NULL ? name : "");
            break;

        case SYMBOL_TABLE_MISSING:
            api_log(api, "'%s' can't be looked up: the game's symbols couldn't be read (see the start of this log)", name != NULL ? name : "");
            break;
    }

    return NULL;
}

typedef struct NameList {
    char* names;
    size_t size;
    size_t needed;
} NameList;

static void append_found_name(size_t index, const char* readable, void* opaque) {
    NameList* list = opaque;
    size_t length = strlen(readable);
    (void)index;

    if (list->needed + length + 1 < list->size) {
        memcpy(list->names + list->needed, readable, length);
        list->names[list->needed + length] = '\n';
        list->names[list->needed + length + 1] = '\0';
    }

    list->needed += length + 1;
}

static size_t api_find_names(const PluginApi* api, const char* text, char* names, size_t names_size) {
    NameList list = { names, names != NULL ? names_size : 0, 0 };
    (void)api;

    if (list.size > 0) {
        names[0] = '\0';
    }

    symbols_each_containing(text, append_found_name, &list);

    return list.needed + 1;
}

static bool api_name_of(const PluginApi* api, const void* address, char* name, size_t name_size, size_t* offset) {
    uint32_t rva = 0;
    uint32_t symbol_offset = 0;
    (void)api;

    if (name == NULL || name_size == 0 || !rva_of(address, &rva) || !symbols_name_at(rva, name, name_size, &symbol_offset)) {
        return false;
    }

    if (offset != NULL) {
        *offset = symbol_offset;
    }

    return true;
}


static bool add_hook(const PluginApi* api, const char* label, void* target, void* detour, void** original) {
    char problem[HOOK_PROBLEM_CAPACITY];

    if (!hooks_add(api->mod_id, target, detour, original, problem, sizeof problem)) {
        api_log(api, "Couldn't hook %s: %s", label, problem);

        return false;
    }

    int count = hooks_count_on(target);

    if (count > 1) {
        api_log(api, "Hooked %s (%d hooks on it now; this one runs first)", label, count);
    } else {
        api_log(api, "Hooked %s", label);
    }

    return true;
}

static bool api_hook(const PluginApi* api, const char* name, void* detour, void** original) {
    void* target = api_find(api, name);

    if (target == NULL) {
        api_log(api, "Couldn't hook '%s': see the line above", name != NULL ? name : "");

        return false;
    }

    char label[LABEL_CAPACITY];
    describe_address(target, label, sizeof label);

    return add_hook(api, label, target, detour, original);
}

static bool api_hook_address(const PluginApi* api, void* target, void* detour, void** original) {
    char label[LABEL_CAPACITY];
    describe_address(target, label, sizeof label);

    return add_hook(api, label, target, detour, original);
}

static bool api_hook_call(const PluginApi* api, void* call, void* detour, void** original) {
    char label[LABEL_CAPACITY];
    char problem[HOOK_PROBLEM_CAPACITY];
    describe_address(call, label, sizeof label);

    if (!hooks_add_call(api->mod_id, call, detour, original, problem, sizeof problem)) {
        api_log(api, "Couldn't hook the call at %s: %s", label, problem);

        return false;
    }

    int count = hooks_count_on(call);

    if (count > 1) {
        api_log(api, "Hooked the call at %s (%d hooks on it now; this one runs first)", label, count);
    } else {
        api_log(api, "Hooked the call at %s", label);
    }

    return true;
}

static void* api_detour_before(const PluginApi* api, void* callback, void*** original) {
    void* stub = callback != NULL && original != NULL ? before_stub_make(callback, original) : NULL;

    if (stub == NULL) {
        api_log(api, "Couldn't make a detour that runs before a function: no memory for it");
    }

    return stub;
}

static bool api_unhook(const PluginApi* api, void* detour) {
    char problem[HOOK_PROBLEM_CAPACITY];

    if (!hooks_remove(api->mod_id, detour, problem, sizeof problem)) {
        api_log(api, "Couldn't unhook: %s", problem);

        return false;
    }

    api_log(api, "Unhooked one of its hooks");

    return true;
}


static bool api_read(const PluginApi* api, const void* address, void* buffer, size_t size) {
    (void)api;

    return buffer != NULL && memory_read(address, buffer, size);
}

static bool api_write(const PluginApi* api, void* address, const void* bytes, size_t size) {
    char label[LABEL_CAPACITY];
    describe_address(address, label, sizeof label);

    if (bytes == NULL || size == 0 || !memory_write(address, bytes, size)) {
        api_log(api, "Couldn't write %zu bytes at %s", size, label);

        return false;
    }

    api_log(api, "Wrote %zu bytes at %s", size, label);

    return true;
}

static bool parse_or_log(const PluginApi* api, const char* text, Pattern* pattern) {
    if (!pattern_parse(text, pattern)) {
        api_log(api, "'%s' isn't a byte pattern: use hex bytes and ??, like \"48 8B ?? 10\" (at most %d bytes)", text != NULL ? text : "", PATTERN_MAX_BYTES);

        return false;
    }

    return true;
}

static bool api_matches(const PluginApi* api, const void* address, const char* text) {
    Pattern pattern;

    return parse_or_log(api, text, &pattern) && pattern_matches(address, &pattern);
}

static void* api_scan(const PluginApi* api, const char* text) {
    Pattern pattern;

    if (!parse_or_log(api, text, &pattern)) {
        return NULL;
    }

    return pattern_scan(loader_host->exe_module, &pattern);
}


static void* api_scan_range(const PluginApi* api, const void* start, size_t size, const char* text) {
    Pattern pattern;

    if (!parse_or_log(api, text, &pattern)) {
        return NULL;
    }

    return pattern_scan_range(start, size, &pattern);
}

static bool is_target(const uint8_t* target, const void* context) {
    return target == context;
}

// The whole C string at target must be the text, so "humanoid" doesn't match "humanoid_rig".
static bool is_text(const uint8_t* target, const void* context) {
    const char* text = context;
    size_t length = strlen(text);
    char found[MAX_TEXT_COMPARED + 1];

    return length < MAX_TEXT_COMPARED && memory_read(target, found, length + 1) && memcmp(found, text, length + 1) == 0;
}

static void* api_find_reference(const PluginApi* api, const void* start, size_t size, const void* target) {
    (void)api;

    return code_find_reference(start, size, is_target, target);
}

static void* api_find_text_reference(const PluginApi* api, const void* start, size_t size, const char* text) {
    (void)api;

    if (text == NULL || text[0] == '\0') {
        return NULL;
    }

    return code_find_reference(start, size, is_text, text);
}

static size_t api_instruction_length(const PluginApi* api, const void* address) {
    (void)api;

    return code_instruction_length(address);
}

// A call a plugin hooked still reads as a call to the game's function, so other plugins can find it by name.
static bool api_decode(const PluginApi* api, const void* address, Instruction* instruction) {
    void* destination = NULL;
    (void)api;

    if (instruction == NULL || !code_decode(address, instruction)) {
        return false;
    }

    if ((instruction->is_call || instruction->is_jump) && hooks_call_destination(address, &destination)) {
        instruction->reference = destination;
    }

    return true;
}

static bool api_function_extent(const PluginApi* api, const void* address, void** start, size_t* size) {
    uint32_t rva = 0;
    uint32_t start_rva = 0;
    uint32_t extent = 0;
    (void)api;

    if (start == NULL || size == NULL || !rva_of(address, &rva) || !symbols_extent_at(rva, &start_rva, &extent)) {
        return false;
    }

    *start = game_base + start_rva;
    *size = extent;

    return true;
}

static void* api_allocate_near(const PluginApi* api, size_t size) {
    void* memory = near_memory_allocate(game_base, size);

    if (memory == NULL) {
        api_log(api, "Couldn't get %zu bytes of memory near the game's code", size);
    }

    return memory;
}

// The game allocates string buffers with its own operator new, so only its operator delete may free them.
static void api_free_string(const PluginApi* api, GameString* string) {
    static void (*game_delete)(void* pointer);

    if (string == NULL) {
        return;
    }

    if (string->data != string->local && game_delete == NULL) {
        SymbolMatch match = symbols_find("_ZdlPv");
        game_delete = match.result == SYMBOL_FOUND ? (void (*)(void*))(game_base + match.rva) : NULL;
    }

    if (string->data != string->local && game_delete == NULL) {
        api_log(api, "Couldn't free a game string: the game's operator delete wasn't found");

        return;
    }

    if (string->data != string->local) {
        game_delete(string->data);
    }

    string->data = string->local;
    string->length = 0;
    string->local[0] = '\0';
}

static bool api_share(const PluginApi* api, const char* name, void* pointer) {
    const char* taken_by = "";
    ShareResult result = shared_add(api->mod_id, name, pointer, &taken_by);

    switch (result) {
        case SHARE_ADDED:
            api_log(api, "Shared '%s'", name);

            return true;

        case SHARE_NAME_INVALID:
            api_log(api, "Couldn't share: a name of 1 to %d characters is needed", SHARED_NAME_CAPACITY - 1);
            break;

        case SHARE_NAME_TAKEN:
            api_log(api, "Couldn't share '%s': %s already shares that name", name, taken_by);
            break;

        case SHARE_FULL:
            api_log(api, "Couldn't share '%s': too many shared names", name);
            break;
    }

    return false;
}

static void* api_shared(const PluginApi* api, const char* name) {
    (void)api;

    return shared_get(name);
}


void plugin_api_setup(const Mt2LoaderHost* host, const GameBuild* build) {
    loader_host = host;
    game_build = *build;
    game_base = (uint8_t*)host->exe_module;
    game_size = pe_nt_headers(host->exe_module)->OptionalHeader.SizeOfImage;
}

// Read when the first plugin is about to start, so launches without plugins don't pay for it.
void plugin_api_load_symbols(void) {
    static bool attempted = false;

    if (attempted) {
        return;
    }

    attempted = true;

    char problem[200];
    ULONGLONG started = GetTickCount64();

    if (!symbols_load(loader_host->exe_path, problem, sizeof problem)) {
        loader_host->log("Game symbols not available: %s. Plugins can't find game functions by name", problem);

        return;
    }

    loader_host->log("Game symbols: %zu read from MT2.exe (%llu ms)", symbols_count(), GetTickCount64() - started);
}

static bool api_setting(const PluginApi* api, const char* key, char* kind, size_t kind_size, char* value, size_t value_size) {
    char problem[256];

    if (!mod_settings_get(api->mod_folder, key, kind, kind_size, value, value_size, problem, sizeof problem)) {
        api_log(api, "No setting '%s': %s", key != NULL ? key : "", problem);

        return false;
    }

    return true;
}

static bool valid_saved_key(const char* key) {
    size_t length = key != NULL ? strlen(key) : 0;

    if (length == 0 || length > SAVED_KEY_CAPACITY) {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        char character = key[index];
        bool allowed = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');

        if (!allowed && character != '_' && character != '-' && character != '.') {
            return false;
        }
    }

    return true;
}

// Each plugin's keys are stored as "<mod id>/<key>", so plugins never see or change each other's values.
static bool own_key(const PluginApi* api, const char* key, char* full_key, size_t full_key_size) {
    if (!valid_saved_key(key)) {
        api_log(api, "'%s' can't be a key for a saved value: use 1 to %d characters of A-Z a-z 0-9 _ - .", key != NULL ? key : "", SAVED_KEY_CAPACITY);

        return false;
    }

    snprintf(full_key, full_key_size, "%s/%s", api->mod_id, key);

    return true;
}

static bool api_saved_set(const PluginApi* api, const void* object, const char* key, const char* value) {
    char full_key[SAVED_KEY_CAPACITY * 2];
    char problem[256];

    if (!own_key(api, key, full_key, sizeof full_key)) {
        return false;
    }

    if (!saved_data_set(object, full_key, value, problem, sizeof problem)) {
        api_log(api, "Couldn't keep '%s' in the saved game: %s", key, problem);

        return false;
    }

    return true;
}

static bool api_saved_get(const PluginApi* api, const void* object, const char* key, char* value, size_t value_size, size_t* length) {
    char full_key[SAVED_KEY_CAPACITY * 2];

    return own_key(api, key, full_key, sizeof full_key) && saved_data_get(object, full_key, value, value_size, length);
}

static bool api_saved_set_link(const PluginApi* api, const void* object, const char* key, const void* target) {
    char full_key[SAVED_KEY_CAPACITY * 2];
    char problem[256];

    if (!own_key(api, key, full_key, sizeof full_key)) {
        return false;
    }

    if (!saved_data_set_link(object, full_key, target, problem, sizeof problem)) {
        api_log(api, "Couldn't keep the link '%s' in the saved game: %s", key, problem);

        return false;
    }

    return true;
}

static void* api_saved_get_link(const PluginApi* api, const void* object, const char* key) {
    char full_key[SAVED_KEY_CAPACITY * 2];

    return own_key(api, key, full_key, sizeof full_key) ? (void*)saved_data_get_link(object, full_key) : NULL;
}

static size_t api_saved_keys(const PluginApi* api, const void* object, char* keys, size_t keys_size) {
    char prefix[SAVED_KEY_CAPACITY * 2];
    snprintf(prefix, sizeof prefix, "%s/", api->mod_id);

    return saved_data_keys(object, prefix, keys, keys_size);
}

static void* api_saved_root(const PluginApi* api) {
    (void)api;

    return saved_data_root();
}

static void log_saved_data(const char* format, ...) {
    char message[MESSAGE_CAPACITY];
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(message, sizeof message, format, arguments);
    va_end(arguments);

    loader_host->log("%s", message);
}

// Once plugins run: a save loaded later still has their values, even if the plugin that wrote them is gone.
void plugin_api_start_saved_data(void) {
    char problem[256];

    if (!symbols_loaded()) {
        return;
    }

    if (!saved_data_start(game_base, game_size, log_saved_data, problem, sizeof problem)) {
        loader_host->log("Plugin values in saved games aren't available: %s", problem);
    }
}

void plugin_api_fill(PluginApi* api, const char* mod_id, const wchar_t* mod_folder) {
    *api = (PluginApi){
        .size = sizeof *api,
        .version = LOADER_API_VERSION,
        .loader_version = MT2LOADER_VERSION,
        .game_build = game_build.name,
        .mod_id = mod_id,
        .mod_folder = mod_folder,
        .game_base = game_base,
        .log = api_log,
        .find = api_find,
        .name_of = api_name_of,
        .hook = api_hook,
        .hook_address = api_hook_address,
        .unhook = api_unhook,
        .read = api_read,
        .write = api_write,
        .matches = api_matches,
        .scan = api_scan,
        .share = api_share,
        .shared = api_shared,
        .scan_range = api_scan_range,
        .find_reference = api_find_reference,
        .find_text_reference = api_find_text_reference,
        .instruction_length = api_instruction_length,
        .free_string = api_free_string,
        .decode = api_decode,
        .function_extent = api_function_extent,
        .allocate_near = api_allocate_near,
        .hook_call = api_hook_call,
        .detour_before = api_detour_before,
        .setting = api_setting,
        .saved_set = api_saved_set,
        .saved_get = api_saved_get,
        .saved_keys = api_saved_keys,
        .saved_root = api_saved_root,
        .saved_set_link = api_saved_set_link,
        .saved_get_link = api_saved_get_link,
        .find_names = api_find_names,
    };
}
