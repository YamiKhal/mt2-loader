#pragma once

#include <string>
#include <vector>

namespace quest_giver {

// Every quest giver in the world with at least one quest.
std::vector<void*> all();

// The quest giver a quest's target is, if it's one with quests; otherwise nullptr.
void* at_destination(void* destination);

int quest_count(const void* npc);
void* quest(const void* npc, int index);

// Where a quest is in its quest giver's chain, or -1.
int index_of(const void* npc, const void* quest);
std::string name(const void* npc);

}
