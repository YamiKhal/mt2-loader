#include "main_line_places.h"

#include "player.h"
#include "quest.h"
#include "questlines.h"

#include <mt2loader.hpp>

// The current main quest, or the one where the player finished the main questline: kept so they're invited back if
// the line grows (line_extensions).
static bool is_main_line_place(const void* instance) {
    questlines::update();

    void* quest = player::quest_of(instance);
    const Questline* line = quest != nullptr ? questlines::main_line_of_quest(quest) : nullptr;

    if (line == nullptr) {
        return false;
    }

    return !player::is_finished(instance) || quest::giver(quest) == quest::giver(line->last_quest);
}


namespace main_line_places {

void install() {
    // The game forgets an outleveled record unless its quest is ready to turn in.
    game::in("mmoToon::RemoveOutlevelledKnowledge").call("mmoQuestInstance::IsReadyToTurnIn")
        .hook<bool(const void* instance)>([](auto is_ready_to_turn_in, const void* instance) {
            return is_ready_to_turn_in(instance) || is_main_line_place(instance);
        });
}

}
