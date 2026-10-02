#include "target_levels.h"

#include "objectives.h"

#include <mt2loader.hpp>

#include <algorithm>

// mmoRange, as mmoQuest::GetQuestLevelRange returns it.
struct LevelRange {
    int lowest = 0;
    int highest = 0;
    bool set = false;
};

// The target's own answer: each kind of target has its own.
static game::Virtual<int(void* destination)> min_level_of{ "mmoQuestDestination::GetQuestMinLevel" };
static game::Virtual<int(void* destination)> max_level_of{ "mmoQuestDestination::GetQuestMaxLevel" };


static bool has_more_targets(const void* quest) {
    return objectives::of(quest) != Objective::standard;
}

static int lowest_of(const void* quest, int level) {
    for (void* target : objectives::targets(quest)) {
        level = std::min(level, min_level_of(target));
    }

    return level;
}

static int highest_of(const void* quest, int level) {
    for (void* target : objectives::targets(quest)) {
        level = std::max(level, max_level_of(target));
    }

    return level;
}


namespace target_levels {

void install() {
    game::in("mmoQuest::GetQuestMinLevel").after([](int level, const void* quest) {
        return has_more_targets(quest) ? lowest_of(quest, level) : level;
    });

    game::in("mmoQuest::GetQuestMaxLevel").after([](int level, const void* quest) {
        return has_more_targets(quest) ? highest_of(quest, level) : level;
    });

    game::in("mmoQuest::GetQuestLevelRange").hook<LevelRange(const void* quest)>([](auto range_of, const void* quest) {
        LevelRange range = range_of(quest);

        if (has_more_targets(quest) && range.set) {
            range.lowest = lowest_of(quest, range.lowest);
            range.highest = highest_of(quest, range.highest);
        }

        return range;
    });
}

}
