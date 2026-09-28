#pragma once

#include <cstddef>
#include <vector>

// How far a player is through a tour quest: the targets they've done, kept with the player in the saved game (as
// links to the targets, so changing the tour's other targets doesn't mix them up). Forgotten when they turn it in.
namespace tour_progress {

// The targets the player hasn't done, nearest first: each one the nearest to the one before, from the last target
// they did (or the quest giver). Worked out from what's done, so it stays put while they walk.
std::vector<void*> route(void* toon, const void* quest);
// The first on their route, or nullptr once every one is done.
void* current_target(void* toon, const void* quest);
std::size_t targets_left(void* toon, const void* quest);
// How far the rest of their route is, from its first target to its last.
float route_length(void* toon, const void* quest);
void mark_done(void* toon, const void* quest, void* destination);
void forget(void* toon, const void* quest);

}
