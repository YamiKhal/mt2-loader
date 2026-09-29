/*
    MT2 Loader for C++: the one file a plugin includes.

        #include <mt2loader.hpp>

        void plugin::init() {
            plugin::log("Hello from {}", plugin::id());
        }

    plugin::   the loader: init, log, fail, setting, Remembered, on_ready, share, the mod's id and folders
    game::     the game: in, find, Function, String, field, singleton, create, enumeration, hook, patch, Address

    The game was compiled by GCC, and this plugin may be compiled by another compiler (Visual Studio). Where the two
    would disagree (how a class is returned, what a std::string is, whose heap memory is on) this header does it the
    game's way, so plugin code never has to think about it. Exceptions never reach the game: a hook that throws is
    logged and the game's own function runs instead, and if plugin::init throws, everything it changed is undone.

    Needs C++20: Visual Studio 2022 or newer, GCC 13 or newer, or clang 17 or newer. The project template sets it.
    Full guide: LOADER_MODDING.md
    MIT license, Copyright (c) 2026 YamiKhal: use it in any plugin, open or closed.
*/
#ifndef MT2LOADER_HPP
#define MT2LOADER_HPP

#if !defined(__cplusplus) || (__cplusplus < 202002L && (!defined(_MSVC_LANG) || _MSVC_LANG < 202002L))
#error "mt2loader.hpp needs C++20. The project template sets it: set(CMAKE_CXX_STANDARD 20)"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <ranges>
#include <set>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// The raw C API underneath, kept out of sight: nothing in it is needed from C++.
#define MT2LOADER_WRAPPER

namespace plugin::raw {
#include "mt2loader.h"
}

#undef PLUGIN_EXPORT
#undef PLUGIN_OK
#undef API_HAS
#undef LOADER_API_VERSION

#if defined(__GNUC__)
#define MT2LOADER_ENTRY extern "C" __attribute__((dllexport, used)) inline
#else
#define MT2LOADER_ENTRY extern "C" __declspec(dllexport) inline
#endif


namespace plugin {

// What the plugin does when the game starts. Write it in your plugin: void plugin::init() { ... }
// Runs once, before any game system exists. If it throws (plugin::fail, or a game:: call that can't do its job),
// the plugin doesn't start, the log says why, and every hook and patch it made is undone.
void init();

// Thrown by plugin::fail and by game:: calls that can't do their job. Its message ends up in the log.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}


namespace plugin::detail {

struct State {
    const raw::PluginApi* api = nullptr;
    bool started = false;
    std::string early_problem;
    std::vector<std::function<void()>> ready_steps;
    std::vector<std::function<void()>> pending_lookups;
    // While init or a ready step runs: how to undo each hook and patch it makes.
    std::vector<std::function<void()>>* undo_steps = nullptr;
    void* game_allocate = nullptr;
};

inline State& state() {
    static State value;

    return value;
}

inline bool api_ready() {
    return state().api != nullptr;
}

inline void write_log(const std::string& text) {
    const raw::PluginApi* api = state().api;

    if (api != nullptr) {
        api->log(api, "%s", text.c_str());
    }
}

inline void remember_undo(std::function<void()> step) {
    if (state().undo_steps != nullptr) {
        state().undo_steps->push_back(std::move(step));
    }
}

}


namespace plugin {

// Writes one line to the loader's log (mt2loader\loader.log), with the mod id in front. std::format style:
// plugin::log("Gold: {}, name: {}", gold, name)
template<class... Arguments>
void log(std::format_string<Arguments...> format, Arguments&&... arguments) {
    detail::write_log(std::format(format, std::forward<Arguments>(arguments)...));
}

// Stops what the plugin is doing, with a message for the log: in init, the plugin doesn't start and its changes
// are undone; in a hook, the game's own function runs instead.
template<class... Arguments>
[[noreturn]] void fail(std::format_string<Arguments...> format, Arguments&&... arguments) {
    throw Error(std::format(format, std::forward<Arguments>(arguments)...));
}

}


namespace plugin::detail {

inline const raw::PluginApi& api() {
    if (state().api == nullptr) {
        plugin::fail("The loader was used before the plugin started (from a global variable?): use it in plugin::init or later");
    }

    return *state().api;
}

}


namespace plugin {

// The mod's id, from its manifest.json.
inline std::string_view id() {
    return detail::api().mod_id;
}

// The game build, as manifest.json's "game_builds" names it, like "0.30.7".
inline std::string_view game_build() {
    return detail::api().game_build;
}

inline std::string_view loader_version() {
    return detail::api().loader_version;
}

// The mod's folder, to read its own files: plugin::folder() / "settings.json".
inline std::filesystem::path folder() {
    return std::filesystem::path(detail::api().mod_folder);
}

// Runs callback once every plugin's init has run, still before the game loads its mods: the place to use what
// other plugins shared. Call it from init.
inline void on_ready(std::function<void()> callback) {
    detail::state().ready_steps.push_back(std::move(callback));
}

// Publishes a pointer under a name for other plugins: a function, or a struct of functions and plain data.
// Names are global, so start them with your mod id: plugin::share("wings.api", &api).
inline void share(std::string_view name, void* pointer) {
    const raw::PluginApi& api = detail::api();

    if (!api.share(&api, std::string(name).c_str(), pointer)) {
        fail("'{}' couldn't be shared (the line above says why)", name);
    }
}

// What another plugin shared under name, or nullptr. Use it in an on_ready step, once everything is shared.
template<class T = void>
T* shared(std::string_view name) {
    const raw::PluginApi& api = detail::api();

    return static_cast<T*>(api.shared(&api, std::string(name).c_str()));
}

// A folder for the plugin's own files that stays when the mod is updated or reinstalled:
// <the game's user folder>\mt2loader\<mod id>. Made the first time it's asked for.
inline std::filesystem::path data_folder() {
    // The mod's folder is <the game's user folder>\mod\<folder>.
    std::filesystem::path path = folder().parent_path().parent_path() / "mt2loader" / std::filesystem::path(std::string(id()));
    std::error_code error;
    std::filesystem::create_directories(path, error);

    if (error) {
        fail("The folder {} couldn't be made: {}", path.string(), error.message());
    }

    return path;
}

}


namespace plugin::detail {

// Text as a value of type T: true/false, a number, or the text itself. Empty if it doesn't read as one.
template<class T>
std::optional<T> value_from_text(const std::string& text) {
    if constexpr (std::is_same_v<T, bool>) {
        if (text == "true" || text == "false") {
            return text == "true";
        }

        return std::nullopt;
    } else if constexpr (std::is_arithmetic_v<T>) {
        T value{};
        auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

        if (error != std::errc() || end != text.data() + text.size()) {
            return std::nullopt;
        }

        return value;
    } else {
        return text;
    }
}

template<class T>
std::string text_from_value(const T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_arithmetic_v<T>) {
        return std::format("{}", value);
    } else {
        std::string text = value;
        std::replace(text.begin(), text.end(), '\n', ' ');
        std::replace(text.begin(), text.end(), '\r', ' ');

        return text;
    }
}

// config.json's kinds, and the C++ type each reads as.
inline std::string_view type_for_kind(std::string_view kind) {
    if (kind == "bool") {
        return "bool";
    }

    if (kind == "int") {
        return "int";
    }

    if (kind == "float") {
        return "double";
    }

    return "std::string";
}

template<class T>
bool reads_as(std::string_view kind) {
    if constexpr (std::is_same_v<T, bool>) {
        return kind == "bool";
    } else if constexpr (std::is_floating_point_v<T>) {
        return kind == "float" || kind == "int";
    } else if constexpr (std::is_integral_v<T>) {
        return kind == "int";
    } else {
        return true;
    }
}

}


namespace plugin {

/*
    A setting your mod declares in its config.json, as the player set it in MT2 Mod Manager (or its default):

        int minimum = plugin::setting<int>("minimum_spend");
        std::string counted = plugin::setting<std::string>("counted");

    config.json is the manager's settings format (mt2-modmanager/docs/MODDING.md): the manager shows each setting
    with its label, slider or list, keeps the player's value per mod profile, and puts it next to your plugin when
    it deploys. Without the manager, the defaults apply. A "bool" reads as bool, an "int" as a whole number, a
    "float" as double, a "string" or "choice" as std::string (the choice's value).
*/
template<class T>
T setting(std::string_view key) {
    static_assert(std::is_arithmetic_v<T> || std::is_same_v<T, std::string>, "A setting reads as bool, a number or std::string");

    const raw::PluginApi& api = detail::api();
    char kind[16];
    char value[1024];

    if (!api.setting(&api, std::string(key).c_str(), kind, sizeof kind, value, sizeof value)) {
        fail("There's no setting '{}' (the line above says why)", key);
    }

    if (!detail::reads_as<T>(kind)) {
        fail("The setting '{}' is a \"{}\" in config.json: read it as plugin::setting<{}>", key, kind, detail::type_for_kind(kind));
    }

    std::optional<T> read = detail::value_from_text<T>(value);

    if (!read) {
        fail("The setting '{}' has the value {}, which isn't a {}", key, value, detail::type_for_kind(kind));
    }

    return *read;
}

}


namespace plugin::detail {

struct RememberedValues {
    bool loaded = false;
    std::map<std::string, std::string, std::less<>> values;
};

inline RememberedValues& remembered() {
    static RememberedValues value;

    return value;
}

inline std::filesystem::path remembered_file() {
    return data_folder() / "remembered.txt";
}

inline void load_remembered() {
    RememberedValues& current = remembered();

    if (current.loaded) {
        return;
    }

    current.loaded = true;

    std::ifstream file(remembered_file());
    std::string line;

    while (std::getline(file, line)) {
        std::size_t separator = line.find(" = ");

        if (separator != std::string::npos) {
            current.values[line.substr(0, separator)] = line.substr(separator + 3);
        }
    }
}

// Written next to the old file first, so a crash halfway never leaves half a file.
inline void save_remembered() {
    std::filesystem::path file = remembered_file();
    std::filesystem::path written = file;
    written += ".new";

    {
        std::ofstream out(written, std::ios::trunc);

        for (const auto& [name, value] : remembered().values) {
            out << name << " = " << value << '\n';
        }

        if (!out) {
            fail("What the plugin remembers couldn't be saved to {}", written.string());
        }
    }

    std::error_code error;
    std::filesystem::rename(written, file, error);

    if (error) {
        std::filesystem::remove(file, error);
        std::filesystem::rename(written, file, error);
    }

    if (error) {
        fail("What the plugin remembers couldn't be saved to {}: {}", file.string(), error.message());
    }
}

}


namespace plugin {

/*
    A value the plugin changes and keeps between game sessions, like a choice the player makes in the game:

        plugin::Remembered<bool> show_spenders{"show_top_spenders", true};

        if (show_spenders) { ... }
        show_spenders = false;    // saved right away

    true/false, a number or text (std::string). Kept in data_folder() / "remembered.txt", one "name = value" line
    each; a line that doesn't read as the type is ignored, and the default is used. For options the player sets
    before playing, declare a setting in config.json instead (plugin::setting): the mod manager shows those.
*/
template<class T>
class Remembered {
    static_assert(std::is_arithmetic_v<T> || std::is_same_v<T, std::string>, "Remembered holds true/false (bool), a number or text (std::string)");

public:
    Remembered(std::string_view name, T default_value) : remembered_name(name), fallback(std::move(default_value)) {}

    T get() const {
        detail::load_remembered();

        const auto& values = detail::remembered().values;
        auto found = values.find(remembered_name);

        if (found == values.end()) {
            return fallback;
        }

        return detail::value_from_text<T>(found->second).value_or(fallback);
    }

    void set(const T& value) {
        detail::load_remembered();

        std::string text = detail::text_from_value(value);
        std::string& stored = detail::remembered().values[remembered_name];

        if (stored != text) {
            stored = text;
            detail::save_remembered();
        }
    }

    operator T() const {
        return get();
    }

    Remembered& operator=(const T& value) {
        set(value);

        return *this;
    }

private:
    std::string remembered_name;
    T fallback;
};

}


namespace game {

// How far find_text, find_reference and scan look from an address when not told: the first 4 KB of a function.
inline constexpr std::size_t search_range = 0x1000;

// An address in the running game. Pointers turn into one by themselves; as<T>() turns it back.
class Address {
public:
    Address() = default;

    Address(std::nullptr_t) {}

    template<class T>
        requires (!std::is_same_v<std::remove_cv_t<T>, char>)
    Address(T* pointer) : number_value(reinterpret_cast<std::uintptr_t>(pointer)) {}

    // Explicit for char pointers, so that text ("mmoCharacter::Update") is always taken as a name.
    explicit Address(const char* pointer) : number_value(reinterpret_cast<std::uintptr_t>(pointer)) {}

    explicit operator bool() const {
        return number_value != 0;
    }

    void* get() const {
        return reinterpret_cast<void*>(number_value);
    }

    template<class T>
    T* as() const {
        return reinterpret_cast<T*>(number_value);
    }

    std::uintptr_t number() const {
        return number_value;
    }

    Address operator+(std::ptrdiff_t offset) const {
        return from_number(number_value + offset);
    }

    Address operator-(std::ptrdiff_t offset) const {
        return from_number(number_value - offset);
    }

    std::ptrdiff_t operator-(Address other) const {
        return static_cast<std::ptrdiff_t>(number_value - other.number_value);
    }

    auto operator<=>(const Address&) const = default;

    // A copy of what's there. Throws if the address can't be read; never crashes.
    template<class T>
    T read() const;

    template<class T>
    std::optional<T> try_read() const;

    // Whether the bytes here match a pattern: hex bytes, ?? for any byte, like "48 8D 15 ?? ?? ?? ??".
    bool matches(std::string_view pattern) const;

    // Throws unless the bytes here match pattern: the check before changing code a game update may have moved.
    void expect(std::string_view pattern) const;

    // The first instruction from here that uses the text (a whole string, like "humanoid"). Throws if none does.
    Address find_text(std::string_view text, std::size_t range = search_range) const;
    Address try_find_text(std::string_view text, std::size_t range = search_range) const;

    // The first instruction from here that refers to target: uses a global, or calls or jumps to it.
    Address find_reference(Address target, std::size_t range = search_range) const;
    Address try_find_reference(Address target, std::size_t range = search_range) const;

    // The first place from here that matches pattern.
    Address scan(std::string_view pattern, std::size_t range = search_range) const;
    Address try_scan(std::string_view pattern, std::size_t range = search_range) const;

    // Length of the instruction here, and the address of the one after it.
    std::size_t instruction_length() const;
    Address next() const;

    // Where the call or jump here goes.
    Address target() const;

    // The readable name of the function or global this address is in, like "mmoCharacter::EquipWeaponModel(bool)",
    // or empty. describe() adds the offset: "mmoCharacter::EquipWeaponModel(bool)+0x61".
    std::string name() const;
    std::string describe() const;

private:
    static Address from_number(std::uintptr_t number) {
        Address address;
        address.number_value = number;

        return address;
    }

    std::uintptr_t number_value = 0;
};

// A game function or global by name: readable ("mmoCharacter::EquipWeaponModel"), with parameters for overloads
// ("mmoCharacter::EquipWeaponModel(bool)"), or mangled. find throws if there's no such name (the log says why);
// try_find returns an empty Address. mt2sdk find <text> lists the names.
inline Address try_find(std::string_view name) {
    const plugin::raw::PluginApi& api = plugin::detail::api();

    return Address(api.find(&api, std::string(name).c_str()));
}

inline Address find(std::string_view name) {
    Address found = try_find(name);

    if (!found) {
        plugin::fail("'{}' isn't in the game (the line above says why)", name);
    }

    return found;
}

// Every game function or global whose readable name contains text, each a name find takes: for the ones a
// template or class makes many of, like find_names("vsProperty<mmoToonThought::Type, "). Needs loader 0.10.0.
inline std::vector<std::string> find_names(std::string_view text) {
    const plugin::raw::PluginApi& api = plugin::detail::api();

    if (api.size < offsetof(plugin::raw::PluginApi, find_names) + sizeof(void*)) {
        plugin::fail("find_names needs loader 0.10.0 or newer (this is {})", api.loader_version);
    }

    std::string wanted(text);
    std::string names(api.find_names(&api, wanted.c_str(), nullptr, 0), '\0');
    api.find_names(&api, wanted.c_str(), names.data(), names.size());

    std::vector<std::string> found;
    std::size_t start = 0;

    for (std::size_t end = names.find('\n'); end != std::string::npos; end = names.find('\n', start)) {
        std::string name = names.substr(start, end - start);

        if (std::find(found.begin(), found.end(), name) == found.end()) {
            found.push_back(name);
        }

        start = end + 1;
    }

    return found;
}

// The first place in the game's code that matches pattern.
inline Address try_scan(std::string_view pattern) {
    const plugin::raw::PluginApi& api = plugin::detail::api();

    return Address(api.scan(&api, std::string(pattern).c_str()));
}

inline Address scan(std::string_view pattern) {
    Address found = try_scan(pattern);

    if (!found) {
        plugin::fail("Nothing in the game's code matches {}", pattern);
    }

    return found;
}

}


template<>
struct std::formatter<game::Address> : std::formatter<std::string> {
    template<class FormatContext>
    auto format(const game::Address& address, FormatContext& context) const {
        return std::formatter<std::string>::format(address.describe(), context);
    }
};


namespace game {

template<class T>
std::optional<T> Address::try_read() const {
    static_assert(std::is_trivially_copyable_v<T>, "read copies bytes, so T must be a plain type: a number, a pointer, or a struct of those");

    const plugin::raw::PluginApi& api = plugin::detail::api();
    std::array<std::byte, sizeof(T)> bytes;

    if (!api.read(&api, get(), bytes.data(), bytes.size())) {
        return std::nullopt;
    }

    return std::bit_cast<T>(bytes);
}

template<class T>
T Address::read() const {
    std::optional<T> value = try_read<T>();

    if (!value) {
        plugin::fail("{} can't be read", describe());
    }

    return *value;
}

inline bool Address::matches(std::string_view pattern) const {
    const plugin::raw::PluginApi& api = plugin::detail::api();

    return api.matches(&api, get(), std::string(pattern).c_str());
}

inline void Address::expect(std::string_view pattern) const {
    if (!matches(pattern)) {
        plugin::fail("The code at {} isn't what this plugin expects ({}): a game update has probably changed it", describe(), pattern);
    }
}

inline Address Address::try_find_text(std::string_view text, std::size_t range) const {
    const plugin::raw::PluginApi& api = plugin::detail::api();

    return Address(api.find_text_reference(&api, get(), range, std::string(text).c_str()));
}

inline Address Address::find_text(std::string_view text, std::size_t range) const {
    Address found = try_find_text(text, range);

    if (!found) {
        plugin::fail("{} doesn't use the text \"{}\" (a game update or another mod may have changed it)", describe(), text);
    }

    return found;
}

inline Address Address::try_find_reference(Address target, std::size_t range) const {
    const plugin::raw::PluginApi& api = plugin::detail::api();

    return Address(api.find_reference(&api, get(), range, target.get()));
}

inline Address Address::find_reference(Address target, std::size_t range) const {
    Address found = try_find_reference(target, range);

    if (!found) {
        plugin::fail("{} doesn't refer to {}", describe(), target.describe());
    }

    return found;
}

inline Address Address::try_scan(std::string_view pattern, std::size_t range) const {
    const plugin::raw::PluginApi& api = plugin::detail::api();

    return Address(api.scan_range(&api, get(), range, std::string(pattern).c_str()));
}

inline Address Address::scan(std::string_view pattern, std::size_t range) const {
    Address found = try_scan(pattern, range);

    if (!found) {
        plugin::fail("Nothing from {} on matches {}", describe(), pattern);
    }

    return found;
}

inline std::size_t Address::instruction_length() const {
    const plugin::raw::PluginApi& api = plugin::detail::api();
    std::size_t length = api.instruction_length(&api, get());

    if (length == 0) {
        plugin::fail("{} isn't readable code", describe());
    }

    return length;
}

inline Address Address::next() const {
    return *this + static_cast<std::ptrdiff_t>(instruction_length());
}

inline Address Address::target() const {
    const plugin::raw::PluginApi& api = plugin::detail::api();

    // Loader 0.6.0 decodes it, and sees through calls other plugins hooked.
    if (api.size >= offsetof(plugin::raw::PluginApi, decode) + sizeof(void*)) {
        plugin::raw::Instruction instruction;

        if (api.decode(&api, get(), &instruction) && (instruction.is_call || instruction.is_jump) && instruction.reference != nullptr
            && instruction.reference_size > 0) {
            return Address(instruction.reference);
        }

        plugin::fail("{} isn't a call or a jump", describe());
    }

    Address after = next();
    auto opcode = read<std::uint8_t>();
    auto second = (*this + 1).read<std::uint8_t>();
    bool far = opcode == 0xE8 || opcode == 0xE9 || (opcode == 0x0F && (second & 0xF0) == 0x80);
    bool near = opcode == 0xEB || (opcode & 0xF0) == 0x70;

    if (far) {
        return after + (after - 4).read<std::int32_t>();
    }

    if (near) {
        return after + (after - 1).read<std::int8_t>();
    }

    plugin::fail("{} isn't a call or a jump", describe());
}

inline std::string Address::name() const {
    const plugin::raw::PluginApi& api = plugin::detail::api();
    char name[1024];

    if (!api.name_of(&api, get(), name, sizeof name, nullptr)) {
        return {};
    }

    return name;
}

inline std::string Address::describe() const {
    const plugin::raw::PluginApi& api = plugin::detail::api();
    char name[1024];
    std::size_t offset = 0;

    if (!api.name_of(&api, get(), name, sizeof name, &offset)) {
        return std::format("0x{:x}", number_value);
    }

    if (offset == 0) {
        return name;
    }

    return std::format("{}+0x{:x}", name, offset);
}

}


namespace plugin::detail {

inline char* game_allocate(std::size_t size) {
    State& current = state();

    if (current.game_allocate == nullptr) {
        // operator new(unsigned long long): the game's own heap, which the game frees strings from.
        current.game_allocate = game::find("_Znwy").get();
    }

    return static_cast<char*>(reinterpret_cast<void* (*)(std::size_t)>(current.game_allocate)(size));
}

}


namespace game {

/*
    The game's std::string. Visual Studio's std::string is a different thing, so game functions take and return
    this instead. It converts to and from std::string, std::string_view and text, so it can mostly be ignored:

        game::Function<bool(void* manager, const game::String& name)> has_thing{"Manager::HasThing"};
        has_thing(manager, "dragon");

        game::String name = get_name(character);
        plugin::log("Name: {}", name);
        std::string copy = name.str();

    Its memory is on the game's heap, so the game can keep or free it. A string field inside a game object can be
    used in place: game::field<game::String>(costume, 0x38) = "humanoid".
*/
class String {
public:
    String() noexcept {
        make_empty();
    }

    String(std::string_view text) {
        make_empty();
        assign(text);
    }

    String(const char* text) : String(std::string_view(text)) {}

    String(const std::string& text) : String(std::string_view(text)) {}

    String(const String& other) : String(other.view()) {}

    String(String&& other) noexcept {
        take(other);
    }

    ~String() {
        release();
    }

    String& operator=(const String& other) {
        if (this != &other) {
            assign(other.view());
        }

        return *this;
    }

    String& operator=(String&& other) noexcept {
        if (this != &other) {
            release();
            take(other);
        }

        return *this;
    }

    String& operator=(std::string_view text) {
        assign(text);

        return *this;
    }

    String& operator=(const char* text) {
        assign(text);

        return *this;
    }

    String& operator=(const std::string& text) {
        assign(text);

        return *this;
    }

    std::string_view view() const noexcept {
        return std::string_view(characters, length);
    }

    std::string str() const {
        return std::string(view());
    }

    const char* c_str() const noexcept {
        return characters;
    }

    std::size_t size() const noexcept {
        return length;
    }

    bool empty() const noexcept {
        return length == 0;
    }

    operator std::string_view() const noexcept {
        return view();
    }

    friend bool operator==(const String& string, const String& other) {
        return string.view() == other.view();
    }

    friend bool operator==(const String& string, std::string_view other) {
        return string.view() == other;
    }

    friend bool operator==(const String& string, const char* other) {
        return string.view() == other;
    }

    friend bool operator==(const String& string, const std::string& other) {
        return string.view() == other;
    }

private:
    static constexpr std::size_t local_capacity = 15;

    bool is_local() const noexcept {
        return characters == local;
    }

    void make_empty() noexcept {
        characters = local;
        length = 0;
        local[0] = '\0';
    }

    void assign(std::string_view value) {
        std::size_t size = value.size();

        if ((is_local() && size <= local_capacity) || (!is_local() && size <= capacity)) {
            std::memmove(characters, value.data(), size);
            characters[size] = '\0';
            length = size;

            return;
        }

        if (!plugin::detail::api_ready()) {
            plugin::detail::state().early_problem = std::format(
                "a game::String with a long text (\"{}\") was made before the plugin started, probably as a global "
                "variable: make it inside a function instead", value.substr(0, 24));
            release();

            return;
        }

        // Filled before the old text is freed, since value may point into it.
        char* block = plugin::detail::game_allocate(size + 1);
        std::memcpy(block, value.data(), size);
        block[size] = '\0';

        release();
        characters = block;
        length = size;
        capacity = size;
    }

    void release() noexcept {
        if (!is_local() && plugin::detail::api_ready()) {
            const plugin::raw::PluginApi& api = *plugin::detail::state().api;
            api.free_string(&api, reinterpret_cast<plugin::raw::GameString*>(this));
        }

        make_empty();
    }

    void take(String& other) noexcept {
        if (other.is_local()) {
            std::memcpy(local, other.local, other.length + 1);
            characters = local;
        } else {
            characters = other.characters;
            capacity = other.capacity;
        }

        length = other.length;
        other.make_empty();
    }

    // As GCC lays out std::string: the text, its length, and either the text itself (up to 15 characters) or
    // the size of the heap block it's in.
    char* characters;
    std::size_t length;
    union {
        char local[local_capacity + 1];
        std::size_t capacity;
    };
};

static_assert(sizeof(String) == 32, "game::String must be laid out as the game's std::string");

}


template<>
struct std::formatter<game::String> : std::formatter<std::string_view> {
    template<class FormatContext>
    auto format(const game::String& text, FormatContext& context) const {
        return std::formatter<std::string_view>::format(text.view(), context);
    }
};


namespace plugin::detail {

template<class T>
consteval bool is_register_sized_struct() {
    if constexpr (std::is_class_v<T>) {
        return std::is_trivially_copyable_v<T> && (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);
    } else {
        return false;
    }
}

template<class T>
inline constexpr bool fits_register = is_register_sized_struct<T>();

template<class T>
inline constexpr bool always_false = false;

template<std::size_t Size>
struct UnsignedOfSize;

template<>
struct UnsignedOfSize<1> {
    using Type = std::uint8_t;
};

template<>
struct UnsignedOfSize<2> {
    using Type = std::uint16_t;
};

template<>
struct UnsignedOfSize<4> {
    using Type = std::uint32_t;
};

template<>
struct UnsignedOfSize<8> {
    using Type = std::uint64_t;
};

}


namespace game {

// Whether the game returns a T through a pointer to room the caller provides (GCC's rule): anything that isn't a
// number, pointer or reference, unless it's a plain struct of 1, 2, 4 or 8 bytes. A plain struct of that size that
// stands for a game class with a copy constructor or destructor is returned through a pointer too; say so with:
//     template<> inline constexpr bool game::returned_in_memory<MyVector2> = true;
template<class T>
inline constexpr bool returned_in_memory = !std::is_void_v<T> && !std::is_reference_v<T> && !std::is_scalar_v<T>
    && !plugin::detail::fits_register<T>;

}


namespace plugin::detail {

// Your compiler's std::string isn't the game's, so it can't cross: game::String is the game's.
template<class T>
consteval void check_not_std_string() {
    static_assert(!std::is_same_v<std::remove_cvref_t<T>, std::string>,
        "The game's std::string is game::String: write game::String (or const game::String&) where the game has std::string. "
        "Your compiler's std::string is laid out differently. A lambda that returns text needs `-> game::String`");
}

// How a value crosses to the game: numbers and pointers as they are, references as pointers, plain structs of
// 1, 2, 4 or 8 bytes as a number of that size. Anything else can't be passed by value (unchecked in this game).
template<class T>
consteval auto raw_type_of() {
    check_not_std_string<T>();

    if constexpr (std::is_reference_v<T>) {
        return std::type_identity<std::remove_reference_t<T>*>{};
    } else if constexpr (std::is_same_v<std::remove_cv_t<T>, long double>) {
        static_assert(always_false<T>, "long double isn't the same size in the game (GCC) as in Visual Studio: use double");
    } else if constexpr (std::is_scalar_v<T>) {
        return std::type_identity<T>{};
    } else if constexpr (fits_register<T>) {
        return std::type_identity<typename UnsignedOfSize<sizeof(T)>::Type>{};
    } else {
        static_assert(always_false<T>,
            "A game function's parameter is a class taken by value (not a reference or a pointer). How the game passes "
            "those hasn't been checked, so it can't be called or hooked yet. If the game takes it as `const T&`, write that");
    }
}

template<class T>
using Raw = typename decltype(raw_type_of<T>())::type;

template<class T>
consteval auto raw_result_of() {
    check_not_std_string<T>();

    if constexpr (std::is_void_v<T>) {
        return std::type_identity<void>{};
    } else if constexpr (game::returned_in_memory<T>) {
        return std::type_identity<void*>{};
    } else {
        return raw_type_of<T>();
    }
}

template<class T>
using RawResult = typename decltype(raw_result_of<T>())::type;

template<class T>
Raw<T> to_raw(T value) {
    if constexpr (std::is_reference_v<T>) {
        return std::addressof(value);
    } else if constexpr (std::is_scalar_v<T>) {
        return value;
    } else {
        return std::bit_cast<Raw<T>>(value);
    }
}

template<class T>
T from_raw(Raw<T> raw) {
    if constexpr (std::is_reference_v<T>) {
        return static_cast<T>(*raw);
    } else if constexpr (std::is_scalar_v<T>) {
        return raw;
    } else {
        return std::bit_cast<T>(raw);
    }
}

// Calls a game function the way the game's compiler does, whichever compiler built the plugin.
template<class Result, class... Parameters>
Result call(void* function, Parameters... arguments) {
    if (function == nullptr) {
        plugin::fail("A game function was called before it was found");
    }

    if constexpr (game::returned_in_memory<Result>) {
        using RawFunction = void* (*)(void*, Raw<Parameters>...);
        alignas(Result) std::byte storage[sizeof(Result)];

        reinterpret_cast<RawFunction>(function)(storage, to_raw<Parameters>(std::forward<Parameters>(arguments))...);

        Result* returned = std::launder(reinterpret_cast<Result*>(storage));
        Result value(std::move(*returned));
        returned->~Result();

        return value;
    } else if constexpr (std::is_void_v<Result>) {
        using RawFunction = void (*)(Raw<Parameters>...);

        reinterpret_cast<RawFunction>(function)(to_raw<Parameters>(std::forward<Parameters>(arguments))...);
    } else {
        using RawFunction = Raw<Result> (*)(Raw<Parameters>...);

        return from_raw<Result>(reinterpret_cast<RawFunction>(function)(to_raw<Parameters>(std::forward<Parameters>(arguments))...));
    }
}

// How many parameters the game's function takes, from its readable name, or -1 if the name doesn't say.
inline int parameter_count_in(std::string_view name) {
    std::size_t close = name.rfind(')');

    if (close == std::string_view::npos || name.find("operator") != std::string_view::npos) {
        return -1;
    }

    int depth = 0;
    std::size_t open = std::string_view::npos;

    for (std::size_t index = close + 1; index-- > 0;) {
        char character = name[index];

        if (character == ')' || character == '>') {
            depth++;
        } else if ((character == '(' || character == '<') && --depth == 0 && character == '(') {
            open = index;

            break;
        }
    }

    if (open == std::string_view::npos) {
        return -1;
    }

    std::string_view inside = name.substr(open + 1, close - open - 1);

    if (inside.empty() || inside == "void") {
        return 0;
    }

    int count = 1;
    depth = 0;

    for (char character : inside) {
        if (character == '(' || character == '<') {
            depth++;
        } else if (character == ')' || character == '>') {
            depth--;
        } else if (character == ',' && depth == 0) {
            count++;
        }
    }

    return count;
}

// A declaration with the wrong number of parameters calls the game wrongly: say so in the log.
inline void check_parameters(game::Address function, std::size_t declared) {
    std::string name = function.name();
    int count = parameter_count_in(name);

    if (count < 0 || declared == static_cast<std::size_t>(count) || declared == static_cast<std::size_t>(count) + 1) {
        return;
    }

    plugin::log("Check the declaration of {}: it's written with {} parameters, but the game's function takes {} ({} with the "
        "object, for a method)", name, declared, count, count + 1);
}

}


namespace game {

template<class Signature>
class Function;

/*
    A game function, to call like any function. Write its type as the game has it, with the object first for a
    method, and pointers (void*) for the game's objects:

        game::Function<int(void* character, bool show)> equip_weapon{"mmoCharacter::EquipWeaponModel"};
        game::Function<game::String(const void* character)> title{"mmoCharacter::GetTitle"};

        equip_weapon(character, true);

    Declared outside any function, it's looked up when the plugin starts, and a missing name stops the plugin there
    with the reason in the log. Declared inside a function, it's looked up right away.
*/
template<class Result, class... Parameters>
class Function<Result(Parameters...)> {
public:
    Function(std::string_view name) : symbol_name(name) {
        if (plugin::detail::api_ready()) {
            look_up();
        } else {
            plugin::detail::state().pending_lookups.push_back([this] { look_up(); });
        }
    }

    Function(const char* name) : Function(std::string_view(name)) {}

    Function(Address address) : location(address) {}

    Result operator()(Parameters... arguments) const {
        if (!location) {
            look_up();
        }

        return plugin::detail::call<Result, Parameters...>(location.get(), std::forward<Parameters>(arguments)...);
    }

    Address address() const {
        if (!location) {
            look_up();
        }

        return location;
    }

private:
    void look_up() const {
        location = game::find(symbol_name);
        plugin::detail::check_parameters(location, sizeof...(Parameters));
    }

    std::string symbol_name;
    mutable Address location;
};

template<class Signature>
class Original;

// The game's function underneath a hook (and any hooks installed before it), for the hook to call.
template<class Result, class... Parameters>
class Original<Result(Parameters...)> {
public:
    explicit Original(void* const* slot, bool* called = nullptr) : slot(slot), called(called) {}

    Result operator()(Parameters... arguments) const {
        if (called != nullptr) {
            *called = true;
        }

        return plugin::detail::call<Result, Parameters...>(*slot, std::forward<Parameters>(arguments)...);
    }

private:
    void* const* slot;
    // Set once the game's function ran, so a hook that fails afterwards doesn't run it again.
    bool* called;
};

// What game::in(...) and game::hook changed, to take it out again later. Ignoring it is fine: changes stay until
// the game exits.
class Hook {
public:
    Hook() = default;

    explicit operator bool() const {
        return !installed.empty();
    }

    // Everything this hook changed goes back to how it was (other plugins' hooks stay).
    void remove() {
        for (const std::function<void()>& take_out : installed) {
            take_out();
        }

        installed.clear();
    }

    void add(std::function<void()> take_out) {
        installed.push_back(std::move(take_out));
    }

private:
    std::vector<std::function<void()>> installed;
};

// Bytes game::patch or game::in(...) wrote, to put the original ones back later.
class Patch {
public:
    Patch() = default;

    explicit operator bool() const {
        return !changes.empty();
    }

    void undo() {
        const plugin::raw::PluginApi& api = plugin::detail::api();

        for (auto change = changes.rbegin(); change != changes.rend(); ++change) {
            api.write(&api, change->address.get(), change->original.data(), change->original.size());
        }

        changes.clear();
    }

    void add(Address address, std::vector<std::uint8_t> original) {
        changes.push_back({ address, std::move(original) });
    }

    void add(const Patch& other) {
        changes.insert(changes.end(), other.changes.begin(), other.changes.end());
    }

private:
    struct Change {
        Address address;
        std::vector<std::uint8_t> original;
    };

    std::vector<Change> changes;
};

// Writes bytes over the game's code or data, and logs it. If plugin::init fails afterwards, the patch is undone.
// The advanced way: game::in(...) finds and changes texts, numbers and calls without bytes.
inline Patch patch(Address where, std::span<const std::uint8_t> bytes) {
    const plugin::raw::PluginApi& api = plugin::detail::api();
    std::vector<std::uint8_t> original(bytes.size());

    if (bytes.empty() || !api.read(&api, where.get(), original.data(), original.size())) {
        plugin::fail("{} can't be read", where.describe());
    }

    if (!api.write(&api, where.get(), bytes.data(), bytes.size())) {
        plugin::fail("{} couldn't be changed (the line above says why)", where.describe());
    }

    Patch made;
    made.add(where, std::move(original));
    plugin::detail::remember_undo([made]() mutable { made.undo(); });

    return made;
}

inline Patch patch(Address where, std::initializer_list<std::uint8_t> bytes) {
    return patch(where, std::span<const std::uint8_t>(bytes.begin(), bytes.size()));
}

// A field of a game object, offset bytes into it, to read or change in place:
//     void*& actor = game::field<void*>(character, 0x6f0);
// The offsets known so far are in LOADER.md §2.3. A wrong offset or object crashes, as it would in the game.
template<class T>
T& field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset);
}

template<class T>
const T& field(const void* object, std::size_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const std::byte*>(object) + offset);
}

}


namespace plugin::detail {

inline bool loader_has(std::size_t field_end) {
    return api().size >= field_end;
}

// "0.11.0" is at least 0.11.
inline bool loader_at_least(int major, int minor) {
    std::string_view version = api().loader_version;
    int found_major = 0;
    int found_minor = 0;
    auto [after_major, major_error] = std::from_chars(version.data(), version.data() + version.size(), found_major);

    if (major_error == std::errc() && after_major != version.data() + version.size() && *after_major == '.') {
        std::from_chars(after_major + 1, version.data() + version.size(), found_minor);
    }

    return found_major > major || (found_major == major && found_minor >= minor);
}

inline void require_loader_0_6() {
    if (!loader_has(offsetof(raw::PluginApi, detour_before) + sizeof(void*))) {
        fail("this needs MT2 Loader 0.6.0 or newer, and the game has {}", api().loader_version);
    }
}

// Where a hook goes: a whole function, or one call instruction inside one.
struct HookTarget {
    game::Address address;
    bool is_call = false;
    // What the log calls it: "mmoCharacter::EquipWeaponModel(bool)", or "the call to X in Y".
    std::string name;
    // The function that runs there, for checking the declared parameters.
    game::Address function;
};

inline bool install_raw(const HookTarget& target, void* detour, void** original) {
    const raw::PluginApi& api = detail::api();

    if (target.is_call) {
        require_loader_0_6();

        return api.hook_call(&api, target.address.get(), detour, original);
    }

    return api.hook_address(&api, target.address.get(), detour, original);
}

// One hook lambda can be installed in this many places (a loop over functions, .every()).
inline constexpr std::size_t hook_pool_size = 32;

/*
    The entry points the game calls for one kind of hook lambda: one per place it's installed. Each turns the game's
    arguments into C++ ones, runs the lambda, and turns its result back, the way the game's compiler expects.
*/
template<class Signature, class Detour>
struct HookPool;

template<class Result, class... Parameters, class Detour>
struct HookPool<Result(Parameters...), Detour> {
    using Original = game::Original<Result(Parameters...)>;

    // Kept after the hook is removed: a call that was already inside it can still finish.
    struct Slot {
        std::optional<Detour> detour;
        void* original = nullptr;
        bool installed = false;
        std::string target_name;
        std::atomic<bool> failure_logged = false;
    };

    static inline std::array<Slot, hook_pool_size> slots{};

    template<std::size_t Index>
    static Result run(Parameters... arguments) {
        Slot& slot = slots[Index];
        bool called = false;
        Original original(&slot.original, &called);

        try {
            if constexpr (std::is_invocable_v<Detour&, Original, Parameters...>) {
                return (*slot.detour)(original, std::forward<Parameters>(arguments)...);
            } else {
                return (*slot.detour)(std::forward<Parameters>(arguments)...);
            }
        } catch (const std::exception& error) {
            report(slot, error.what());
        } catch (...) {
            report(slot, "an error that isn't a std::exception");
        }

        // A hook that returns nothing and failed after the game's function ran: it ran once, as it should. One that
        // returns a value has no result to give back, so the function runs again for one (.after keeps its own).
        if constexpr (std::is_void_v<Result>) {
            if (called) {
                return;
            }
        }

        return original(std::forward<Parameters>(arguments)...);
    }

    static void report(Slot& slot, const char* problem) {
        if (!slot.failure_logged.exchange(true)) {
            plugin::log("The hook on {} failed: {}. The game's own code ran instead (later failures of this hook aren't logged)",
                slot.target_name, problem);
        }
    }

    template<std::size_t Index>
    static RawResult<Result> entry(Raw<Parameters>... raw_arguments) {
        if constexpr (std::is_void_v<Result>) {
            run<Index>(from_raw<Parameters>(raw_arguments)...);
        } else {
            return to_raw<Result>(run<Index>(from_raw<Parameters>(raw_arguments)...));
        }
    }

    template<std::size_t Index>
    static void* entry_in_memory(void* result, Raw<Parameters>... raw_arguments) {
        ::new (result) Result(run<Index>(from_raw<Parameters>(raw_arguments)...));

        return result;
    }

    template<std::size_t Index>
    static void* entry_address() {
        if constexpr (game::returned_in_memory<Result>) {
            return reinterpret_cast<void*>(&entry_in_memory<Index>);
        } else {
            return reinterpret_cast<void*>(&entry<Index>);
        }
    }

    template<std::size_t... Index>
    static std::array<void*, hook_pool_size> entry_table(std::index_sequence<Index...>) {
        return { entry_address<Index>()... };
    }

    static game::Hook install(const HookTarget& target, const Detour& detour) {
        static_assert(std::is_invocable_v<Detour&, Original, Parameters...> || std::is_invocable_v<Detour&, Parameters...>,
            "The hook's lambda must take the function's parameters, as written in its type, with `auto original` first "
            "to call the game's function");

        static const std::array<void*, hook_pool_size> entries = entry_table(std::make_index_sequence<hook_pool_size>{});
        std::size_t index = 0;

        while (index < hook_pool_size && slots[index].installed) {
            index++;
        }

        if (index == hook_pool_size) {
            fail("One hook lambda is installed in {} places already: write a second one for {}", hook_pool_size, target.name);
        }

        Slot& slot = slots[index];
        void* entry = entries[index];

        check_parameters(target.function, sizeof...(Parameters));
        slot.detour.emplace(detour);
        slot.installed = true;
        slot.target_name = target.name;

        if (!install_raw(target, entry, &slot.original)) {
            slot.installed = false;
            fail("{} couldn't be hooked (the line above says why)", target.name);
        }

        game::Hook made;
        made.add([entry, &slot] {
            const raw::PluginApi& api = detail::api();

            if (slot.installed) {
                api.unhook(&api, entry);
                slot.installed = false;
            }
        });
        remember_undo([made]() mutable { made.remove(); });

        return made;
    }
};

// Code that runs before the game's function, with its arguments, whatever the function returns.
template<class Callback, class... Parameters>
struct BeforePool {
    struct Slot {
        std::optional<Callback> callback;
        bool installed = false;
        std::string target_name;
        std::atomic<bool> failure_logged = false;
    };

    static inline std::array<Slot, hook_pool_size> slots{};

    template<std::size_t Index>
    static void entry(Raw<Parameters>... raw_arguments) {
        Slot& slot = slots[Index];

        try {
            (*slot.callback)(from_raw<Parameters>(raw_arguments)...);
        } catch (const std::exception& error) {
            report(slot, error.what());
        } catch (...) {
            report(slot, "an error that isn't a std::exception");
        }
    }

    static void report(Slot& slot, const char* problem) {
        if (!slot.failure_logged.exchange(true)) {
            plugin::log("The code before {} failed: {}. The game's own code ran on (later failures aren't logged)",
                slot.target_name, problem);
        }
    }

    template<std::size_t... Index>
    static std::array<void*, hook_pool_size> entry_table(std::index_sequence<Index...>) {
        return { reinterpret_cast<void*>(&entry<Index>)... };
    }

    static game::Hook install(const HookTarget& target, const Callback& callback) {
        static const std::array<void*, hook_pool_size> entries = entry_table(std::make_index_sequence<hook_pool_size>{});
        std::size_t index = 0;

        require_loader_0_6();

        while (index < hook_pool_size && slots[index].installed) {
            index++;
        }

        if (index == hook_pool_size) {
            fail("One lambda runs before {} places already: write a second one for {}", hook_pool_size, target.name);
        }

        const raw::PluginApi& api = detail::api();
        Slot& slot = slots[index];
        void** original = nullptr;

        check_parameters(target.function, sizeof...(Parameters));
        slot.callback.emplace(callback);
        slot.installed = true;
        slot.target_name = target.name;

        void* stub = api.detour_before(&api, entries[index], &original);

        if (stub == nullptr || !install_raw(target, stub, original)) {
            slot.installed = false;
            fail("{} couldn't be hooked (the line above says why)", target.name);
        }

        game::Hook made;
        made.add([stub, &slot] {
            const raw::PluginApi& loader = detail::api();

            if (slot.installed) {
                loader.unhook(&loader, stub);
                slot.installed = false;
            }
        });
        remember_undo([made]() mutable { made.remove(); });

        return made;
    }
};

// The type of a lambda written with plain parameter types: [](void* character, bool show) { ... } is int(void*, bool).
template<class Member>
struct MemberSignature;

template<class Class, class Result, class... Parameters>
struct MemberSignature<Result (Class::*)(Parameters...) const> {
    using Type = Result(Parameters...);
};

template<class Class, class Result, class... Parameters>
struct MemberSignature<Result (Class::*)(Parameters...)> {
    using Type = Result(Parameters...);
};

template<class Class, class Result, class... Parameters>
struct MemberSignature<Result (Class::*)(Parameters...) const noexcept> {
    using Type = Result(Parameters...);
};

template<class Class, class Result, class... Parameters>
struct MemberSignature<Result (Class::*)(Parameters...) noexcept> {
    using Type = Result(Parameters...);
};

template<class Lambda>
concept PlainLambda = requires { &Lambda::operator(); };

template<class Lambda>
using SignatureOf = typename MemberSignature<decltype(&Lambda::operator())>::Type;

template<class Lambda>
consteval void check_plain_lambda() {
    static_assert(PlainLambda<Lambda>,
        "Write the lambda's parameter types (void* character, bool show), not auto: they tell mt2loader.hpp how the "
        "game calls the function. For a lambda that calls the game's own function, use .hook<Type>([](auto original, ...) {...})");
}

template<class Signature, class Detour>
game::Hook hook_with(const HookTarget& target, const Detour& detour) {
    static_assert(std::is_class_v<Detour>, "Give a lambda: [](auto original, void* object, ...) { ... }");

    return HookPool<Signature, Detour>::install(target, detour);
}

template<class Signature>
struct Actions;

// replace, after and returns, for a function or a call with this type.
template<class Result, class... Parameters>
struct Actions<Result(Parameters...)> {
    template<class Replacement>
    static game::Hook replace(const HookTarget& target, Replacement replacement) {
        auto detour = [replacement](game::Original<Result(Parameters...)>, Parameters... arguments) -> Result {
            return replacement(std::forward<Parameters>(arguments)...);
        };

        return hook_with<Result(Parameters...)>(target, detour);
    }
};

template<class Callback>
game::Hook replace(const HookTarget& target, Callback callback) {
    check_plain_lambda<Callback>();

    return Actions<SignatureOf<Callback>>::replace(target, std::move(callback));
}

template<class Signature>
struct BeforeActions {
    static_assert(always_false<Signature>, "A lambda for .before returns nothing: it runs, then the game's function runs "
        "as usual. To change the result, use .after; to skip the game's function, .replace");

    template<class Callback>
    static game::Hook install(const HookTarget&, Callback) {
        return {};
    }
};

template<class... Parameters>
struct BeforeActions<void(Parameters...)> {
    template<class Callback>
    static game::Hook install(const HookTarget& target, Callback callback) {
        return BeforePool<Callback, Parameters...>::install(target, callback);
    }
};

template<class Callback>
game::Hook before(const HookTarget& target, Callback callback) {
    check_plain_lambda<Callback>();

    return BeforeActions<SignatureOf<Callback>>::install(target, std::move(callback));
}

inline void report_after_failure(std::atomic<bool>& logged, const std::string& target_name, const char* problem) {
    if (!logged.exchange(true)) {
        plugin::log("The code after {} failed: {}. The game's own result stands (later failures aren't logged)", target_name, problem);
    }
}

// after: [](int result, void* character, bool show) { return result + 1; } for a function that returns int, or
// [](void* character, bool show) { ... } for one that returns nothing.
template<class Signature, class Callback>
struct AfterActions {
    static_assert(always_false<Signature>, "A lambda for .after gets the function's result first and returns the result "
        "to use: [](int result, void* object) { return result; }. For a function that returns nothing, it returns nothing");

    static game::Hook install(const HookTarget&, Callback) {
        return {};
    }
};

template<class Result, class First, class... Rest, class Callback>
    requires (!std::is_void_v<Result>)
struct AfterActions<Result(First, Rest...), Callback> {
    static_assert(std::is_same_v<std::remove_cvref_t<First>, Result>,
        "A lambda for .after gets the function's result first and returns the result to use: "
        "[](int result, void* object) { return result; }. For a function that returns nothing, the lambda returns nothing");

    static game::Hook install(const HookTarget& target, Callback callback) {
        auto failure_logged = std::make_shared<std::atomic<bool>>(false);
        auto detour = [callback, failure_logged, name = target.name](game::Original<Result(Rest...)> original, Rest... arguments) -> Result {
            Result result = original(arguments...);

            try {
                return callback(result, arguments...);
            } catch (const std::exception& error) {
                report_after_failure(*failure_logged, name, error.what());
            } catch (...) {
                report_after_failure(*failure_logged, name, "an error that isn't a std::exception");
            }

            return result;
        };

        return hook_with<Result(Rest...)>(target, detour);
    }
};

template<class... Parameters, class Callback>
struct AfterActions<void(Parameters...), Callback> {
    static game::Hook install(const HookTarget& target, Callback callback) {
        auto detour = [callback](game::Original<void(Parameters...)> original, Parameters... arguments) {
            original(arguments...);
            callback(arguments...);
        };

        return hook_with<void(Parameters...)>(target, detour);
    }
};

template<class Callback>
game::Hook after(const HookTarget& target, Callback callback) {
    check_plain_lambda<Callback>();

    return AfterActions<SignatureOf<Callback>, Callback>::install(target, std::move(callback));
}

template<class Value>
game::Hook returns(const HookTarget& target, Value value) {
    auto detour = [value](game::Original<Value()>) -> Value {
        return value;
    };
    // The parameters are left out on purpose: the value is returned whatever they are.
    HookTarget without_check = target;
    without_check.function = game::Address();

    return hook_with<Value()>(without_check, detour);
}

struct Place {
    game::Address address;
    raw::Instruction instruction;
};

// Every instruction of a function, decoded.
inline std::vector<Place> decode_all(game::Address start, std::size_t size) {
    const raw::PluginApi& api = detail::api();
    std::vector<Place> places;
    game::Address position = start;

    require_loader_0_6();

    while (position < start + static_cast<std::ptrdiff_t>(size)) {
        Place place{ position, {} };

        if (!api.decode(&api, position.get(), &place.instruction)) {
            break;
        }

        places.push_back(place);
        position = position + place.instruction.length;
    }

    return places;
}

inline bool is_text_at(game::Address address, std::string_view text) {
    const raw::PluginApi& api = detail::api();
    std::vector<char> bytes(text.size() + 1);

    return api.read(&api, address.get(), bytes.data(), bytes.size()) && std::string_view(bytes.data(), text.size()) == text
        && bytes[text.size()] == '\0';
}

inline bool uses_text(const Place& place, std::string_view text) {
    return place.instruction.reference != nullptr && !place.instruction.is_call && !place.instruction.is_jump
        && is_text_at(place.instruction.reference, text);
}

inline std::string without_spaces(std::string_view text) {
    std::string result;

    for (char character : text) {
        if (character != ' ') {
            result += character;
        }
    }

    return result;
}

// "bool std::operator==<char, ...>(std::string const&, char const*)" -> "std::operator==": what a modder writes.
inline std::string plain_name(std::string_view name) {
    std::size_t close = name.rfind(')');
    int depth = 0;

    if (close != std::string_view::npos) {
        for (std::size_t index = close + 1; index-- > 0;) {
            if (name[index] == ')') {
                depth++;
            } else if (name[index] == '(' && --depth == 0) {
                name = name.substr(0, index);

                break;
            }
        }
    }

    std::size_t start = 0;
    depth = 0;

    for (std::size_t index = 0; index < name.size(); index++) {
        char character = name[index];

        if (character == '<') {
            depth++;
        } else if (character == '>' && depth > 0) {
            depth--;
        } else if (character == ' ' && depth == 0 && !name.substr(0, index).ends_with("operator")) {
            start = index + 1;
        }
    }

    name = name.substr(start);

    bool is_operator_with_angle = name.ends_with("operator>") || name.ends_with("operator>>") || name.ends_with("operator->");

    if (name.ends_with('>') && !is_operator_with_angle) {
        depth = 0;

        for (std::size_t index = name.size(); index-- > 0;) {
            if (name[index] == '>') {
                depth++;
            } else if (name[index] == '<' && --depth == 0) {
                name = name.substr(0, index);

                break;
            }
        }
    }

    return without_spaces(name);
}

inline bool names_match(std::string_view readable, std::string_view wanted) {
    std::string wanted_plain = without_spaces(wanted);

    if (wanted_plain.find('(') != std::string::npos) {
        std::string full = without_spaces(readable);

        return full == wanted_plain || full.ends_with(" " + wanted_plain) || full.ends_with(wanted_plain);
    }

    return plain_name(readable) == wanted_plain;
}

}


namespace game {

class Calls;
class Texts;
class Numbers;

/*
    One game function, to change: as a whole, or a call, text or number inside it. What game::in returns.

        game::in("mmoCharacter::EquipWeaponModel").after([](int result, void* character, bool show) { ... });
        game::in("mmoCharacter::EquipWeaponModel").call("std::operator==").with_text("humanoid").returns(true);
        game::in("mmoRoster::AddHero").number(8).becomes(16);
*/
class Code {
public:
    explicit Code(Address function) : start(function) {
        const plugin::raw::PluginApi& api = plugin::detail::api();
        void* symbol_start = nullptr;
        std::size_t symbol_size = 0;

        plugin::detail::require_loader_0_6();

        if (api.function_extent(&api, function.get(), &symbol_start, &symbol_size) && symbol_start == function.get()
            && symbol_size > 0 && symbol_size < max_size) {
            length = symbol_size;
        }
    }

    Address address() const {
        return start;
    }

    std::size_t size() const {
        return length;
    }

    std::string name() const {
        return start.name().empty() ? start.describe() : start.name();
    }

    // The game's function doesn't run; this lambda runs instead, with the same parameters:
    //     .replace([](void* character, bool show) { return 0; })
    template<class Replacement>
    Hook replace(Replacement replacement) const {
        return plugin::detail::replace(target(), std::move(replacement));
    }

    // Runs first, with the function's arguments; then the game's function runs as usual.
    //     .before([](void* character, bool show) { plugin::log("equipping"); })
    template<class Callback>
    Hook before(Callback callback) const {
        return plugin::detail::before(target(), std::move(callback));
    }

    // Runs after the game's function, with its result first, and returns the result the game gets:
    //     .after([](int result, void* character, bool show) { return result; })
    // For a function that returns nothing, the lambda takes just the parameters.
    template<class Callback>
    Hook after(Callback callback) const {
        return plugin::detail::after(target(), std::move(callback));
    }

    // The function always returns value, without running.
    template<class Value>
    Hook returns(Value value) const {
        return plugin::detail::returns(target(), std::move(value));
    }

    // The advanced way, with the type written out: the lambda gets `auto original` first, to call the game's function
    // when and how it wants. .hook<int(void* character, bool show)>([](auto original, void* character, bool show) {...})
    template<class Signature, class Detour>
    Hook hook(Detour detour) const {
        return plugin::detail::hook_with<Signature>(target(), detour);
    }

    // A call this function makes, to the function named (as mt2sdk find shows it, with or without parameters).
    Calls call(std::string_view function) const;

    // A text this function uses, like "humanoid".
    Texts text(std::string_view text) const;

    // A number written in this function's code, like the 8 in `if (count < 8)`.
    Numbers number(std::int64_t value) const;

private:
    // No function is this long; a size from the symbol table beyond it means the symbol is something else.
    static constexpr std::size_t max_size = 1024 * 1024;

    plugin::detail::HookTarget target() const {
        return { start, false, name(), start };
    }

    Address start;
    std::size_t length = search_range;
};

inline Code in(Address function) {
    return Code(function);
}

// The game function to change, by name: game::in("mmoCharacter::EquipWeaponModel")
inline Code in(std::string_view function) {
    return Code(find(function));
}

inline Code in(const char* function) {
    return in(std::string_view(function));
}

}


namespace plugin::detail {

// Picks the places a selection means: exactly one unless told .nth(n) or .every(), with the reason when it can't.
inline std::vector<game::Address> choose(const std::vector<game::Address>& found, int number, bool every,
    const game::Code& code, const std::string& one, const std::string& several) {
    if (found.empty()) {
        fail("{} has no {}", code.name(), one);
    }

    if (every) {
        return found;
    }

    if (number > 0) {
        if (static_cast<std::size_t>(number) > found.size()) {
            fail("{} has only {} {}, so there's no number {}", code.name(), found.size(), found.size() == 1 ? one : several, number);
        }

        return { found[static_cast<std::size_t>(number) - 1] };
    }

    if (found.size() > 1) {
        std::string offsets;

        for (const game::Address& place : found) {
            offsets += std::format("{}+0x{:x}", offsets.empty() ? "" : ", ", place - code.address());
        }

        fail("{} has {} {} (at {}): pick one with .nth(1) to .nth({}), or all of them with .every()", code.name(),
            found.size(), several, offsets, found.size());
    }

    return found;
}

}


namespace game {

/*
    Calls a function makes to another one, to change what they do. From game::in(...).call("name"):

        game::in("mmoCharacter::EquipWeaponModel").call("std::operator==").with_text("humanoid").returns(true);

    It must pick exactly one call, unless .nth(n) picks the n-th or .every() takes all of them; otherwise the plugin
    doesn't start, and the log says how many there are and where. Then, as for a whole function: returns, replace,
    before, after, or hook<Type> (the lambda gets the called function's parameters).
*/
class Calls {
public:
    Calls(Code in, std::string_view function) : code(std::move(in)), called_function(function) {}

    // Only calls that pass this text (it's used between the call before and this one).
    Calls with_text(std::string_view text) const {
        Calls narrowed = *this;
        narrowed.passed_text = text;

        return narrowed;
    }

    // The n-th of the calls, counting from 1.
    Calls nth(int number) const {
        Calls narrowed = *this;
        narrowed.chosen_number = number;

        return narrowed;
    }

    Calls every() const {
        Calls narrowed = *this;
        narrowed.take_all = true;

        return narrowed;
    }

    template<class Value>
    Hook returns(Value value) const {
        return for_each([&](const plugin::detail::HookTarget& target) { return plugin::detail::returns(target, value); });
    }

    template<class Replacement>
    Hook replace(Replacement replacement) const {
        return for_each([&](const plugin::detail::HookTarget& target) { return plugin::detail::replace(target, replacement); });
    }

    template<class Callback>
    Hook before(Callback callback) const {
        return for_each([&](const plugin::detail::HookTarget& target) { return plugin::detail::before(target, callback); });
    }

    template<class Callback>
    Hook after(Callback callback) const {
        return for_each([&](const plugin::detail::HookTarget& target) { return plugin::detail::after(target, callback); });
    }

    template<class Signature, class Detour>
    Hook hook(Detour detour) const {
        return for_each([&](const plugin::detail::HookTarget& target) { return plugin::detail::hook_with<Signature>(target, detour); });
    }

    // The call instructions picked, for going deeper.
    std::vector<Address> places() const {
        std::string passing = passed_text.empty() ? "" : std::format(" with the text \"{}\"", passed_text);

        return plugin::detail::choose(matching(), chosen_number, take_all, code, std::format("call to {}{}", called_function, passing),
            std::format("calls to {}{}", called_function, passing));
    }

private:
    std::vector<Address> matching() const {
        std::vector<Address> found;
        bool text_since_last_call = false;

        for (const plugin::detail::Place& place : plugin::detail::decode_all(code.address(), code.size())) {
            const plugin::raw::Instruction& instruction = place.instruction;

            if (!passed_text.empty() && plugin::detail::uses_text(place, passed_text)) {
                text_since_last_call = true;
            }

            bool is_direct = (instruction.is_call || instruction.is_jump) && instruction.reference != nullptr
                && instruction.reference_size == 4 && instruction.length == 5;

            if (!is_direct) {
                continue;
            }

            Address called(instruction.reference);
            bool is_other_function = called < code.address() || called >= code.address() + static_cast<std::ptrdiff_t>(code.size());
            bool wanted = is_other_function && plugin::detail::names_match(called.name(), called_function) && (passed_text.empty() || text_since_last_call);

            if (wanted) {
                found.push_back(place.address);
            }

            if (instruction.is_call) {
                text_since_last_call = false;
            }
        }

        return found;
    }

    template<class Install>
    Hook for_each(Install install) const {
        Hook all_hooks;

        for (const Address& call : places()) {
            Address called = call.target();
            plugin::detail::HookTarget target{ call, true, std::format("the call to {} in {}", called.name(), call.describe()), called };
            Hook made = install(target);
            all_hooks.add([made]() mutable { made.remove(); });
        }

        return all_hooks;
    }

    Code code;
    std::string called_function;
    std::string passed_text;
    int chosen_number = 0;
    bool take_all = false;
};

/*
    A text a function uses, to change it. From game::in(...).text("humanoid"):

        game::in("mmoCostumeEditorView::_UpdateGrid").text("not humanoid").becomes("no weapons for this rig");

    Picks exactly one place unless told .nth(n) or .every(). The new text gets its own memory near the game's code.
*/
class Texts {
public:
    Texts(Code in, std::string_view text) : code(std::move(in)), used_text(text) {}

    Texts nth(int number) const {
        Texts narrowed = *this;
        narrowed.chosen_number = number;

        return narrowed;
    }

    Texts every() const {
        Texts narrowed = *this;
        narrowed.take_all = true;

        return narrowed;
    }

    std::vector<Address> places() const {
        std::vector<Address> found;

        for (const plugin::detail::Place& place : plugin::detail::decode_all(code.address(), code.size())) {
            if (place.instruction.reference_size == 4 && plugin::detail::uses_text(place, used_text)) {
                found.push_back(place.address);
            }
        }

        return plugin::detail::choose(found, chosen_number, take_all, code, std::format("use of the text \"{}\"", used_text),
            std::format("uses of the text \"{}\"", used_text));
    }

    Patch becomes(std::string_view new_text) const {
        const plugin::raw::PluginApi& api = plugin::detail::api();
        std::vector<Address> chosen = places();
        std::vector<plugin::detail::Place> all_places = plugin::detail::decode_all(code.address(), code.size());

        if (new_text.size() != used_text.size()) {
            check_no_length_nearby(chosen, all_places);
        }

        auto* copy = static_cast<char*>(api.allocate_near(&api, new_text.size() + 1));

        if (copy == nullptr) {
            plugin::fail("There's no memory near the game's code for the text \"{}\"", new_text);
        }

        std::memcpy(copy, new_text.data(), new_text.size());
        copy[new_text.size()] = '\0';

        Patch made;

        for (const Address& place : chosen) {
            raw_retarget(place, Address(static_cast<void*>(copy)), made);
            retarget_text_end(place, all_places, Address(static_cast<void*>(copy + new_text.size())), made);
        }

        plugin::log("The text \"{}\" in {} is now \"{}\"", used_text, code.name(), new_text);

        return made;
    }

private:
    static constexpr int nearby_instructions = 8;

    std::size_t index_of(const Address& place, const std::vector<plugin::detail::Place>& all_places) const {
        for (std::size_t index = 0; index < all_places.size(); index++) {
            if (all_places[index].address == place) {
                return index;
            }
        }

        return all_places.size();
    }

    // Code that builds a std::string from a text often has the text's length written next to it.
    void check_no_length_nearby(const std::vector<Address>& chosen, const std::vector<plugin::detail::Place>& all_places) const {
        for (const Address& place : chosen) {
            std::size_t index = index_of(place, all_places);
            std::size_t first = index > nearby_instructions ? index - nearby_instructions : 0;
            std::size_t last = std::min(all_places.size(), index + nearby_instructions + 1);

            for (std::size_t other = first; other < last; other++) {
                const plugin::raw::Instruction& instruction = all_places[other].instruction;

                if (instruction.immediate_size > 0 && !instruction.adjusts_stack && instruction.immediate == static_cast<std::int64_t>(used_text.size())) {
                    plugin::fail("{} seems to use the length of \"{}\" ({}) next to it, at {}: a text of another length could "
                        "be cut short or read too far. Use one of {} characters, or change the length too with .number({})",
                        code.name(), used_text, used_text.size(), all_places[other].address.describe(), used_text.size(), used_text.size());
                }
            }
        }
    }

    static void raw_retarget(Address place, Address new_target, Patch& made) {
        const plugin::raw::PluginApi& api = plugin::detail::api();
        plugin::raw::Instruction instruction;

        api.decode(&api, place.get(), &instruction);

        Address offset_at = place + instruction.reference_offset;
        std::int64_t distance = new_target - (place + instruction.length);

        if (distance < INT32_MIN || distance > INT32_MAX) {
            plugin::fail("The new text is too far from {} to be used there", place.describe());
        }

        auto offset = static_cast<std::int32_t>(distance);
        std::array<std::uint8_t, 4> bytes;
        std::memcpy(bytes.data(), &offset, bytes.size());

        made.add(patch(offset_at, std::span<const std::uint8_t>(bytes)));
    }

    // Code that copies a text sometimes also points at its end: that pointer moves with it.
    void retarget_text_end(Address place, const std::vector<plugin::detail::Place>& all_places, Address new_end, Patch& made) const {
        const plugin::raw::PluginApi& api = plugin::detail::api();
        plugin::raw::Instruction instruction;

        api.decode(&api, place.get(), &instruction);

        Address old_end = Address(instruction.reference) + static_cast<std::ptrdiff_t>(used_text.size());
        std::size_t index = index_of(place, all_places);
        std::size_t last = std::min(all_places.size(), index + nearby_instructions + 1);

        for (std::size_t other = index + 1; other < last; other++) {
            const plugin::detail::Place& candidate = all_places[other];

            if (candidate.instruction.reference_size == 4 && !candidate.instruction.is_call && !candidate.instruction.is_jump
                && Address(candidate.instruction.reference) == old_end) {
                raw_retarget(candidate.address, new_end, made);
            }
        }
    }

    Code code;
    std::string used_text;
    int chosen_number = 0;
    bool take_all = false;
};

/*
    A number written in a function's code, to change it. From game::in(...).number(8):

        game::in("mmoRoster::CanHire").number(8).becomes(16);

    Only numbers the code itself holds (cmp eax, 8), not stack bookkeeping. Picks exactly one place unless told
    .nth(n) or .every(). The new number must fit where the old one was: the log says the range if it doesn't.
*/
class Numbers {
public:
    Numbers(Code in, std::int64_t value) : code(std::move(in)), used_value(value) {}

    Numbers nth(int number) const {
        Numbers narrowed = *this;
        narrowed.chosen_number = number;

        return narrowed;
    }

    Numbers every() const {
        Numbers narrowed = *this;
        narrowed.take_all = true;

        return narrowed;
    }

    std::vector<Address> places() const {
        std::vector<Address> found;

        for (const plugin::detail::Place& place : plugin::detail::decode_all(code.address(), code.size())) {
            const plugin::raw::Instruction& instruction = place.instruction;

            if (instruction.immediate_size > 0 && !instruction.adjusts_stack && instruction.immediate == used_value) {
                found.push_back(place.address);
            }
        }

        return plugin::detail::choose(found, chosen_number, take_all, code, std::format("use of the number {}", used_value),
            std::format("uses of the number {}", used_value));
    }

    Patch becomes(std::int64_t new_value) const {
        const plugin::raw::PluginApi& api = plugin::detail::api();
        Patch made;

        for (const Address& place : places()) {
            plugin::raw::Instruction instruction;
            api.decode(&api, place.get(), &instruction);

            std::uint32_t size = instruction.immediate_size;
            std::int64_t lowest = size == 8 ? INT64_MIN : -(std::int64_t{ 1 } << (8 * size - 1));
            std::int64_t highest = size == 8 ? INT64_MAX : (std::int64_t{ 1 } << (8 * size - 1)) - 1;

            if (new_value < lowest || new_value > highest) {
                plugin::fail("The number at {} is stored in {} byte{}, so it can be from {} to {}, not {}", place.describe(), size,
                    size == 1 ? "" : "s", lowest, highest, new_value);
            }

            std::array<std::uint8_t, 8> bytes;
            std::memcpy(bytes.data(), &new_value, bytes.size());

            made.add(patch(place + instruction.immediate_offset, std::span<const std::uint8_t>(bytes.data(), size)));
        }

        plugin::log("The number {} in {} is now {}", used_value, code.name(), new_value);

        return made;
    }

private:
    Code code;
    std::int64_t used_value;
    int chosen_number = 0;
    bool take_all = false;
};

inline Calls Code::call(std::string_view function) const {
    return Calls(*this, function);
}

inline Texts Code::text(std::string_view text) const {
    return Texts(*this, text);
}

inline Numbers Code::number(std::int64_t value) const {
    return Numbers(*this, value);
}

/*
    The advanced way to hook a whole function, with its type written out, when the lambda needs to call the game's
    function itself (the easy ways are game::in(...).before, .after, .replace):

        game::hook<int(void* character, bool show)>("mmoCharacter::EquipWeaponModel", [](auto original, void* character, bool show) {
            return original(character, show);
        });

    Several plugins can hook one function: the last hook runs first, and its original leads to the one before.
    If the lambda throws, the log says so and the game's function runs instead.
*/
template<class Signature, class Detour>
Hook hook(std::string_view name, Detour detour) {
    Address function = find(name);

    return plugin::detail::hook_with<Signature>({ function, false, function.name(), function }, detour);
}

template<class Signature, class Detour>
Hook hook(Address function, Detour detour) {
    std::string name = function.name().empty() ? function.describe() : function.name();

    return plugin::detail::hook_with<Signature>({ function, false, name, function }, detour);
}

/*
    A list of game objects that a game object keeps (the engine's object arrays), seen in place:

        for (void* subscriber : game::field<game::Objects>(manager, "mmoSubscriberManager::subscriber")) { ... }

    add() appends one, as the game does: a list that owns its objects (a vsArrayStore) deletes it with the rest.
    reserve() makes room first, for a list other threads may be reading.
*/
class Objects {
public:
    Objects(const Objects&) = delete;
    Objects& operator=(const Objects&) = delete;

    void* const* begin() const {
        return items;
    }

    void* const* end() const {
        return items + size();
    }

    std::size_t size() const {
        return count > 0 ? static_cast<std::size_t>(count) : 0;
    }

    bool empty() const {
        return size() == 0;
    }

    void* operator[](std::size_t index) const {
        if (index >= size()) {
            plugin::fail("There's no object number {} in a list of {}", index, size());
        }

        return items[index];
    }

    // Room grows as the game grows it (twice as much, at least 4), on the game's heap.
    void add(void* object) {
        if (count >= capacity) {
            grow(std::max(capacity * 2, 4));
        }

        items[count++] = object;
    }

    // Room for this many, so adding up to that never moves the list: code on another thread reading it meanwhile
    // (the game loading on its workers) never sees it moved.
    void reserve(std::size_t room) {
        if (room > static_cast<std::size_t>(capacity)) {
            grow(static_cast<std::int32_t>(room));
        }
    }

private:
    Objects() = default;

    void grow(std::int32_t room) {
        static Function<void**(std::size_t size)> allocate{ "_Znay" };
        static Function<void(void** items)> free{ "_ZdaPv" };

        void** grown = allocate(static_cast<std::size_t>(room) * sizeof(void*));
        std::copy(items, items + size(), grown);

        if (items != nullptr) {
            free(items);
        }

        items = grown;
        capacity = room;
    }

    // As the engine lays out vsArrayStore: its class, the objects, how many, and room for how many.
    void* array_class = nullptr;
    void** items = nullptr;
    std::int32_t count = 0;
    std::int32_t capacity = 0;
};

/*
    A link a game object keeps to another object that can go away (the engine's vsWeakObjectLink), seen in place:

        void* giver = game::field<game::Link>(quest, "mmoQuest::questGiver").get();

    get() is the object, or nullptr once it's gone (or if the link was never set).
*/
class Link {
public:
    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    void* get() const {
        if (proxy == nullptr || proxy->object == nullptr) {
            return nullptr;
        }

        return object;
    }

    explicit operator bool() const {
        return get() != nullptr;
    }

private:
    Link() = default;

    // What the linked object shares with every link to it: the object, or nullptr once it's gone.
    struct Proxy {
        void* object;
        std::int32_t links;
    };

    // As the engine lays out vsWeakObjectLink (read from vsWeakObjectLink<mmoNPC>::Resolve): its class, the id it
    // loads by, then a vsWeakPointer: its class, the object and the proxy.
    void* link_class = nullptr;
    std::byte loading[0x18] = {};
    void* pointer_class = nullptr;
    void* object = nullptr;
    Proxy* proxy = nullptr;
};

static_assert(sizeof(Link) == 0x38, "vsWeakObjectLink is 0x38 bytes in the game");

}


namespace plugin::detail {

// Where the game's property objects (vsProperty and kin) keep the offset of their field.
inline constexpr std::size_t property_offset_at = 0x38;

// "mmoSubscriber::spend_other" -> "mmoSubscriber", "spend_other"
inline std::pair<std::string, std::string> split_member(std::string_view name) {
    int depth = 0;
    std::size_t separator = std::string_view::npos;

    for (std::size_t index = 0; index + 1 < name.size(); index++) {
        depth += name[index] == '<' ? 1 : 0;
        depth -= name[index] == '>' ? 1 : 0;

        if (depth == 0 && name[index] == ':' && name[index + 1] == ':') {
            separator = index;
        }
    }

    if (separator == std::string_view::npos || separator == 0 || separator + 2 >= name.size()) {
        fail("'{}' isn't a class and a field, like \"mmoSubscriber::spend_other\"", name);
    }

    return { std::string(name.substr(0, separator)), std::string(name.substr(separator + 2)) };
}

// Each saved field has a property object: s_<name>Property, or, made at startup, one that s_<name>_property points to.
inline game::Address try_find_property(const std::string& owner, const std::string& member) {
    game::Address property = game::try_find(std::format("{}::s_{}Property", owner, member));

    if (property) {
        return property;
    }

    game::Address pointer = game::try_find(std::format("{}::s_{}_property", owner, member));

    return pointer ? game::Address(pointer.read<void*>()) : game::Address();
}

// The classes a class is built on that start where it starts (so their fields are at the same offsets), from the
// game's type information: "mmoToon" -> "vsObject<mmoToon, mmoCharacter>", then on to mmoCharacter, mmoProp...
inline std::vector<std::string> base_classes(const std::string& owner) {
    game::Address info = game::try_find(std::format("typeinfo for {}", owner));
    std::vector<game::Address> bases;

    if (!info) {
        return {};
    }

    std::string kind = game::Address(info.read<void*>()).name();

    if (kind.find("__si_class_type_info") != std::string::npos) {
        bases.push_back(game::Address((info + 16).read<void*>()));
    } else if (kind.find("__vmi_class_type_info") != std::string::npos) {
        auto base_count = (info + 20).read<std::uint32_t>();

        for (std::uint32_t index = 0; index < base_count && index < 64; index++) {
            auto base = (info + 24 + 16 * index).read<void*>();
            auto offset_and_flags = (info + 32 + 16 * index).read<std::int64_t>();
            bool is_virtual = (offset_and_flags & 1) != 0;

            if (!is_virtual && (offset_and_flags >> 8) == 0) {
                bases.push_back(game::Address(base));
            }
        }
    }

    std::vector<std::string> names;

    for (const game::Address& base : bases) {
        std::string name = base.name();
        constexpr std::string_view prefix = "typeinfo for ";

        if (name.starts_with(prefix)) {
            names.push_back(name.substr(prefix.size()));
        }
    }

    return names;
}

// The property object of a field, looked for in its class and the classes it's built on.
inline game::Address find_property(std::string_view name) {
    auto [owner, member] = split_member(name);
    std::vector<std::string> to_search{ owner };
    std::set<std::string> searched;

    while (!to_search.empty() && searched.size() < 64) {
        std::string current = to_search.front();
        to_search.erase(to_search.begin());

        if (!searched.insert(current).second) {
            continue;
        }

        if (game::Address property = try_find_property(current, member)) {
            return property;
        }

        for (std::string& base : base_classes(current)) {
            to_search.push_back(std::move(base));
        }
    }

    fail("{} has no saved field called '{}' (mt2sdk find \"{}::s_\" lists the ones it has)", owner, member, owner);
}

struct PropertyType {
    // "vsProperty", "vsPropertyObject", "vsPropertyObjectPointer"...
    std::string kind;
    // Its first template argument: "int", "vsVolatileObjectArray<mmoSubscriber>", "mmoToon"...
    std::string value;
};

// From the property object's class: "vtable for vsProperty<int, mmoSubscriber>".
inline PropertyType property_type(game::Address property, std::string_view name) {
    auto table = property.try_read<void*>();
    std::string class_name = table && *table != nullptr ? game::Address(*table).name() : std::string();
    constexpr std::string_view prefix = "vtable for ";

    if (!class_name.starts_with(prefix)) {
        fail("The game hasn't set up the field {} yet: use it once the game runs (in a hook), not while the plugin starts", name);
    }

    std::string_view rest = std::string_view(class_name).substr(prefix.size());
    std::size_t open = rest.find('<');

    if (open == std::string_view::npos) {
        return { std::string(rest), "" };
    }

    int depth = 0;
    std::size_t end = open + 1;

    for (; end < rest.size(); end++) {
        depth += rest[end] == '<' ? 1 : 0;
        depth -= rest[end] == '>' ? 1 : 0;

        if (depth < 0 || (depth == 0 && rest[end] == ',')) {
            break;
        }
    }

    return { std::string(rest.substr(0, open)), std::string(rest.substr(open + 1, end - open - 1)) };
}

struct BuiltinType {
    std::string_view name;
    std::size_t size;
    char kind;
};

// The game's plain types as its names write them: b(ool), i(nteger), f(loating point).
inline constexpr std::array<BuiltinType, 14> builtin_types{ {
    { "bool", 1, 'b' }, { "char", 1, 'i' }, { "signed char", 1, 'i' }, { "unsigned char", 1, 'i' },
    { "short", 2, 'i' }, { "unsigned short", 2, 'i' }, { "int", 4, 'i' }, { "unsigned int", 4, 'i' },
    { "long", 4, 'i' }, { "unsigned long", 4, 'i' }, { "long long", 8, 'i' }, { "unsigned long long", 8, 'i' },
    { "float", 4, 'f' }, { "double", 8, 'f' },
} };

// Stands for an object kept inside another (not a pointer to it), for game::object_in.
struct ObjectInside {};

// Catches a field read as the wrong type, where the game's names tell: game::field<float> of an int.
template<class T>
void check_field_type(std::string_view name, const PropertyType& type) {
    bool is_text = type.value.find("basic_string") != std::string::npos || type.value == "std::string";
    bool is_list = type.kind == "vsPropertyObject" && type.value.find("Array<") != std::string::npos;
    bool is_link = type.kind == "vsPropertyObject" && type.value.starts_with("vsWeakObjectLink<");

    if constexpr (std::is_same_v<T, game::String>) {
        if (!is_text) {
            fail("{} is of type {} in the game, not text", name, type.value);
        }
    } else if constexpr (std::is_same_v<T, game::Objects>) {
        if (!is_list) {
            fail("{} is of type {} in the game, not a list of objects", name, type.value);
        }
    } else if constexpr (std::is_same_v<T, game::Link>) {
        if (!is_link) {
            fail("{} is of type {} in the game, not a link to an object", name, type.value);
        }
    } else if constexpr (std::is_same_v<T, ObjectInside>) {
        if (type.kind != "vsPropertyObject" || is_list || is_link) {
            fail("{} is of type {} in the game, not an object kept inside another", name, type.value);
        }
    } else {
        if (is_text) {
            fail("{} is text in the game: read it as game::field<game::String>", name);
        }

        if (is_list) {
            fail("{} is a list of objects in the game: read it as game::field<game::Objects>", name);
        }

        if (is_link) {
            fail("{} is a link to an object that can go away in the game: read it as game::field<game::Link>", name);
        }

        // An object link holds a plain pointer once the game has resolved it (a saved id before that).
        if ((type.kind == "vsPropertyObjectPointer" || type.kind == "vsPropertyObjectLink") && !std::is_pointer_v<T>) {
            fail("{} is a pointer to {} in the game: read it as game::field<void*>", name, type.value);
        }

        if (type.kind == "vsPropertyObject" && std::is_pointer_v<T>) {
            fail("{} is a {} kept inside the object in the game, not a pointer: get it with game::object_in", name, type.value);
        }

        auto builtin = std::find_if(builtin_types.begin(), builtin_types.end(), [&](const BuiltinType& builtin_type) {
            return builtin_type.name == type.value;
        });

        if (builtin != builtin_types.end() && type.kind == "vsProperty") {
            bool same_kind = (builtin->kind == 'b' && std::is_same_v<T, bool>)
                || (builtin->kind == 'f' && std::is_floating_point_v<T>)
                || (builtin->kind == 'i' && (std::is_integral_v<T> || std::is_enum_v<T>) && !std::is_same_v<T, bool>);

            if (!same_kind || sizeof(T) != builtin->size) {
                fail("{} is of type {} in the game: read it as game::field<{}>", name, type.value, type.value);
            }
        }
    }
}

// Guards what the plugin looks up once and keeps (field offsets, singletons, taught enum words): hooks run on the
// game's worker threads too (a player's planning, loading and saving a game).
inline std::shared_mutex& lookups_lock() {
    static std::shared_mutex lock;

    return lock;
}

// Offsets found so far, per type read (so each name and type is checked once).
template<class T>
std::map<std::string, std::size_t, std::less<>>& field_offsets() {
    static std::map<std::string, std::size_t, std::less<>> offsets;

    return offsets;
}

template<class T>
std::size_t field_offset(std::string_view name) {
    auto& offsets = field_offsets<T>();

    {
        std::shared_lock reading(lookups_lock());
        auto found = offsets.find(name);

        if (found != offsets.end()) {
            return found->second;
        }
    }

    game::Address property = find_property(name);
    check_field_type<T>(name, property_type(property, name));

    auto offset = (property + static_cast<std::ptrdiff_t>(property_offset_at)).read<std::size_t>();
    std::unique_lock writing(lookups_lock());
    offsets.emplace(std::string(name), offset);

    return offset;
}

}


namespace game {

/*
    A field of a game object by the name the game saves it under, the class's name and the field's:

        int spent = game::field<int>(subscriber, "mmoSubscriber::spend_other");
        void* toon = game::field<void*>(subscriber, "mmoSubscriber::main");
        game::String& name = game::field<game::String>(toon, "mmoToon::name");

    The game says where each field is, so this keeps working when an update moves them. Fields of the classes a
    class is built on are found too (name belongs to mmoProp). Text is game::String, a list of objects is
    game::Objects, a pointer to an object is void*, a link to an object that can go away is game::Link; a type that
    doesn't match the game's stops with the reason. mt2sdk find "mmoSubscriber::s_" lists the names. Works once the
    game runs (in a hook, or on_ready and later).
*/
template<class T>
T& field(void* object, std::string_view name) {
    if (object == nullptr) {
        plugin::fail("{} of nothing (the object is nullptr)", name);
    }

    return field<T>(object, plugin::detail::field_offset<T>(name));
}

template<class T>
const T& field(const void* object, std::string_view name) {
    if (object == nullptr) {
        plugin::fail("{} of nothing (the object is nullptr)", name);
    }

    return field<T>(object, plugin::detail::field_offset<T>(name));
}

// An object the game keeps inside another (not a pointer to it), by the field's name, as a pointer to use like any
// other game object:
//     void* instance = game::object_in(record, "mmoQuestGiverProgress::quest");
inline void* object_in(void* object, std::string_view name) {
    if (object == nullptr) {
        plugin::fail("{} of nothing (the object is nullptr)", name);
    }

    return static_cast<std::byte*>(object) + plugin::detail::field_offset<plugin::detail::ObjectInside>(name);
}

// The one object of a class that the game keeps (a manager or a registry), or nullptr while there's none, as before
// a game is loaded:
//     void* subscribers = game::singleton("mmoSubscriberManager");
inline void* singleton(std::string_view class_name) {
    static std::map<std::string, Address, std::less<>> instances;

    {
        std::shared_lock reading(plugin::detail::lookups_lock());
        auto found = instances.find(class_name);

        if (found != instances.end()) {
            return found->second.read<void*>();
        }
    }

    Address instance = try_find(std::format("vsSingleton<{}>::s_instance", class_name));

    if (!instance) {
        plugin::fail("The game keeps no single {} (there's no vsSingleton<{}>::s_instance)", class_name, class_name);
    }

    std::unique_lock writing(plugin::detail::lookups_lock());
    instances.emplace(std::string(class_name), instance);

    return instance.read<void*>();
}

// A new game object, made the way the game makes one it loads from its files:
//     void* board = game::create("mmoLeaderboard");
// It's on the game's heap: hand it to the game, or delete it with game::destroy.
inline void* create(std::string_view class_name) {
    Function<void*(const void* factory)> make{ std::format("vsRTTIFactory<{}>::Create", class_name) };
    void* object = make(nullptr);

    if (object == nullptr) {
        plugin::fail("The game made no {}", class_name);
    }

    return object;
}

// A game object from a data file, the game's or a mod's, made as the game makes the ones it loads
// (vsObjectList::LoadFromFilename): the first object the file describes, or nullptr if there's no such file or object.
//     void* variant = game::load("gizmo/captive/base.variant");
// It's on the game's heap: hand it to the game (game::Objects::add), or delete it with game::destroy.
inline void* load(std::string_view path) {
    static Function<bool(const String& path)> exists{ "vsFile::Exists" };
    static Function<void(void* file, const String& path, int mode)> open{ "_ZN6vsFileC1ERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEENS_4ModeE" };
    static Function<void(void* file)> close{ "_ZN6vsFileD1Ev" };
    static Function<bool(void* file, void* record)> read{ "vsFile::Record" };
    static Function<void(void* record)> start_record{ "_ZN8vsRecordC1Ev" };
    static Function<void(void* record)> end_record{ "_ZN8vsRecordD1Ev" };
    static Function<void(void* context)> start_context{ "_ZN15vsObjectContextC1Ev" };
    static Function<void(void* context)> end_context{ "_ZN15vsObjectContextD1Ev" };
    static Function<const String&(const void* token)> label_of{ "vsToken::AsString" };
    static Function<void*(const String& name)> find_class{ "vsRTTI::Find" };
    // The class's factory makes one, and the object reads itself from the record: the game's own virtual calls.
    constexpr std::ptrdiff_t create_slot = 0x10;
    constexpr std::ptrdiff_t load_from_record_slot = 0x70;
    // Room for each, larger than the game's (0x90, 0x80 and 8 bytes on its stack).
    constexpr std::size_t room = 0x200;
    constexpr int read_mode = 0;

    String name(path);

    if (!exists(name)) {
        return nullptr;
    }

    alignas(16) std::array<std::byte, room> file{};
    alignas(16) std::array<std::byte, room> record{};
    alignas(16) std::array<std::byte, room> context{};
    open(file.data(), name, read_mode);
    start_record(record.data());
    start_context(context.data());
    void* object = nullptr;

    while (object == nullptr && read(file.data(), record.data())) {
        void* type = find_class(label_of(record.data()));
        void* create = type != nullptr ? (Address(*static_cast<void**>(type)) + create_slot).read<void*>() : nullptr;
        object = create != nullptr ? plugin::detail::call<void*, void*>(create, type) : nullptr;

        if (object != nullptr) {
            void* load_from_record = (Address(*static_cast<void**>(object)) + load_from_record_slot).read<void*>();
            plugin::detail::call<void, void*, void*, void*>(load_from_record, object, record.data(), context.data());
        }
    }

    end_context(context.data());
    end_record(record.data());
    close(file.data());

    return object;
}

// Deletes a game object with its class's own destructor, as the game would. For objects of the game's classes
// (everything game::create makes); nullptr is fine.
inline void destroy(void* object) {
    if (object == nullptr) {
        return;
    }

    Address table(Address(object).read<void*>());
    Address deleting((table + 8).read<void*>());
    std::string class_name = table.name();
    std::string destructor = deleting.name();

    if (!class_name.starts_with("vtable for ") || destructor.find("::~") == std::string::npos) {
        plugin::fail("{} isn't a game object with a destructor, so it can't be destroyed", Address(object).describe());
    }

    plugin::detail::call<void, void*>(deleting.get(), object);
}


/*
    The plugin's own values on a game object, kept in the saved game with it:

        game::Saved marker = game::saved(npc);
        marker.set("shape", "question");
        std::string shape = marker.get("shape", "exclamation");
        int uses = marker.get("uses", 0);

        game::saved().set("visits", 3);    // on the saved game as a whole

    true/false, numbers and text. They're written when the game saves the object, and back when the save loads:
    in a hook on the object's PostResolve (or later) they're there. They go away with the object. Each plugin
    sees only its own keys (1-64 characters of A-Z a-z 0-9 _ - .). The game skips them if it loads the save
    without the loader, and forgets them if it saves again then.
*/
class Saved {
public:
    explicit Saved(const void* object) : target(object) {
        if (object == nullptr) {
            plugin::fail("game::saved got no object (nullptr)");
        }
    }

    // The value, or fallback if there's none (or it doesn't read as T).
    template<class T>
        requires std::is_arithmetic_v<T> || std::is_same_v<T, std::string>
    T get(std::string_view key, T fallback) const {
        std::optional<std::string> text = read(key);

        if (!text) {
            return fallback;
        }

        return plugin::detail::value_from_text<T>(*text).value_or(fallback);
    }

    std::string get(std::string_view key, const char* fallback) const {
        return get<std::string>(key, fallback);
    }

    template<class T>
        requires std::is_arithmetic_v<T> || std::is_same_v<T, std::string>
    std::optional<T> find(std::string_view key) const {
        std::optional<std::string> text = read(key);

        if (!text) {
            return std::nullopt;
        }

        return plugin::detail::value_from_text<T>(*text);
    }

    bool has(std::string_view key) const {
        return read(key).has_value() || link(key) != nullptr;
    }

    template<class T>
        requires std::is_arithmetic_v<T>
    void set(std::string_view key, T value) {
        write(key, plugin::detail::text_from_value(value).c_str());
    }

    void set(std::string_view key, std::string_view text) {
        write(key, std::string(text).c_str());
    }

    void set(std::string_view key, const char* text) {
        write(key, text);
    }

    void erase(std::string_view key) {
        write(key, nullptr);
    }

    /*
        A link to another game object (a building, an NPC, a quest), kept in saved games as the game keeps its own
        links, and nullptr once that object is destroyed. link() gives the object as a whole (a building, not the
        quest target inside it). Needs loader 0.12.0.

            game::saved(quest).set_link("target.2", building);
            void* building = game::saved(quest).link("target.2");
    */
    void set_link(std::string_view key, const void* other) {
        const plugin::raw::PluginApi& api = plugin::detail::api();

        if (!plugin::detail::loader_at_least(0, 12)) {
            plugin::fail("Links in saved games need MT2 Loader 0.12.0 or newer (this is {})", plugin::loader_version());
        }

        if (!api.saved_set_link(&api, target, std::string(key).c_str(), other)) {
            plugin::fail("Couldn't keep the link '{}' on {} in the saved game (the line above says why)", key, Address(target).describe());
        }
    }

    void* link(std::string_view key) const {
        const plugin::raw::PluginApi& api = plugin::detail::api();

        if (!plugin::detail::loader_at_least(0, 12)) {
            return nullptr;
        }

        return api.saved_get_link(&api, target, std::string(key).c_str());
    }

    std::vector<std::string> keys() const {
        const plugin::raw::PluginApi& api = plugin::detail::api();
        std::string text(api.saved_keys(&api, target, nullptr, 0), '\0');
        api.saved_keys(&api, target, text.data(), text.size());

        std::vector<std::string> found;
        std::size_t start = 0;

        for (std::size_t end = text.find('\n'); end != std::string::npos; end = text.find('\n', start)) {
            found.emplace_back(text.substr(start, end - start));
            start = end + 1;
        }

        return found;
    }

    const void* object() const {
        return target;
    }

private:
    std::optional<std::string> read(std::string_view key) const {
        const plugin::raw::PluginApi& api = plugin::detail::api();
        std::string name(key);
        // Most values are short, and most reads find none: those take no allocation.
        std::array<char, 256> buffer;
        std::size_t length = 0;

        if (!api.saved_get(&api, target, name.c_str(), buffer.data(), buffer.size(), &length)) {
            return std::nullopt;
        }

        if (length < buffer.size()) {
            return std::string(buffer.data(), length);
        }

        std::string value;

        // A value another thread lengthens in between is read again; one it erases is gone.
        while (length >= value.size()) {
            value.assign(length + 1, '\0');

            if (!api.saved_get(&api, target, name.c_str(), value.data(), value.size(), &length)) {
                return std::nullopt;
            }
        }

        value.resize(length);

        return value;
    }

    void write(std::string_view key, const char* value) {
        const plugin::raw::PluginApi& api = plugin::detail::api();
        std::string name(key);

        if (!api.saved_set(&api, target, name.c_str(), value)) {
            plugin::fail("Couldn't keep '{}' on {} in the saved game (the line above says why)", key, Address(target).describe());
        }
    }

    const void* target;
};

inline Saved saved(const void* object) {
    return Saved(object);
}

/*
    One of the game's records: a label and the words after it. What a window gets in UICommand, from a button
    whose command is "MessageTo Window QuestMarker shape star":

        game::in("mmoNPCInfoWindow::UICommand").after([](bool handled, void* window, game::Record command) {
            if (command.label() == "QuestMarker") {
                std::string what = command.text(0);    // "shape"
            }

            return handled;
        });
*/
class Record {
public:
    Record(const void* record) : target(record) {}

    std::string label() const {
        return std::string(text_of(target).view());
    }

    int size() const {
        // Where the game keeps a record's word count (vsRecord's token list).
        return (Address(target) + 0x40).read<int>();
    }

    // The word at index, or "" past the end.
    std::string text(int index) const {
        if (index < 0 || index >= size()) {
            return "";
        }

        return std::string(text_of(token_of(target, index)).view());
    }

    std::optional<int> number(int index) const {
        return plugin::detail::value_from_text<int>(text(index));
    }

    const void* get() const {
        return target;
    }

private:
    static inline Function<const void*(const void* record, int index)> token_of{ "vsRecord::GetToken" };
    static inline Function<const String&(const void* token)> text_of{ "vsToken::AsString" };

    const void* target;
};

// Whether a game is started or loaded, so game::saved() has one to keep values in.
inline bool is_game_open() {
    const plugin::raw::PluginApi& api = plugin::detail::api();

    return api.saved_root(&api) != nullptr;
}

// The saved game as a whole (its game state): for values that belong to no one object. Only while a game is open.
inline Saved saved() {
    const plugin::raw::PluginApi& api = plugin::detail::api();
    void* root = api.saved_root(&api);

    if (root == nullptr) {
        plugin::fail("There's no saved game open (game::saved() works once a game is started or loaded)");
    }

    return Saved(root);
}

class Enumeration;

}


namespace plugin::detail {

// The words each enum was taught with game::enumeration(...).add, by the enum's name.
inline std::map<std::string, std::map<std::string, int, std::less<>>, std::less<>>& added_enum_values() {
    static std::map<std::string, std::map<std::string, int, std::less<>>, std::less<>> values;

    return values;
}

inline std::optional<int> added_enum_value(const std::string& enum_name, std::string_view text) {
    std::shared_lock reading(lookups_lock());
    const auto& all = added_enum_values();
    auto values = all.find(enum_name);

    if (values == all.end()) {
        return std::nullopt;
    }

    auto found = values->second.find(text);

    return found == values->second.end() ? std::nullopt : std::optional<int>(found->second);
}

inline std::optional<std::string> added_enum_word(const std::string& enum_name, int value) {
    std::shared_lock reading(lookups_lock());
    const auto& all = added_enum_values();
    auto values = all.find(enum_name);

    if (values == all.end()) {
        return std::nullopt;
    }

    for (const auto& [word, added] : values->second) {
        if (added == value) {
            return word;
        }
    }

    return std::nullopt;
}

struct EnumTokens {
    game::Function<void*(const void* record, int index)> of_record{ "vsRecord::GetToken" };
    game::Function<void*(void* writer, int index)> of_writer{ "vsRecordWriter::GetToken" };
    game::Function<void*(void* reader)> record_of_reader{ "vsRecordReader::Get()" };
    game::Function<const game::String&(const void* token)> text{ "vsToken::AsString" };
    game::Function<void(void* token, const game::String& text)> set_text{ "vsToken::SetString" };
};

// Looked up the first time an enum is taught, so plugins that add none don't need these.
inline const EnumTokens& enum_tokens() {
    static const EnumTokens tokens;

    return tokens;
}

// The game writes a value it doesn't know as an empty word: the added word goes in its place.
inline void write_added_word(const std::string& enum_name, int value, void* token) {
    if (std::optional<std::string> word = added_enum_word(enum_name, value)) {
        enum_tokens().set_text(token, game::String(*word));
    }
}

// A word the game couldn't read: the added value, if it's an added word.
inline bool read_added_word(const std::string& enum_name, const void* token, int* value) {
    std::optional<int> added = added_enum_value(enum_name, enum_tokens().text(token));

    if (added) {
        *value = *added;
    }

    return added.has_value();
}

inline int* property_field(const void* property, void* object) {
    auto offset = *reinterpret_cast<const std::size_t*>(static_cast<const std::byte*>(property) + property_offset_at);

    return reinterpret_cast<int*>(static_cast<std::byte*>(object) + offset);
}

// The fields of this enum type that objects save (vsProperty<T, Owner>): some convert the word themselves.
inline void teach_enum_properties(const std::string& enum_name) {
    for (const std::string& name : game::find_names(std::format("vsProperty<{}, ", enum_name))) {
        if (name.find(">::Load(") != std::string::npos) {
            game::in(name).after([enum_name](bool loaded, const void* property, void* object, void* record, void*) {
                return loaded || read_added_word(enum_name, enum_tokens().of_record(record, 0), property_field(property, object));
            });
        } else if (name.find(">::LoadStream(") != std::string::npos) {
            game::in(name).after([enum_name](bool loaded, const void* property, void* object, void* reader, void*) {
                void* token = enum_tokens().of_record(enum_tokens().record_of_reader(reader), 0);

                return loaded || read_added_word(enum_name, token, property_field(property, object));
            });
        } else if (name.find(">::Save(") != std::string::npos) {
            game::in(name).after([enum_name](bool saved, const void* property, void* object, void* record, void*) {
                write_added_word(enum_name, *property_field(property, object), enum_tokens().of_record(record, 0));

                return saved;
            });
        } else if (name.find(">::SaveStream(") != std::string::npos) {
            game::in(name).after([enum_name](bool saved, const void* property, void* object, void* writer, void*) {
                write_added_word(enum_name, *property_field(property, object), enum_tokens().of_writer(writer, 0));

                return saved;
            });
        }
    }
}

// Everywhere the game turns this enum into its word and back: its data files, and the saved games, so a saved added
// value loads again (and a game without the plugin keeps the field's default instead).
inline void teach_enum(const std::string& enum_name) {
    enum_tokens();

    game::in(std::format("bool LoadFromRecord<{}>", enum_name)).after([enum_name](bool loaded, void* record, int* value, void*) {
        return loaded || read_added_word(enum_name, enum_tokens().of_record(record, 0), value);
    });

    if (game::Address convert = game::try_find(std::format("bool ConvertFromString<{}>", enum_name))) {
        game::in(convert).after([enum_name](bool converted, int* value, const game::String& text) {
            std::optional<int> added = converted ? std::nullopt : added_enum_value(enum_name, text);

            if (added) {
                *value = *added;
            }

            return converted || added.has_value();
        });
    }

    if (game::Address convert = game::try_find(std::format("ConvertToString<{}>", enum_name))) {
        game::in(convert).after([enum_name](game::String text, const int* value) {
            std::optional<std::string> word = added_enum_word(enum_name, *value);

            return word ? game::String(*word) : text;
        });
    }

    if (game::Address load = game::try_find(std::format("bool LoadFromStream<{}>", enum_name))) {
        game::in(load).after([enum_name](bool loaded, void* reader, int* value, void*) {
            return loaded || read_added_word(enum_name, enum_tokens().of_record(enum_tokens().record_of_reader(reader), 0), value);
        });
    }

    if (game::Address write = game::try_find(std::format("bool WriteToRecord<{}>", enum_name))) {
        game::in(write).after([enum_name](bool written, void* record, const int* value, void*) {
            write_added_word(enum_name, *value, enum_tokens().of_record(record, 0));

            return written;
        });
    }

    if (game::Address write = game::try_find(std::format("bool WriteToStream<{}>", enum_name))) {
        game::in(write).after([enum_name](bool written, void* writer, const int* value, void*) {
            write_added_word(enum_name, *value, enum_tokens().of_writer(writer, 0));

            return written;
        });
    }

    teach_enum_properties(enum_name);
}

}


namespace game {

/*
    One of the game's enums, as its data files write it:

        int leaderboards = game::enumeration("mmoGameFeatures::Flag").value("Leaderboards");
        game::enumeration("mmoLeaderboard::Type").add("spenders", 100);

    add teaches the game a new word: where it reads this enum from its data files (type "spenders"; in a .win file),
    it gets the value given, instead of failing on the word. Saved games keep the word too (loader 0.10.0), and a
    game loaded without the plugin gives such a field its default value. Give values the game doesn't use, and handle
    them in your hooks: the game's own code only knows its own.
*/
class Enumeration {
public:
    explicit Enumeration(std::string_view name) : enum_name(name) {}

    int value(std::string_view text) const {
        if (std::optional<int> added = plugin::detail::added_enum_value(enum_name, text)) {
            return *added;
        }

        Function<bool(int* value, const String& text)> convert{ std::format("bool ConvertFromString<{}>", enum_name) };
        int value = 0;

        if (!convert(&value, String(text))) {
            plugin::fail("{} has no value called \"{}\"", enum_name, text);
        }

        return value;
    }

    void add(std::string_view text, int value) const {
        Function<bool(int* value, const String& text)> convert{ std::format("bool ConvertFromString<{}>", enum_name) };
        int existing = 0;

        if (convert(&existing, String(text))) {
            plugin::fail("{} already has \"{}\" (it's {})", enum_name, text, existing);
        }

        auto& all = plugin::detail::added_enum_values();
        bool first = false;

        {
            std::shared_lock reading(plugin::detail::lookups_lock());
            first = all.find(enum_name) == all.end() || all.find(enum_name)->second.empty();
        }

        if (first) {
            plugin::detail::teach_enum(enum_name);
        }

        {
            std::unique_lock writing(plugin::detail::lookups_lock());
            all[enum_name][std::string(text)] = value;
        }

        plugin::detail::remember_undo([enum_name = enum_name, text = std::string(text)] {
            std::unique_lock writing(plugin::detail::lookups_lock());
            plugin::detail::added_enum_values()[enum_name].erase(text);
        });
        plugin::log("{} reads \"{}\" as {}", enum_name, text, value);
    }

private:
    std::string enum_name;
};

inline Enumeration enumeration(std::string_view name) {
    return Enumeration(name);
}

/*
    A text the game shows (its vsLocString), for functions that take one: translation keys in it are filled in when
    it's shown, "{quests_expanded_from} Alda".

        game::Function<void(void* pane, const game::LocalizedText& text)> set_text{ "mmoTextPane::SetText" };
        set_text(pane, game::LocalizedText("{my_mod_title}"));
*/
struct LocalizedText {
    String text;
    // The values it fills in ({name} and the like): none from here.
    void* values[3] = {};

    explicit LocalizedText(std::string_view from = {}) : text(from) {}
};

static_assert(sizeof(LocalizedText) == 0x38, "vsLocString is 0x38 bytes in the game");

// A color as the game keeps one (vsColor): red, green, blue and alpha, 0 to 1.
struct Color {
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    float alpha = 1.0f;
};

}


namespace plugin::detail {

// This plugin's rules, and where the game's Custom Rules lists showed them.
struct CustomRules {
    std::vector<std::string> names;
    // Each rules object a list showed, so a checkbox can name its object by its place here.
    std::vector<const void*> shown;
    // The New Game window's rules, which the game copies field by field when it starts the game.
    const void* new_game = nullptr;
    bool listing_new_game = false;
    // Where each list's rows for this plugin start, set aside while the game sizes the list.
    std::map<const void*, int> first_rows;
    std::optional<std::map<std::string, bool, std::less<>>> starting_game;
    // The loaded game's rules, read from its rules.vrt (while a game loads, on a worker thread) and asked from any.
    std::map<std::string, bool, std::less<>> in_game;
    std::shared_mutex in_game_lock;
};

inline CustomRules& custom_rules() {
    static CustomRules rules;

    return rules;
}

struct RuleList {
    game::Function<void(void* grid, int rows)> set_rows{ "mmoGridView::SetRowCount" };
    game::Function<void(void* grid, int column, int row)> set_empty{ "mmoGridView::SetEmpty" };
    game::Function<void(void* grid, int column, int row, const game::LocalizedText& text, const game::Color& color,
        bool, const game::String&, float)> set_text{ "mmoGridView::SetTextAndColor" };
    game::Function<void(void* grid, int column, int row, bool value, const game::String& command,
        const game::String& tooltip)> set_checkbox{ "mmoGridView::SetCheckbox" };
    game::Function<void(void* grid)> finish{ "mmoGridView::FinishedEditing" };
};

inline const RuleList& rule_list() {
    static const RuleList list;

    return list;
}

inline std::string rule_key(std::string_view name) {
    return std::format("rule.{}", name);
}

inline bool rule_value(const void* rules, std::string_view name) {
    return game::saved(rules).get(rule_key(name), false);
}

inline void set_rule_value(const void* rules, std::string_view name, bool value) {
    if (value) {
        game::saved(rules).set(rule_key(name), true);
    } else {
        game::saved(rules).erase(rule_key(name));
    }
}

inline int shown_slot(const void* rules) {
    auto& shown = custom_rules().shown;
    auto found = std::find(shown.begin(), shown.end(), rules);

    if (found != shown.end()) {
        return static_cast<int>(found - shown.begin());
    }

    shown.push_back(rules);

    return static_cast<int>(shown.size()) - 1;
}

// Room under the game's own rules for a gap and this plugin's rows, made while the game sizes the list: sizing a list
// again later deletes every cell in it. Other plugins make theirs after.
inline void make_room_for_rules(void* grid) {
    CustomRules& state = custom_rules();
    int rows = game::field<int>(grid, "mmoGridView::dimY");

    state.first_rows[grid] = rows;
    rule_list().set_rows(grid, rows + 1 + static_cast<int>(state.names.size()));
}

// In the room made for them: a gap, then a checkbox for each of this plugin's rules.
inline void add_rule_rows(const void* rules, void* grid) {
    CustomRules& state = custom_rules();
    const RuleList& list = rule_list();
    auto first_row = state.first_rows.find(grid);

    if (first_row == state.first_rows.end()) {
        return;
    }

    int row = first_row->second;
    int slot = shown_slot(rules);
    state.first_rows.erase(first_row);

    list.set_empty(grid, 0, row);
    list.set_empty(grid, 1, row);

    for (const std::string& name : state.names) {
        row++;
        list.set_text(grid, 0, row, game::LocalizedText(std::format("{{customrules_{}}}", name)), game::Color(), false, game::String(), 0.0f);
        list.set_checkbox(grid, 1, row, rule_value(rules, name), game::String(std::format("MessageTo Parent ChangedRule {} {}", name, slot)),
            game::String());
    }

    list.finish(grid);
}

// A checkbox sends "MessageTo Parent ChangedRule <name> <slot> <0 or 1>". The game's rule checkboxes send the same
// label, so a save's Settings tab writes its rules.vrt, with this value in it.
inline void take_rule_click(std::string_view command) {
    std::vector<std::string> words;

    for (auto word : std::views::split(command, ' ')) {
        words.emplace_back(word.begin(), word.end());
    }

    CustomRules& state = custom_rules();

    if (words.size() != 6 || words[2] != "ChangedRule" || std::ranges::find(state.names, words[3]) == state.names.end()) {
        return;
    }

    std::optional<int> slot = value_from_text<int>(words[4]);
    std::optional<int> checked = value_from_text<int>(words[5]);

    if (slot && checked && *slot >= 0 && static_cast<std::size_t>(*slot) < state.shown.size()) {
        set_rule_value(state.shown[static_cast<std::size_t>(*slot)], words[3], *checked != 0);
    }
}

inline std::map<std::string, bool, std::less<>> rule_values(const void* rules) {
    std::map<std::string, bool, std::less<>> values;

    for (const std::string& name : custom_rules().names) {
        values[name] = rule_value(rules, name);
    }

    return values;
}

// Where the game's rules go, from the lists to the saved game's rules.vrt and back into the loaded game.
inline void follow_custom_rules() {
    game::in("mmoNewGameWindow::_PopulateCustomRulesGrid").before([](void*) {
        custom_rules().listing_new_game = true;
    });

    game::in("mmoNewGameWindow::_PopulateCustomRulesGrid").after([](void*) {
        custom_rules().listing_new_game = false;
    });

    game::in("mmoCustomRules::SetupGrid").call("mmoGridView::SetRowCount").every().after([](void* grid, int) {
        make_room_for_rules(grid);
    });

    game::in("mmoCustomRules::SetupGrid").after([](void* rules, void* grid, bool) {
        if (custom_rules().listing_new_game) {
            custom_rules().new_game = rules;
        }

        add_rule_rows(rules, grid);
    });

    game::in("mmoPane::Command").before([](void*, const game::String& command) {
        take_rule_click(command.view());
    });

    // Starting a new game: the window is still there, its rules copied into the game's own a moment later.
    game::in("mmoModeInGame::SetNewGameParameters").before([](void*, const void*) {
        CustomRules& state = custom_rules();

        if (state.new_game != nullptr) {
            state.starting_game = rule_values(state.new_game);
        }
    });

    // Then the new game writes its rules.vrt from those.
    game::in("vsObject<mmoCustomRules, vsNullObject>::SaveToFilename").before([](const void* rules, const game::String&) {
        CustomRules& state = custom_rules();

        if (state.starting_game) {
            for (const auto& [name, value] : *state.starting_game) {
                set_rule_value(rules, name, value);
            }

            state.starting_game.reset();
        }
    });

    game::in("mmoModeInGame::DoInit").before([](void*) {
        std::unique_lock writing(custom_rules().in_game_lock);
        custom_rules().in_game.clear();
    });

    // Starting or loading a game reads its rules.vrt into a new rules object, then copies the game's own rules out.
    game::in("vsObject<mmoCustomRules, vsNullObject>::LoadFromRecord").after([](void* rules, void*, void*) {
        auto values = rule_values(rules);
        std::unique_lock writing(custom_rules().in_game_lock);
        custom_rules().in_game = std::move(values);
    });
}

}


namespace game {

/*
    A rule of your own among the game's Custom Rules, set for each saved game: a checkbox under the game's rules on the
    New Game window's Custom Rules tab and on a saved game's Settings tab.

        game::CustomRule no_story = game::custom_rule("my_mod_disable_story");
        if (no_story.enabled()) { ... }

    Its label is the text {customrules_<name>}, from your i18n file. It starts unchecked. The saved game keeps it in its
    rules.vrt, which the game reads when the game loads (the game skips it without the plugin), so a change on the
    Settings tab applies the next time that game loads. Names: 1-59 characters of A-Z a-z 0-9 _ - .
*/
class CustomRule {
public:
    explicit CustomRule(std::string_view name) : rule_name(name) {}

    // Checked for the game that's loaded (false outside a game).
    bool enabled() const {
        auto& rules = plugin::detail::custom_rules();
        std::shared_lock reading(rules.in_game_lock);
        const auto& in_game = rules.in_game;
        auto found = in_game.find(rule_name);

        return found != in_game.end() && found->second;
    }

    const std::string& name() const {
        return rule_name;
    }

private:
    std::string rule_name;
};

inline CustomRule custom_rule(std::string_view name) {
    auto& names = plugin::detail::custom_rules().names;

    if (name.empty() || name.size() > 59 || !std::ranges::all_of(name, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'; })) {
        plugin::fail("A custom rule's name is 1-59 characters of A-Z a-z 0-9 _ - . (not \"{}\")", name);
    }

    if (std::ranges::find(names, name) != names.end()) {
        plugin::fail("The custom rule \"{}\" is already added", name);
    }

    if (!plugin::detail::loader_at_least(0, 11)) {
        plugin::fail("Custom rules need MT2 Loader 0.11.0 or newer, which keeps them in a saved game's rules.vrt (this is {})",
            plugin::loader_version());
    }

    if (names.empty()) {
        plugin::detail::follow_custom_rules();
    }

    names.emplace_back(name);
    plugin::detail::remember_undo([name = std::string(name)] {
        std::erase(plugin::detail::custom_rules().names, name);
    });
    plugin::log("Custom rule \"{}\" added", name);

    return CustomRule(name);
}

}


namespace plugin::detail {

// Runs init or a ready step. If it throws, whatever it hooked or patched is undone, and the log says why.
template<class Step>
bool run_step(std::string_view failure, Step&& step) {
    std::vector<std::function<void()>> undo;
    std::optional<std::string> problem;

    state().undo_steps = &undo;

    try {
        step();
    } catch (const std::exception& error) {
        problem = error.what();
    } catch (...) {
        problem = "an error that isn't a std::exception";
    }

    state().undo_steps = nullptr;

    if (!problem) {
        return true;
    }

    for (auto undo_step = undo.rbegin(); undo_step != undo.rend(); ++undo_step) {
        try {
            (*undo_step)();
        } catch (...) {
        }
    }

    if (undo.empty()) {
        plugin::log("{}: {}", failure, *problem);
    } else {
        plugin::log("{}: {}. Its {} {} undone", failure, *problem, undo.size(), undo.size() == 1 ? "change is" : "changes are");
    }

    return false;
}

inline int start(const raw::PluginApi* api) {
    State& current = state();
    current.api = api;

    if (api->size < offsetof(raw::PluginApi, free_string) + sizeof api->free_string) {
        plugin::log("Not started: it needs MT2 Loader 0.4.0 or newer, and this is {}", api->loader_version);

        return 1;
    }

    if (!current.early_problem.empty()) {
        plugin::log("Not started: {}", current.early_problem);

        return 1;
    }

    current.started = run_step("Not started", [&current] {
        for (const std::function<void()>& look_up : current.pending_lookups) {
            look_up();
        }

        current.pending_lookups.clear();
        plugin::init();
    });

    return current.started ? 0 : 1;
}

inline void ready() {
    State& current = state();

    if (!current.started) {
        return;
    }

    for (const std::function<void()>& step : current.ready_steps) {
        run_step("A step given to on_ready stopped", step);
    }
}

}


MT2LOADER_ENTRY int plugin_init(const plugin::raw::PluginApi* api) {
    return plugin::detail::start(api);
}

MT2LOADER_ENTRY void plugin_ready(const plugin::raw::PluginApi*) {
    plugin::detail::ready();
}

#endif
