#pragma once

#include <cstddef>
#include <vector>

// What a quest has players do, beyond the game's one target (Standard):
// - a Tour: 2 to 5 targets players do in any order, each with its own action;
// - a Branch: a hand-off to one of 2 to 4 quest givers, each player picking one.
enum class Objective {
    standard,
    tour,
    branch,
};

// A quest's targets. The first is the quest's own (what the game and a save without the mod see); the rest are kept on
// the quest in the saved game, as links. Targets are the game's quest destinations (mmoQuestDestination*: a
// building, zone, NPC or elite).
namespace objectives {

constexpr std::size_t most_tour_targets = 5;
constexpr std::size_t most_branch_targets = 4;

Objective of(const void* quest);
// A Standard quest drops every target but its own; a Branch drops those that aren't other quest givers.
void set(void* quest, Objective objective);

std::size_t most_targets(Objective objective);
std::vector<void*> targets(const void* quest);
// The object a target is part of (a building, for the quest destination inside it): what links keep.
const void* object_of(const void* destination);
// A quest giver a Branch can send players to: a quest giver with quests, other than the quest's own.
bool can_branch_to(const void* quest, void* destination);
// False when the quest is full, already has it, or (a Branch) it isn't a quest giver it can send players to.
bool add_target(void* quest, void* destination);
// The quest's own target is replaced by the next one; a quest keeps at least one.
void remove_target(void* quest, std::size_t index);

}
