#include "player_classes.h"

#include <mt2loader.hpp>

// The game's character types come in kinds: player classes, monsters and NPCs.
constexpr int class_kind = 0;

static game::Function<int(void* manager, int kind)> type_count{ "mmoCharacterTypeManager::GetTypeCount" };
static game::Function<void*(void* manager, int kind, int index)> type_at{ "mmoCharacterTypeManager::GetType(CharacterType, int)" };
static game::Function<const game::String&(const void* type)> name_of{ "mmoCharacterType::GetName" };
static game::Function<void*(void* character)> type_of_character{ "mmoCharacter::GetCharacterType()" };


static void* class_at(int index) {
    void* manager = game::singleton("mmoCharacterTypeManager");

    if (manager == nullptr || index < 0 || index >= type_count(manager, class_kind)) {
        return nullptr;
    }

    void* type = type_at(manager, class_kind, index);

    return type != nullptr && !game::field<bool>(type, "mmoCharacterType::destroyed") ? type : nullptr;
}


namespace player_classes {

std::vector<PlayerClass> all() {
    void* manager = game::singleton("mmoCharacterTypeManager");
    std::vector<PlayerClass> classes;

    if (manager == nullptr) {
        return classes;
    }

    for (int index = 0; index < type_count(manager, class_kind); index++) {
        if (void* type = class_at(index)) {
            classes.push_back(PlayerClass{ index, std::string(name_of(type).view()) });
        }
    }

    return classes;
}

bool exists(int index) {
    return class_at(index) != nullptr;
}

std::optional<int> of(const void* toon) {
    void* manager = game::singleton("mmoCharacterTypeManager");
    void* type = toon != nullptr ? type_of_character(const_cast<void*>(toon)) : nullptr;

    if (manager == nullptr || type == nullptr) {
        return std::nullopt;
    }

    for (int index = 0; index < type_count(manager, class_kind); index++) {
        if (type_at(manager, class_kind, index) == type) {
            return index;
        }
    }

    return std::nullopt;
}

}
