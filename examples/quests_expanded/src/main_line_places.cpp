#include "main_line_places.h"

#include "player.h"
#include "quest.h"
#include "questlines.h"

#include <mt2loader.hpp>

#include <algorithm>

// The current main quest, or the one where the player finished the main questline (at any of its ends): kept so
// they're invited back if the line grows (line_extensions).
static bool is_main_line_place(const void* instance) {
    questlines::update();

    void* quest = player::quest_of(instance);
    const Questline* line = quest != nullptr ? questlines::main_line_of_quest(quest) : nullptr;

    if (line == nullptr) {
        return false;
    }

    void* giver = quest::giver(quest);

    return !player::is_finished(instance) || std::ranges::any_of(line->ends, [&](void* end) { return quest::giver(end) == giver; });
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
