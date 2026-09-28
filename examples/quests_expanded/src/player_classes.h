#pragma once

#include <optional>
#include <string>
#include <vector>

// The MMO's player classes, by their place in the game's list (mmoCharacterTypeManager, type 0), which stays the same
// when a class is renamed or another is destroyed.
struct PlayerClass {
    int index = 0;
    std::string name;
};

namespace player_classes {

// The classes players can still play, in the game's order.
std::vector<PlayerClass> all();
bool exists(int index);
std::optional<int> of(const void* toon);

}
