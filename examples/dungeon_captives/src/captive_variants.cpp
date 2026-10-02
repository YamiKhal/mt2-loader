#include "captive_variants.h"

#include "character_types.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <mutex>
#include <vector>

constexpr const char* base_file = "gizmo/dungeon_captives_captive/base.variant";
// Room for this many variants before their list moves: made up front, since saved captives load on the game's workers.
constexpr std::size_t room_for_variants = 1024;

static game::Function<void*(const void* library, const game::String& name)> definition_named{ "mmoGizmoLibrary::GetGizmoDefinition" };
static game::Function<void*(const void* definition, const game::String& name)> variant_named{ "mmoGizmoDefinition::GetVariant" };

// One variant is made once, whichever thread asks first.
static std::mutex making_lock;
// Variants kept out of the placing tool's list while it fills it: other characters' poses and the data variant.
static std::size_t hidden = 0;


static bool is_captive(const void* definition) {
    return game::field<game::String>(definition, "mmoGizmoDefinition::name").view() == captive_variants::type_name;
}

static bool is_captive_name(std::string_view variant) {
    return !captive_variants::character_of(variant).empty();
}

static game::Objects& variants_of(void* definition) {
    return game::field<game::Objects>(definition, "mmoGizmoDefinition::variants");
}

static std::string name_of(const void* variant) {
    return game::field<game::String>(variant, "mmoGizmoVariant::name").str();
}

static void* make_variant(void* definition, const std::string& name) {
    std::lock_guard making(making_lock);
    game::Objects& variants = variants_of(definition);
    auto made = std::ranges::find_if(variants, [&](const void* variant) { return name_of(variant) == name; });

    if (made != variants.end()) {
        return *made;
    }

    void* variant = game::load(base_file);

    if (variant == nullptr) {
        return nullptr;
    }

    game::field<game::String>(variant, "mmoGizmoVariant::name") = name;
    variants.reserve(room_for_variants);
    variants.add(variant);

    return variant;
}

static void* captive_definition() {
    void* library = game::singleton("mmoGizmoLibrary");

    return library != nullptr ? definition_named(library, game::String(std::string(captive_variants::type_name))) : nullptr;
}

// The tool lists a type's variants up to their count: the ones it shows go first, the count covers only them.
static void show_only(void* definition, const std::vector<void*>& shown) {
    std::lock_guard making(making_lock);
    game::Objects& variants = variants_of(definition);
    std::vector<void*> rest;

    for (void* variant : variants) {
        if (std::ranges::find(shown, variant) == shown.end()) {
            rest.push_back(variant);
        }
    }

    std::ranges::copy(shown, variants.begin());
    std::ranges::copy(rest, variants.begin() + shown.size());
    hidden = rest.size();
    variants.resize(shown.size());
}


namespace captive_variants {

std::string variant_name(std::string_view character, std::string_view pose) {
    return std::format("{}.{}", character, pose);
}

std::string_view character_of(std::string_view variant) {
    std::size_t dot = variant.rfind('.');

    if (dot == std::string_view::npos || std::ranges::find(poses, variant.substr(dot + 1)) == poses.end()) {
        return {};
    }

    std::string_view character = variant.substr(0, dot);

    return character_types::is_name(character) ? character : std::string_view{};
}

std::string_view pose_of(std::string_view variant) {
    return character_of(variant).empty() ? std::string_view{} : variant.substr(variant.rfind('.') + 1);
}

void show_poses(std::string_view character) {
    void* definition = captive_definition();

    if (definition == nullptr) {
        return;
    }

    std::vector<void*> shown;

    for (std::string_view pose : poses) {
        void* variant = !character.empty() ? variant_named(definition, game::String(variant_name(character, pose))) : nullptr;

        if (variant != nullptr) {
            shown.push_back(variant);
        }
    }

    if (shown.empty()) {
        if (void* base = variant_named(definition, game::String(std::string(base_variant)))) {
            shown.push_back(base);
        }
    }

    show_only(definition, shown);
}

void show_all() {
    void* definition = captive_definition();

    if (definition == nullptr) {
        return;
    }

    std::lock_guard making(making_lock);
    game::Objects& variants = variants_of(definition);
    variants.resize(variants.size() + hidden);
    hidden = 0;
}

void install() {
    // A captive's variant is asked for by name (placing, loading, drawing): a character's pose is made the first time.
    game::in("mmoGizmoDefinition::GetVariant").after([](void* found, const void* definition, const game::String& name) {
        if (found != nullptr || !is_captive_name(name.view()) || !is_captive(definition)) {
            return found;
        }

        return make_variant(const_cast<void*>(definition), name.str());
    });

    // A saved captive loading asks the library's own table, which has only the data variant; one it lacks would
    // become treasure.
    game::in("mmoGizmoLibrary::GizmoVariantExists").after([](bool exists, const void* library, const game::String& type, const game::String& variant) {
        if (exists || type.view() != type_name || !is_captive_name(variant.view())) {
            return exists;
        }

        void* definition = definition_named(library, type);

        return definition != nullptr && variant_named(definition, variant) != nullptr;
    });

    // The tool names each entry after its type; a captive's after its pose (the character window names who).
    game::in("mmoCursorBehaviourGizmo::Activate").call("mmoGizmo::GetDisplayName")
        .hook<const game::String&(const game::String& title, const game::String& variant)>(
            [](auto name, const game::String& title, const game::String& variant) -> const game::String& {
                static game::String shown;
                std::string_view pose = pose_of(variant.view());

                if (pose.empty()) {
                    return name(title, variant);
                }

                shown = std::format("{{dungeon_captives_pose_{}}}", pose);

                return shown;
            });
}

}
