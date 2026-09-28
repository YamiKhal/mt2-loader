#pragma once

#include <mt2loader.hpp>

#include <vector>

// A player's place with each quest giver they've met: one record (mmoQuestGiverProgress) per quest giver, with the
// step in its chain and the quest they're on (an mmoQuestInstance inside the record).
namespace player {

constexpr int met_nothing_taken = -1;

std::vector<void*> records(void* toon);
// The game's own list, without a copy: only for a loop that neither adds nor removes records.
const game::Objects& records_in_place(void* toon);
void* record_for(void* toon, void* npc);
void* giver_of(const void* record);

int step(const void* record);
void set_step(void* record, int step);
void* instance_of(void* record);

void* quest_of(const void* instance);
bool is_finished(const void* instance);

// The index of the quest this quest giver gives the player next: where their record is, or the first one.
int next_quest_index(void* toon, void* npc);

}
