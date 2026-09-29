#pragma once

// A plain quest to a dungeon's entrance asks players to clear the dungeon: it's done once a party they're in finishes
// a run of it, as the game judges one complete (no chests, loot or dungeon bosses left). Their copy of the quest keeps
// that as one defeat, which a quest to a building never counts otherwise; a save without the mod has a plain "go
// there" quest.
namespace dungeon_quests {

void install();
// The dungeon a quest's target is the entrance of, or nullptr.
void* dungeon_at(void* destination);
// The dungeon a quest asks players to clear, or nullptr (a tour's targets are each plain "go there").
void* dungeon_of(const void* quest);
bool is_cleared(const void* instance);
void mark_cleared(void* instance);

}
