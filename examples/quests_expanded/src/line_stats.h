#pragma once

#include "questlines.h"

#include <cstddef>
#include <optional>
#include <vector>

struct QuestStats {
    void* quest = nullptr;
    int accepted = 0;
    int completed = 0;
};

// How players get on with a line, from its quests' own counts: taking its first quest starts it, turning in the last
// of any of its ends finishes it, and a quest taken but not turned in yet has a player on it (or one who dropped it).
struct LineStats {
    int started = 0;
    int finished = 0;
    int on_it = 0;
    std::vector<QuestStats> quests;
};

namespace line_stats {

LineStats of(const Questline& line);
// How many players took a chapter's first quest: for a branch, how many took that branch.
int players_starting(const Questline& line, std::size_t chapter);

// Turned in per taken, in percent; nothing while no one took it.
std::optional<int> finish_rate(int taken, int turned_in);

}
