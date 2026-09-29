#include "character_types.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>

static game::Function<int(const void* manager, int kind)> type_count{ "mmoCharacterTypeManager::GetTypeCount" };
static game::Function<void*(void* manager, int kind, int index)> type_at{ "mmoCharacterTypeManager::GetType(CharacterType, int)" };
static game::Function<bool(const void* collection, const void* type)> owns{ "mmoCharacterTypeCollection::Owns" };
static game::Function<const game::String&(const void* type)> name_of{ "mmoCharacterType::GetName" };


static bool is_destroyed(const void* type) {
    return game::field<bool>(type, "mmoCharacterType::destroyed");
}

static std::optional<int> index_in(std::string_view text) {
    int index = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), index);

    if (error != std::errc{} || end != text.data() + text.size() || index < 0) {
        return std::nullopt;
    }

    return index;
}


namespace character_types {

const void* design_characters() {
    void* design = game::singleton("mmoDesign");

    return design != nullptr ? game::object_in(design, "mmoDesign::globalCharacterType") : nullptr;
}

std::vector<Named> all() {
    std::vector<Named> found;
    void* manager = game::singleton("mmoCharacterTypeManager");
    const void* characters = design_characters();

    if (manager == nullptr || characters == nullptr) {
        return found;
    }

    for (const Kind& kind : kinds) {
        int count = type_count(manager, kind.number);

        for (int index = 0; index < count; index++) {
            void* type = type_at(manager, kind.number, index);

            if (type != nullptr && !is_destroyed(type) && owns(characters, type)) {
                found.push_back(Named{ std::format("{}.{}", kind.word, index), type });
            }
        }
    }

    return found;
}

void* named(std::string_view name) {
    void* manager = game::singleton("mmoCharacterTypeManager");
    std::size_t dot = name.find('.');

    if (manager == nullptr || dot == std::string_view::npos) {
        return nullptr;
    }

    auto kind = std::ranges::find(kinds, name.substr(0, dot), &Kind::word);
    std::optional<int> index = index_in(name.substr(dot + 1));

    if (kind == kinds.end() || !index || *index >= type_count(manager, kind->number)) {
        return nullptr;
    }

    return type_at(manager, kind->number, *index);
}

bool is_name(std::string_view name) {
    return std::ranges::any_of(kinds, [&](const Kind& kind) {
        return name.size() > kind.word.size() && name.starts_with(kind.word) && name[kind.word.size()] == '.';
    });
}

std::string display_name(const void* type) {
    return name_of(type).str();
}

}
