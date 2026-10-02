#include "dungeon_quests.h"

#include "objectives.h"
#include "player.h"
#include "quest.h"

#include <mt2loader.hpp>


namespace dungeon_quests {

void* dungeon_at(void* destination) {
    void* building = game::as(destination, "mmoBuilding");

    return building != nullptr ? game::field<void*>(building, "mmoBuilding::dungeon") : nullptr;
}

void* dungeon_of(const void* quest) {
    void* dungeon = quest != nullptr ? dungeon_at(quest::destination(quest)) : nullptr;

    return dungeon != nullptr && objectives::of(quest) == Objective::standard ? dungeon : nullptr;
}

bool is_cleared(const void* instance) {
    return game::field<int>(instance, "mmoQuestInstance::monstersDefeated") > 0;
}

void mark_cleared(void* instance) {
    game::field<int>(instance, "mmoQuestInstance::monstersDefeated") = 1;
}

void install() {
    // The game's text for a building it doesn't serve ("enjoy the scenery") has no values of its own to let go of.
    game::in("mmoQuest::GetActionString").after([](game::LocalizedText text, const void* quest) {
        return dungeon_of(quest) != nullptr ? game::LocalizedText("{quests_expanded_action_clear_dungeon}") : text;
    });

    // Getting there isn't enough: only a run the player was part of.
    game::in("mmoQuestInstance::IsReadyToTurnIn").after([](bool ready, const void* instance) {
        return dungeon_of(player::quest_of(instance)) != nullptr ? is_cleared(instance) : ready;
    });
}

}
