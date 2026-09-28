// Exercises the C++ wrapper against the fake game, built by GCC and by Visual Studio. Started as mods cpp_main and
// cpp_fail: cpp_main checks everything and hooks the fake game; cpp_fail hooks and patches, then fails, so its
// changes must be undone.
#include <mt2loader.hpp>

struct Vector2 {
    float x;
    float y;
};

struct Vector3 {
    float x;
    float y;
    float z;
};

struct FakeCharacter {
    int weapons;
};

// Declared outside any function: looked up when the plugin starts.
game::Function<int(void* character, bool show)> equip_weapon{"FakeCharacter::EquipWeaponModel"};

static int shared_value = 1234;


static void check(std::string_view what, bool ok) {
    plugin::log("check {}: {}", what, ok ? "ok" : "FAILED");
}

template<class Step>
static std::string error_of(Step step) {
    try {
        step();
    } catch (const plugin::Error& error) {
        return error.what();
    }

    return "";
}

template<class Step>
static bool throws(Step step) {
    try {
        step();
    } catch (const plugin::Error&) {
        return true;
    }

    return false;
}


static void check_plugin_info() {
    check("id", plugin::id() == "cpp_main");
    check("game build", plugin::game_build() == "test");
    check("folder", std::filesystem::exists(plugin::folder() / "manifest.json"));
}

static void check_addresses() {
    game::Address equip = game::find("_ZN13FakeCharacter16EquipWeaponModelEb");
    game::Address rig_check = game::find("FakeCharacter::RigCheck");
    game::Address results = game::find("fake_write_results");
    game::Address gold = game::find("fake_gold");

    check("find readable", game::find("FakeCharacter::EquipWeaponModel(bool)") == equip);
    check("find with const", game::find("FakeCharacter::Title() const") == game::find("FakeCharacter::Title"));
    check("try_find missing", !game::try_find("FakeCharacter::Nothing"));
    check("find missing throws", throws([] { game::find("FakeCharacter::Nothing"); }));
    check("name", equip.name() == "FakeCharacter::EquipWeaponModel(bool)");
    check("describe", (equip + 3).describe() == "FakeCharacter::EquipWeaponModel(bool)+0x3");
    check("format", std::format("{}", equip + 3) == "FakeCharacter::EquipWeaponModel(bool)+0x3");
    check("read", gold.read<int>() == 5);
    check("try_read bad address", !game::Address(reinterpret_cast<void*>(0x10)).try_read<int>());
    check("read bad address throws", throws([] { game::Address(reinterpret_cast<void*>(0x10)).read<int>(); }));

    game::Address text_use = rig_check.find_text("fake_humanoid");

    check("find_text", text_use.instruction_length() == 7 && text_use.next() == text_use + 7);
    check("find_text missing throws", throws([&] { rig_check.find_text("fake_human"); }));

    game::Address call = results.find_reference(rig_check);

    check("find_reference and target", call.target() == rig_check);
    check("target of a non-jump throws", throws([&] { text_use.target(); }));

    std::string first_byte = std::format("{:02X}", equip.read<std::uint8_t>());

    check("matches", equip.matches(first_byte) && equip.scan(first_byte, 16) == equip);
    check("expect throws", throws([&] { (equip + 1).expect(first_byte + " " + first_byte + " " + first_byte); }));
    check("scan whole code", game::scan(first_byte) != game::Address());
    check("find a template without its return type",
        game::find("fake_equal<std::string>") == game::find("bool fake_equal<std::string>(std::string const&, char const*)"));
}

static void check_strings() {
    game::String empty;
    game::String short_text = "short";
    game::String long_text = "a text that is longer than fifteen characters";
    game::String copy = long_text;
    game::String moved = std::move(copy);

    check("string empty", empty.empty() && empty.view().empty() && empty.c_str()[0] == '\0');
    check("string short", short_text == "short" && short_text.size() == 5);
    check("string long", long_text == std::string("a text that is longer than fifteen characters"));
    check("string moved", moved == long_text && copy.empty());

    moved = "short again";
    short_text = "now the short one is longer than fifteen";

    check("string assigned", moved == "short again" && short_text.str() == "now the short one is longer than fifteen");
    check("string formatted", std::format("[{}]", short_text) == "[now the short one is longer than fifteen]");
}

static void check_calls() {
    FakeCharacter character{3};
    game::Function<game::String(const void* character)> title{"FakeCharacter::Title"};
    game::Function<game::String(const void* character, const game::String& name)> greet{"FakeCharacter::Greet"};
    game::Function<int(void* character, const game::String& rig)> rig_check{"FakeCharacter::RigCheck"};
    game::Function<float(const void* character, float value)> scale{"FakeCharacter::Scale"};
    game::Function<Vector2(const void* character)> position2{"FakeCharacter::Position2"};
    game::Function<Vector3(const void* character, float scale)> position3{"FakeCharacter::Position3"};

    check("call int", equip_weapon(&character, true) == 3);
    check("call returning a string", title(&character) == "Fake character with 3 weapons");
    check("call with a short string", greet(&character, "Yami") == "Hello, Yami!");
    check("call with a long string", greet(&character, std::string("a player with a long name")) == "Hello, a player with a long name!");
    check("call with a string reference", rig_check(&character, "fake_humanoid") == 3 && rig_check(&character, "other") == 0);
    check("call float", scale(&character, 2.0f) == 6.0f);

    Vector2 flat = position2(&character);
    Vector3 point = position3(&character, 2.0f);

    check("call returning 8 bytes", flat.x == 4.5f && flat.y == 2.5f);
    check("call returning 12 bytes", point.x == 2.0f && point.y == 4.0f && point.z == 3.0f);
    check("field", game::field<int>(&character, 0) == 3);
}

// config.json declares them; settings.json (as the mod manager writes it) has the player's values.
static void check_settings() {
    check("setting values", plugin::setting<int>("count") == 10 && plugin::setting<double>("count") == 10.0
        && plugin::setting<double>("ratio") == 0.5 && !plugin::setting<bool>("enabled") && plugin::setting<std::string>("mode") == "b"
        && plugin::setting<std::string>("title") == "hi");
    check("setting wrong type", error_of([] { plugin::setting<bool>("count"); })
        .find("is a \"int\" in config.json: read it as plugin::setting<int>") != std::string::npos);
    check("setting missing", error_of([] { plugin::setting<int>("nothing"); }).find("There's no setting 'nothing'") != std::string::npos);
}

static void check_remembered() {
    plugin::Remembered<bool> shown{"shown", true};
    plugin::Remembered<int> starts{"starts", 0};
    plugin::Remembered<std::string> greeting{"greeting", "hi"};

    check("remembered defaults", shown && starts == 0 && greeting.get() == "hi");

    shown = false;
    starts = starts + 1;
    greeting = std::string("hello there");

    plugin::Remembered<bool> shown_again{"shown", true};

    check("remembered kept", !shown_again && starts == 1 && greeting.get() == "hello there");
    check("data folder", plugin::data_folder().filename() == "cpp_main" && std::filesystem::exists(plugin::data_folder() / "remembered.txt"));
}

static void check_enums() {
    game::Enumeration types = game::enumeration("FakeLeaderboard::Type");

    check("enum value", types.value("gold") == 1);
    check("enum value missing", error_of([&] { types.value("bronze"); }).find("has no value called \"bronze\"") != std::string::npos);
    check("enum word taken", error_of([&] { types.add("level", 5); }).find("already has \"level\" (it's 0)") != std::string::npos);

    types.add("spenders", 100);

    std::vector<std::string> saves = game::find_names("vsProperty<FakeLeaderboard::Type, ");
    check("names found by part", std::find(saves.begin(), saves.end(),
        "vsProperty<FakeLeaderboard::Type, FakeEntry>::Save(vsNullObject const*, vsRecord*, vsSaveObjectContext*) const") != saves.end());
    check("names found by part only", game::find_names("NoSuchNameInTheGame").empty());
}

// Runs while the fake game writes its results: fields by name, lists, singletons, create and destroy.
static void check_reflection() {
    game::in("FakeSubscriberManager::Total").replace([](const void*) {
        void* manager = game::singleton("FakeSubscriberManager");
        const game::Objects& subscribers = game::field<game::Objects>(manager, "FakeSubscriberManager::subscriber");
        int total = 0;

        for (void* subscriber : subscribers) {
            total += game::field<int>(subscriber, "FakeSubscriber::spend_other");
        }

        void* first = subscribers[0];
        void* toon = game::field<void*>(first, "FakeSubscriber::main");

        check("field float", game::field<float>(first, "FakeSubscriber::happiness") == 0.5f);
        check("field of a base class", game::field<int>(toon, "FakeToon::level") == 12 && game::field<game::String>(toon, "FakeToon::name") == "Fakey");
        check("field wrong type", error_of([&] { game::field<float>(first, "FakeSubscriber::spend_other"); })
            .find("is of type int in the game: read it as game::field<int>") != std::string::npos);
        check("field text as a number", error_of([&] { game::field<int>(toon, "FakeToon::name"); }).find("is text in the game") != std::string::npos);
        check("field pointer as a number", error_of([&] { game::field<int>(first, "FakeSubscriber::main"); }).find("is a pointer to FakeToon") != std::string::npos);
        check("field missing", error_of([&] { game::field<int>(first, "FakeSubscriber::nothing"); })
            .find("FakeSubscriber has no saved field called 'nothing'") != std::string::npos);
        check("list bounds", subscribers.size() == 2 && throws([&] { subscribers[2]; }));
        check("field link", game::field<game::Link>(first, "FakeSubscriber::favorite").get() == toon);
        check("field link to an object that's gone", !game::field<game::Link>(subscribers[1], "FakeSubscriber::favorite"));
        check("field link as a pointer", error_of([&] { game::field<void*>(first, "FakeSubscriber::favorite"); })
            .find("read it as game::field<game::Link>") != std::string::npos);
        check("object inside", game::field<int>(game::object_in(first, "FakeSubscriber::badge"), 8) == 5);
        check("object inside as a pointer", error_of([&] { game::field<void*>(first, "FakeSubscriber::badge"); })
            .find("get it with game::object_in") != std::string::npos);
        check("object inside of a number", error_of([&] { game::object_in(first, "FakeSubscriber::spend_other"); })
            .find("not an object kept inside another") != std::string::npos);
        check("singleton missing", error_of([] { game::singleton("FakeNothing"); }).find("keeps no single FakeNothing") != std::string::npos);

        game::field<game::String>(toon, "FakeToon::name") = "Renamed by a plugin, with a long name";

        void* board = game::create("FakeBoard");

        check("create", game::field<int>(board, 8) == 7);
        game::destroy(board);

        return total;
    });
}

game::Function<void(void* npc, const char* marker)> show_marker{"FakeNpc::ShowMarker"};
game::Function<void(void* rules, const char* shown)> show_rule{"FakeRules::Show"};

static int visits_in_saved_game() {
    try {
        return game::saved().get("visits", 0);
    } catch (const plugin::Error&) {
        return 0;
    }
}

// Runs while the fake game saves a game with two quest givers, closes it and loads it again.
static void check_saved() {
    game::in("FakeNpc::Generate").after([](void* npc, int kind) {
        if (kind != 1) {
            return;
        }

        game::Saved marker = game::saved(npc);
        marker.set("shape", "question");
        marker.set("color", "#FF8800");
        marker.set("uses", 2);
        marker.set("gone", true);
        marker.erase("gone");
        game::saved().set("visits", 3);

        static int plain = 0;

        check("saved read back", marker.get("shape", "") == "question" && marker.get("uses", 0) == 2 && marker.find<int>("uses") == 2);
        check("saved keys", marker.keys() == std::vector<std::string>{ "shape", "color", "uses" });
        check("saved missing", !marker.has("gone") && marker.get("nothing", "fallback") == "fallback");
        check("saved wrong type", marker.get("shape", 5) == 5 && !marker.find<int>("shape").has_value());
        check("saved bad key", error_of([&] { marker.set("bad key!", 1); }).find("Couldn't keep 'bad key!'") != std::string::npos);
        check("saved on an object the game doesn't save", error_of([] { game::saved(&plain).set("x", 1); }).find("Couldn't keep 'x'") != std::string::npos);
    });

    game::in("FakeNpc::PostResolve").after([](void* npc) {
        game::Saved marker = game::saved(npc);
        std::string shown = std::format("{} {} visits={}", marker.get("shape", "none"), marker.get("color", "none"), visits_in_saved_game());

        show_marker(npc, shown.c_str());
    });

    // Rules the game writes to a text file (a saved game's rules.vrt) keep values too.
    game::in("FakeRules::Setup").after([](void* rules) {
        game::saved(rules).set("rule.no_story", true);
    });

    game::in("FakeRules::Loaded").after([](void* rules) {
        show_rule(rules, game::saved(rules).get("rule.no_story", false) ? "kept" : "lost");
    });
}

static int* game_gold;
static int* game_seen;

// The easy ways: game::in(...) with plain lambdas, calls, texts and numbers.
static void change_with_targets() {
    game_gold = game::find("fake_gold").as<int>();
    game_seen = game::find("fake_seen").as<int>();

    game::in("FakeCharacter::EquipWeaponModel").before([](void*, bool show) {
        *game_gold += show ? 100 : 1;
    });

    game::in("FakeCharacter::EquipWeaponModel").after([](int result, void*, bool) {
        return result + 10;
    });

    game::in("FakeCharacter::Title").after([](game::String title, const void*) {
        return game::String(title.str() + " (hooked)");
    });

    game::in("FakeCharacter::Greet").replace([](const void*, const game::String& name) -> game::String {
        return std::format("Hi {}, from a hook with a long text", name);
    });

    game::in("FakeCharacter::Position2").after([](Vector2 flat, const void*) {
        flat.y = 7.0f;

        return flat;
    });

    game::in("FakeCharacter::Sum6").before([](const void*, int a, float b, int c, double d, int e, int f) {
        *game_seen = static_cast<int>(a + b + c + d + e + f);
    });

    game::Code weapons = game::in("FakeCharacter::Weapons");

    check("call ambiguous without the text", error_of([&] { weapons.call("fake_equal").returns(true); }).find("has 2 calls to fake_equal") != std::string::npos);
    check("call missing", error_of([&] { weapons.call("fake_equal").with_text("fake_bird").returns(true); }).find("has no call to fake_equal with the text \"fake_bird\"") != std::string::npos);
    check("call places", weapons.call("fake_equal").every().places().size() == 2);

    weapons.call("fake_equal").with_text("fake_quadruped").returns(true);

    game::Code limit = game::in("FakeCharacter::Limit");

    check("number places", limit.number(777).places().size() == 1);
    check("number too big", error_of([&] { limit.number(777).becomes(1ll << 40); }).find("from -2147483648 to 2147483647") != std::string::npos);

    limit.number(777).becomes(1234);
    game::in("FakeCharacter::Motto").text("fake motto").becomes("a changed motto that is longer");

    // One lambda type in two places: each gets its own entry.
    for (const char* name : { "FakeCharacter::First", "FakeCharacter::Second" }) {
        game::in(name).returns(50);
    }
}

// The advanced way: the type written out, and the game's function called through original.
static void change_with_hooks() {
    game::in("FakeCharacter::Count").after([](void*) {
        plugin::fail("this code after fails on purpose");
    });

    game::in("FakeCharacter::Doubled").after([](int, const void*, int) -> int {
        plugin::fail("this code after fails on purpose too");
    });

    game::hook<int(void* character, int value)>("FakeCharacter::Overloaded(int)", [](auto, void*, int) -> int {
        plugin::fail("this hook fails on purpose");
    });

    game::hook<float(const void* character, float value)>("FakeCharacter::Scale", [](auto original, const void* character, float value) {
        return original(character, value) + 0.5f;
    });

    int offset = 100;

    game::hook<Vector3(const void* character, float scale)>("FakeCharacter::Position3", [offset](auto original, const void* character, float scale) {
        Vector3 point = original(character, scale);
        point.z += static_cast<float>(offset);

        return point;
    });

    // A call to a function that returns 12 bytes: the result pointer comes first at the call too.
    game::in("FakeCharacter::Reach").call("FakeCharacter::Position3").after([](Vector3 point, const void*, float) {
        point.x += 1000.0f;

        return point;
    });

    game::Hook removed = game::hook<int(void* character, const game::String& rig)>("FakeCharacter::RigCheck", [](auto, void*, const game::String&) {
        return 999;
    });

    removed.remove();
    check("hook removed", !removed);
}

static void start_main() {
    check_plugin_info();
    check_addresses();
    check_strings();
    check_calls();
    check_settings();
    check_remembered();
    check_enums();
    check_reflection();
    check_saved();
    change_with_targets();
    change_with_hooks();

    plugin::share("cpp_main.value", &shared_value);
    plugin::on_ready([] {
        int* value = plugin::shared<int>("cpp_main.value");

        check("on_ready and shared", value != nullptr && *value == 1234);
    });
}

// Changes every kind of thing, then fails: all of it must be undone before cpp_main starts.
static void start_failing() {
    game::hook<int(void* character, bool show)>("FakeCharacter::EquipWeaponModel", [](auto original, void* character, bool show) {
        return original(character, show) + 1000;
    });

    game::patch(game::find("fake_gold"), { 0x63, 0, 0, 0 });
    game::in("FakeCharacter::Weapons").call("fake_equal").with_text("fake_humanoid").returns(false);
    game::in("FakeCharacter::Limit").number(777).becomes(999);
    game::in("FakeCharacter::Motto").text("fake motto").becomes("undone motto");
    game::in("FakeCharacter::Sum6").before([](const void*, int, float, int, double, int, int) {
        *game::find("fake_seen").as<int>() = -1;
    });
    game::enumeration("FakeLeaderboard::Type").add("spenders", 999);

    plugin::on_ready([] {
        plugin::log("check ready after a failed start: FAILED");
    });

    plugin::fail("stopping on purpose");
}

void plugin::init() {
    if (plugin::id() == "cpp_main") {
        start_main();
    } else {
        start_failing();
    }
}
