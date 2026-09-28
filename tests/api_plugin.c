// Exercises every PluginApi service against the fake game. Loaded twice, as mods api_a and api_b.
#include <stdio.h>
#include <string.h>

#include <mt2loader.h>

static const PluginApi* loader;
static int shared_value = 1234;
static int (*original_equip)(void* character, bool show);
static int (*original_overloaded)(void* character, int value);


static void check(const char* what, bool ok) {
    loader->log(loader, "check %s: %s", what, ok ? "ok" : "FAILED");
}

static int equip_plus_10(void* character, bool show) {
    return original_equip(character, show) + 10;
}

static int equip_plus_100(void* character, bool show) {
    return original_equip(character, show) + 100;
}

static int overloaded_plus_1000(void* character, int value) {
    return original_overloaded(character, value) + 1000;
}


static void check_find(void) {
    void* raw = loader->find(loader, "_ZN13FakeCharacter16EquipWeaponModelEb");

    check("find raw", raw != NULL);
    check("find readable", loader->find(loader, "FakeCharacter::EquipWeaponModel") == raw);
    check("find with parameters", loader->find(loader, "FakeCharacter::EquipWeaponModel(bool)") == raw);
    check("find ignores spaces", loader->find(loader, "FakeCharacter :: EquipWeaponModel ( bool )") == raw);
    check("find ambiguous", loader->find(loader, "FakeCharacter::Overloaded") == NULL);
    check("find overload", loader->find(loader, "FakeCharacter::Overloaded(int)") != NULL);
    check("find missing", loader->find(loader, "FakeCharacter::Nothing") == NULL);

    char name[256];
    size_t offset = 0;
    bool named = loader->name_of(loader, (char*)raw + 3, name, sizeof name, &offset);

    check("name_of", named && strcmp(name, "FakeCharacter::EquipWeaponModel(bool)") == 0 && offset == 3);
}

static void check_memory(void) {
    int* gold = loader->find(loader, "fake_gold");
    int new_gold = 42;
    int read_back = 0;
    int ignored = 0;

    check("write", gold != NULL && loader->write(loader, gold, &new_gold, sizeof new_gold));
    check("read", loader->read(loader, gold, &read_back, sizeof read_back) && read_back == 42);
    check("read bad address", !loader->read(loader, (void*)0x10, &ignored, sizeof ignored));

    unsigned char* function = loader->find(loader, "FakeCharacter::Overloaded(float)");
    unsigned char bytes[12];
    char pattern[64] = "??";

    loader->read(loader, function, bytes, sizeof bytes);

    for (size_t index = 1; index < sizeof bytes; index++) {
        snprintf(pattern + strlen(pattern), sizeof pattern - strlen(pattern), " %02X", bytes[index]);
    }

    check("matches", loader->matches(loader, function, pattern));
    check("scan", loader->scan(loader, pattern) == function);
    check("bad pattern", !loader->matches(loader, function, "zz"));
}

// The game's calling convention for a method that returns a class by value: result pointer first, then the object.
typedef GameString* (*TitleFunction)(GameString* result, const void* character);
// The same call written as C returns a struct: the C compiler passes the result pointer first by itself.
typedef GameString (*TitleByValue)(const void* character);

static void check_navigation(void) {
    unsigned char* rig_check = loader->find(loader, "FakeCharacter::RigCheck");
    unsigned char* results = loader->find(loader, "fake_write_results");
    void* gold = loader->find(loader, "fake_gold");
    unsigned char* text_use = loader->find_text_reference(loader, rig_check, 0x100, "fake_humanoid");
    char pattern[8];

    snprintf(pattern, sizeof pattern, "%02X", rig_check[0]);

    check("has 0.4.0 fields", API_HAS(loader, free_string));
    check("find_text_reference", text_use != NULL && loader->instruction_length(loader, text_use) == 7);
    check("find_text_reference needs the whole text", loader->find_text_reference(loader, rig_check, 0x100, "fake_human") == NULL);
    check("find_reference", loader->find_reference(loader, results, 0x200, gold) != NULL);
    check("scan_range", loader->scan_range(loader, rig_check, 16, pattern) == rig_check);
    check("scan_range stays in range", loader->scan_range(loader, rig_check + 1, 0, pattern) == NULL);
}

static void check_returned_string(void) {
    TitleFunction title = (TitleFunction)loader->find(loader, "FakeCharacter::Title");
    int character_weapons = 3;
    GameString result;

    check("find a name with [abi:cxx11]", title != NULL);

    if (title == NULL) {
        return;
    }

    GameString* returned = title(&result, &character_weapons);

    check("string returned by value", returned == &result && strcmp(game_string_text(&result), "Fake character with 3 weapons") == 0);
    check("string on the game's heap", result.data != result.local);

    loader->free_string(loader, &result);
    check("free_string", result.data == result.local && result.length == 0);

    GameString by_value = ((TitleByValue)(void*)title)(&character_weapons);

    check("string returned as a C struct", strcmp(game_string_text(&by_value), "Fake character with 3 weapons") == 0);
    loader->free_string(loader, &by_value);
}

static void start_a(void) {
    check_find();
    check_memory();
    check_navigation();
    check_returned_string();

    void* equip = loader->find(loader, "FakeCharacter::EquipWeaponModel");

    check("hook", loader->hook(loader, "FakeCharacter::EquipWeaponModel", (void*)equip_plus_10, (void**)&original_equip));
    check("hooked code starts with a jump", loader->matches(loader, equip, "E9"));
    check("hook missing", !loader->hook(loader, "FakeCharacter::Nothing", (void*)equip_plus_100, (void**)&original_equip));
    check("share", loader->share(loader, "api_a.value", &shared_value));
}

static void start_b(void) {
    check("hook second", loader->hook(loader, "FakeCharacter::EquipWeaponModel(bool)", (void*)equip_plus_100, (void**)&original_equip));
    check("hook other", loader->hook(loader, "FakeCharacter::Overloaded(int)", (void*)overloaded_plus_1000, (void**)&original_overloaded));
    check("unhook", loader->unhook(loader, (void*)overloaded_plus_1000));
    check("share taken", !loader->share(loader, "api_a.value", &shared_value));
}


PLUGIN_EXPORT int plugin_init(const PluginApi* api) {
    loader = api;

    check("api size", api->size >= sizeof *api && api->version == LOADER_API_VERSION);

    if (strcmp(api->mod_id, "api_a") == 0) {
        start_a();
    } else {
        start_b();
    }

    return PLUGIN_OK;
}

PLUGIN_EXPORT void plugin_ready(const PluginApi* api) {
    if (strcmp(api->mod_id, "api_b") == 0) {
        int* value = api->shared(api, "api_a.value");

        check("shared", value != NULL && *value == 1234);
    }
}
