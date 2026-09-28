#include "line_stats.h"

#include "quest.h"

#include <algorithm>

namespace line_stats {

LineStats of(const Questline& line) {
    LineStats stats;

    for (void* quest : line.quests) {
        QuestStats quest_stats{ quest, quest::times_accepted(quest), quest::times_completed(quest) };

        stats.on_it += std::max(quest_stats.accepted - quest_stats.completed, 0);
        stats.quests.push_back(quest_stats);
    }

    if (!stats.quests.empty()) {
        stats.started = stats.quests.front().accepted;
        stats.finished = stats.quests.back().completed;
    }

    return stats;
}

std::optional<int> finish_rate(int taken, int turned_in) {
    if (taken <= 0) {
        return std::nullopt;
    }

    return turned_in * 100 / taken;
}

}
