/*
    MT2 Loader plugin API in C: the raw layer underneath mt2loader.hpp.

    Plugins are written in C++ with mt2loader.hpp, which wraps this file. This file is for plugins written in plain
    C, and for tools in other languages; C++ code gets an error here unless it defines MT2LOADER_RAW_API first.

    A plugin is a 64-bit Windows DLL that exports plugin_init. The loader calls it once, early in startup
    (while the game looks for its mods, before any game system exists), with a PluginApi that stays valid
    until the game exits. Keep the pointer: every service is called through it.

        #include "mt2loader.h"

        PLUGIN_EXPORT int plugin_init(const PluginApi* api) {
            api->log(api, "Hello from %s", api->mod_id);

            return PLUGIN_OK;
        }

    Works with any C or C++ compiler that makes 64-bit Windows DLLs (Visual Studio, MinGW-w64, clang).
    Only plain C types cross between plugin and loader, so the plugin may use any C runtime.

    Full guide: LOADER_MODDING.md
    MIT license, Copyright (c) 2026 YamiKhal: use it in any plugin, open or closed.
*/
#ifndef MT2LOADER_H
#define MT2LOADER_H

#if defined(__cplusplus) && !defined(MT2LOADER_WRAPPER) && !defined(MT2LOADER_RAW_API)
#error "In C++, include <mt2loader.hpp> instead: it wraps this C file. (For the raw C API anyway, define MT2LOADER_RAW_API first.)"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

// The API this header describes. Fields are only ever added at the end; `size` says which ones a loader has.
#define LOADER_API_VERSION 1

#define PLUGIN_OK 0

#ifdef __cplusplus
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PLUGIN_EXPORT __declspec(dllexport)
#endif

typedef struct PluginApi PluginApi;

/*
    The game's std::string, laid out as the game's compiler (GCC) makes it: 32 bytes. Many game functions
    take or return one. game_string_text reads one. game_string_view wraps your text so it can be passed
    to a game function that takes `const std::string&`; the game can read and copy it, and nothing else.
    A game function that returns a std::string by value fills one you provide; free it with api->free_string.
*/
typedef struct GameString {
    char* data;
    size_t length;
    union {
        char local[16];
        size_t capacity;
    };
} GameString;

static inline const char* game_string_text(const GameString* string) {
    return string->data;
}

static inline GameString game_string_view(const char* text) {
    GameString string;
    size_t length = 0;

    while (text[length] != '\0') {
        length++;
    }

    string.data = (char*)text;
    string.length = length;
    string.capacity = length;

    return string;
}


// One decoded instruction (loader 0.6.0, api->decode). Offsets count from the instruction's first byte.
typedef struct Instruction {
    uint32_t length;
    // A number written in the instruction (cmp eax, 8), sign-extended: where it is, and its size (0 when none).
    uint32_t immediate_offset;
    uint32_t immediate_size;
    int64_t immediate;
    // What the instruction refers to: a rip-relative operand (a global, a text), or where a call or jump goes.
    // NULL when nothing. reference_offset and reference_size locate the relative offset inside the instruction.
    void* reference;
    uint32_t reference_offset;
    uint32_t reference_size;
    bool is_call;
    bool is_jump;
    bool is_return;
    // add or sub on the stack pointer: a function making room for its locals, not a number the game uses.
    bool adjusts_stack;
} Instruction;


struct PluginApi {
    // Size of this struct as the running loader knows it. A field exists if its offset is below size.
    uint32_t size;
    // LOADER_API_VERSION of the running loader.
    uint32_t version;

    const char* loader_version;
    // The game build, as named in manifest.json's "game_builds", like "0.30.7".
    const char* game_build;
    const char* mod_id;
    // Where the mod's folder is, for reading its own files.
    const wchar_t* mod_folder;
    // Where MT2.exe is loaded. Game addresses move every launch; find() already adds this.
    void* game_base;

    // Writes one line to the loader's log (mt2loader\loader.log), prefixed with the mod id. printf formatting.
    void (*log)(const PluginApi* api, const char* format, ...);

    /*
        Game symbols: every function and global of MT2.exe, by name. Returns its address in the running game,
        or NULL (and logs why) when there's no such symbol or the name is ambiguous.

        Names can be written three ways:
            "mmoCharacter::EquipWeaponModel"          readable, without parameters (must match only one symbol)
            "mmoCharacter::EquipWeaponModel(bool)"    readable, with parameters (for overloaded functions)
            "_ZN12mmoCharacter16EquipWeaponModelEb"   the raw (mangled) name
        Spaces don't matter, and std::string can stand for std::__cxx11::basic_string<char, ...>.
        mt2sdk find <text> lists the names.
    */
    void* (*find)(const PluginApi* api, const char* name);

    // Readable name of the symbol that contains address, plus the offset into it. Returns false if none.
    bool (*name_of)(const PluginApi* api, const void* address, char* name, size_t name_size, size_t* offset);

    /*
        Hooks: replace a game function with your own. Your function gets the same arguments (for a method,
        the object comes first) and returns the same type. Call *original to run the game's function
        from yours, or skip it. Several plugins can hook one function: the last hook is called first,
        and each one's original leads to the one before it.

            static int (*original_equip)(void* character, bool show);

            static int my_equip(void* character, bool show) {
                return original_equip(character, true);
            }

            api->hook(api, "mmoCharacter::EquipWeaponModel", (void*)my_equip, (void**)&original_equip);

        hook takes a name as find() does; hook_address takes an address. Both return false and log why
        if it can't be done. Always call through *original: the loader updates it when hooks change.
    */
    bool (*hook)(const PluginApi* api, const char* name, void* detour, void** original);
    bool (*hook_address)(const PluginApi* api, void* target, void* detour, void** original);
    // Takes one of your hooks out again. Other plugins' hooks on the same function keep working.
    bool (*unhook)(const PluginApi* api, void* detour);

    /*
        Memory. Patterns are hex bytes with ?? for any byte: "48 8B ?? 10 E8".
        read never crashes on a bad address: it returns false. write also works on code (it lifts the
        page protection for the write) and logs each patch.
    */
    bool (*read)(const PluginApi* api, const void* address, void* buffer, size_t size);
    bool (*write)(const PluginApi* api, void* address, const void* bytes, size_t size);
    bool (*matches)(const PluginApi* api, const void* address, const char* pattern);
    // First place in the game's code (.text) that matches pattern, or NULL.
    void* (*scan)(const PluginApi* api, const char* pattern);

    /*
        Sharing between plugins: publish a pointer (a function, a struct of functions, data) under a name,
        so other plugins can use it. Names are global: prefix them with your mod id, like "wings.api".
        Share in plugin_init; look up in plugin_ready, when every plugin has shared what it offers.
    */
    bool (*share)(const PluginApi* api, const char* name, void* pointer);
    void* (*shared)(const PluginApi* api, const char* name);

    // ---- Added in loader 0.4.0. Check that a field exists first: API_HAS(api, scan_range). ----

    /*
        Code navigation. Game code is full of references: `lea rdx, "humanoid"` before a string compare,
        a call to another function, a jump. These find them inside a function, so a plugin can locate the
        instruction to check or patch by what it does, not by an offset that moves between game builds.
        start must be the start of an instruction (a function's address is).

            unsigned char* equip = api->find(api, "mmoCharacter::EquipWeaponModel");
            unsigned char* compare = api->find_text_reference(api, equip, 0x200, "humanoid");
    */
    // Like scan, within [start, start + size).
    void* (*scan_range)(const PluginApi* api, const void* start, size_t size, const char* pattern);
    // First instruction that refers to target: a rip-relative operand, call, jump or conditional jump.
    void* (*find_reference)(const PluginApi* api, const void* start, size_t size, const void* target);
    // First instruction whose rip-relative operand points at the text `text` (a whole C string).
    void* (*find_text_reference)(const PluginApi* api, const void* start, size_t size, const char* text);
    // Length in bytes of the instruction at address, or 0 if it isn't readable code.
    size_t (*instruction_length)(const PluginApi* api, const void* address);

    // Frees what a game function returned in a GameString (by value), with the game's own allocator.
    void (*free_string)(const PluginApi* api, GameString* string);

    // ---- Added in loader 0.6.0: what mt2loader.hpp's game::in(...) is built on. ----

    // One instruction, decoded. Returns false if address isn't readable code.
    bool (*decode)(const PluginApi* api, const void* address, Instruction* instruction);

    // Start and size of the function (or global) that contains address, from the game's symbol table.
    bool (*function_extent)(const PluginApi* api, const void* address, void** start, size_t* size);

    // Memory within 2 GB of the game's code, readable, writable and executable, never freed. An instruction's
    // 32-bit offsets can reach it: new text for a patched instruction, or code.
    void* (*allocate_near)(const PluginApi* api, size_t size);

    /*
        Hooks one call instruction (E8 call or E9 jump to a function) instead of a whole function: only that call
        goes to detour, which takes the called function's arguments and returns its result. *original is what the
        call went to before (the function, or an earlier hook on this call). unhook takes it out, as for hook.
    */
    bool (*hook_call)(const PluginApi* api, void* call, void* detour, void** original);

    /*
        A detour for hook or hook_call that calls callback with the hooked function's arguments, then continues to
        *original with the same arguments, returning whatever it returns: code that runs before a function
        without knowing what the function returns. callback is void, with the function's parameters. The
        detour's original is filled in by hook or hook_call through *original.
    */
    void* (*detour_before)(const PluginApi* api, void* callback, void*** original);

    // ---- Added in loader 0.7.0 ----

    /*
        A setting the mod declares in its config.json (MT2 Mod Manager's settings format), with the value the
        player picked in the manager (settings.json, which the manager writes next to the plugin), or its default.
        kind gets "int", "float", "bool", "string", "choice" or "color" (as "#RRGGBB"); value gets the value as text ("12", "0.5", "true").
        Returns false, and logs why, if the mod declares no such setting.
    */
    bool (*setting)(const PluginApi* api, const char* key, char* kind, size_t kind_size, char* value, size_t value_size);

    // ---- Added in loader 0.8.0: the plugin's own values in saved games. ----

    /*
        Values the plugin keeps on a game object (a mmoNPC*, the game state from saved_root, ...). They're written
        into the saved game with the object and read back when it loads. Each plugin sees only its own keys
        (1-64 characters of A-Z a-z 0-9 _ - .). The game skips them when it loads without the loader, and they go
        away with the object.

        saved_set stores value (NULL removes the key) and returns false, logging why, if it can't be kept.
        saved_get copies the value when value_size is enough and sets *length either way; false if there's none.
        saved_keys writes the object's keys, one per line, and returns the size they need (with the final '\0').
        saved_root is the object every saved game starts from (the game state), or NULL outside a game.
    */
    bool (*saved_set)(const PluginApi* api, const void* object, const char* key, const char* value);
    bool (*saved_get)(const PluginApi* api, const void* object, const char* key, char* value, size_t value_size, size_t* length);
    size_t (*saved_keys)(const PluginApi* api, const void* object, char* keys, size_t keys_size);
    void* (*saved_root)(const PluginApi* api);

    // ---- Added in loader 0.10.0. ----

    /*
        The readable names of the game's functions and globals that contain text, one per line, each one find takes.
        Returns the size they need (with the final '\0'); names is filled when names_size is enough.
    */
    size_t (*find_names)(const PluginApi* api, const char* text, char* names, size_t names_size);

    // ---- Added in loader 0.12.0. ----

    /*
        A link from one game object to another (a quest to the buildings it sends players to), kept with the first
        in saved games as the game keeps its own links: saved as the id the save gives the other object, and turned
        back into that object once a load has read every object. A link to an object that's destroyed goes away.
        The key is shared with saved_set's: setting a text value there replaces the link, and the other way round.

        saved_set_link keeps target under key (NULL removes it) and returns false, logging why, if it can't.
        saved_get_link is the object, NULL if there's no link (or the key holds text).
    */
    bool (*saved_set_link)(const PluginApi* api, const void* object, const char* key, const void* target);
    void* (*saved_get_link)(const PluginApi* api, const void* object, const char* key);

    // ---- Added in loader 0.13.0. ----

    /*
        A field the game doesn't name, by the name mt2-mappings gives it ("mmoCharacterType::colors"): its offset and
        C++ type ("int", "?" when unknown). The loader checks a place the mapping names still uses the field there.
        False otherwise, with problem saying why, or empty when mt2-mappings doesn't have the field.
    */
    bool (*mapped_field)(const PluginApi* api, const char* name, size_t* offset, char* type, size_t type_size, char* problem, size_t problem_size);

    // How many bytes the game allocates for an object of the class ("mmoActor"), read from its code; 0 if unknown.
    size_t (*class_size)(const PluginApi* api, const char* class_name);
};

// True when the running loader has this field of PluginApi (a newer header than the loader is fine to use).
#define API_HAS(api, field) ((api)->size >= offsetof(PluginApi, field) + sizeof((api)->field))

// What a plugin exports. Return PLUGIN_OK, or any other number to have it logged as a problem.
typedef int (*PluginInitFunction)(const PluginApi* api);

// Optional export, PLUGIN_EXPORT void plugin_ready(const PluginApi* api): called once every plugin's
// plugin_init has run, still before the game loads its mods.
typedef void (*PluginReadyFunction)(const PluginApi* api);

#endif
