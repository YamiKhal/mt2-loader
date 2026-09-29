#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

// The MMO's characters (mmoCharacterType): its NPCs, classes and monsters with all their variants, as the quest giver
// tool offers them. Each is named by the game's own id for it, "<kind>.<index>" ("npc.3", "monster.12"), as a placed
// character saves its type. A type is never taken out of the game's list (destroying one only marks it), so a name
// keeps standing for the same character, in saves too.
namespace character_types {

struct Kind {
    // CharacterType, as the game numbers them: Class, Monster, NPC.
    int number;
    std::string_view word;
};

// In the order the quest giver tool lists them (mmoCursorBehaviourNPC::Activate).
constexpr std::array<Kind, 3> kinds{ { { 2, "npc" }, { 0, "class" }, { 1, "monster" } } };

struct Named {
    std::string name;
    void* type = nullptr;
};

// The MMO's own characters (an mmoCharacterTypeCollection), as the character editor and the quest giver tool show
// them, or nullptr outside a game.
const void* design_characters();
// Every character of the MMO not destroyed, NPCs first, then classes, then monsters. Empty outside a game.
std::vector<Named> all();
// The character a name stands for, destroyed or not, or nullptr.
void* named(std::string_view name);
bool is_name(std::string_view name);
std::string display_name(const void* type);

}
