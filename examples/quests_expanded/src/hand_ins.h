#pragma once

// Giving a player the quest a hand-off sends them to, through the game's own way of taking a quest
// (mmoToon::CollectQuestFromQuestGiver): the talk, the history entry, the record and the quest's counters.
namespace hand_ins {

void give(void* toon, void* npc, int index);

// Whether a hand-in from this quest giver is being given right now, whatever the player's level.
bool is_giving(const void* npc);

void install();

}
