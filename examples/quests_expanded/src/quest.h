#pragma once

#include <string>

// What a quest asks of players, from its action (mmoQuest::Action): errands in town (go to a building, set home, buy
// or sell), hunting monsters, fetching what monsters drop, killing an elite, or talking to someone.
enum class QuestKind {
    errand,
    hunt,
    fetch,
    elite,
    talk,
};

namespace quest {

void* giver(const void* quest);
void* destination(const void* quest);
bool is_talk(const void* quest);
QuestKind kind(const void* quest);
int min_level(const void* quest);
void refresh_advertisements(void* quest);

std::string name(const void* quest);

// How many times players took and turned in the quest, ever (its daily counts' totals).
int times_accepted(const void* quest);
int times_completed(const void* quest);

}
