#include "story_demand.h"

#include "player_types.h"
#include "questlines.h"
#include "story.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <cmath>

// Past the game's own (0 to 7, and 8 it clears out).
constexpr int story_chapter_demand = 20;
constexpr const char* story_chapter_word = "newstorychapter";
constexpr const char* story_chapter_key = "quests_expanded_chapter_release";

// Heat each day is the game's, times 0.5 (no one cares) to 1.5 (everyone cares).
constexpr float least_heat = 0.5f;

static game::Function<void(void* manager, int type)> ensure_demand{ "mmoReleaseManager::_EnsureHasBaseDemandType" };
static game::Function<void(void* manager, int type)> purge_demand{ "mmoReleaseManager::_PurgeBaseDemandType" };
static game::Function<void(void* manager)> validate_demands{ "mmoReleaseManager::ValidateDemands" };
static game::Function<void(void* keys, const game::String& key)> add_key{ "vsArray<std::string>::AddItem(std::string const&)" };


static bool has_main_line() {
    questlines::update();

    const std::vector<Questline>& lines = questlines::all();

    return std::any_of(lines.begin(), lines.end(), [](const Questline& line) { return line.settings.main; });
}

static bool is_story_demand(const void* demand) {
    return game::field<int>(demand, "mmoReleaseDemand::type") == story_chapter_demand;
}


namespace story_demand {

void install() {
    game::enumeration("mmoReleaseDemand::Type").add(story_chapter_word, story_chapter_demand);

    game::in("mmoReleaseManager::_SetupNewDemands").after([](void* manager) {
        if (story::enabled() && has_main_line()) {
            ensure_demand(manager, story_chapter_demand);
        } else {
            purge_demand(manager, story_chapter_demand);
        }
    });

    game::in("mmoReleaseDemand::_SetMatchingChangeKeys").after([](void* demand) {
        if (is_story_demand(demand)) {
            add_key(game::object_in(demand, "mmoReleaseDemand::changeKeys"), game::String(story_chapter_key));
        }
    });

    game::in("mmoReleaseManager::_BuildHeat_NewDay").call("mmoReleaseDemand::IncrementHeat").hook<void(void* demand, int heat)>(
        [](auto increment_heat, void* demand, int heat) {
            if (is_story_demand(demand)) {
                heat = static_cast<int>(std::lround(static_cast<float>(heat) * (least_heat + player_types::story_interest())));
            }

            increment_heat(demand, heat);
        });
}

void refresh() {
    if (void* manager = game::singleton("mmoReleaseManager")) {
        validate_demands(manager);
    }
}

const char* change_key() {
    return story_chapter_key;
}

}
