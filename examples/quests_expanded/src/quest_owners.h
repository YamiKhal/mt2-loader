#pragma once

// Whose quest a player's quest (an mmoQuestInstance, inside their record of a quest giver) is. The game's functions
// on it don't say, so the mod looks it up among the players, remembering what it found.
namespace quest_owners {

void install();
void* player_of(const void* instance);

}
