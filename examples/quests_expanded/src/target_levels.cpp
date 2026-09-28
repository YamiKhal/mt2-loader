#include "target_levels.h"

#include "layout.h"
#include "objectives.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <cstddef>

// mmoRange, as mmoQuest::GetQuestLevelRange returns it.
struct LevelRange {
    int lowest = 0;
    int highest = 0;
    bool set = false;
};


// The target's own answer (mmoQuestDestination's level functions, which each kind of target has its own of).
static int ask_level(void* destination, std::ptrdiff_t slot) {
    using Ask = int (*)(void* destination);
    void* const* table = *static_cast<void* const* const*>(destination);

    return reinterpret_cast<Ask>(table[static_cast<std::size_t>(slot) / sizeof(void*)])(destination);
}

static bool has_more_targets(const void* quest) {
    return objectives::of(quest) != Objective::standard;
}

static int lowest_of(const void* quest, std::ptrdiff_t slot, int level) {
    for (void* target : objectives::targets(quest)) {
        level = std::min(level, ask_level(target, slot));
    }

    return level;
}

static int highest_of(const void* quest, std::ptrdiff_t slot, int level) {
    for (void* target : objectives::targets(quest)) {
        level = std::max(level, ask_level(target, slot));
    }

    return level;
}


namespace target_levels {

void install() {
    game::in("mmoQuest::GetQuestMinLevel").after([](int level, const void* quest) {
        return has_more_targets(quest) ? lowest_of(quest, layout.min_level_slot, level) : level;
    });

    game::in("mmoQuest::GetQuestMaxLevel").after([](int level, const void* quest) {
        return has_more_targets(quest) ? highest_of(quest, layout.max_level_slot, level) : level;
    });

    game::in("mmoQuest::GetQuestLevelRange").hook<LevelRange(const void* quest)>([](auto range_of, const void* quest) {
        LevelRange range = range_of(quest);

        if (has_more_targets(quest) && range.set) {
            range.lowest = lowest_of(quest, layout.min_level_slot, range.lowest);
            range.highest = highest_of(quest, layout.max_level_slot, range.highest);
        }

        return range;
    });
}

}
