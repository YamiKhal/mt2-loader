#include "target_editing.h"

#include "layout.h"
#include "objectives.h"
#include "quest_details.h"
#include "questlines.h"
#include "steering.h"

#include <mt2loader.hpp>

static game::Function<void(void* display, void* quest)> display_quest{ "mmoQuestDisplay::DisplayQuest" };


namespace target_editing {

void install() {
    // "QuestsExpanded details": the card's details button (sent to the card itself).
    game::in("mmoQuestSelector::UICommand").after([](bool handled, void* card, game::Record command) {
        if (command.label() != "QuestsExpanded" || command.text(0) != "details") {
            return handled;
        }

        if (void* quest = weak_object(card, layout.card_quest)) {
            quest_details::open(quest);
        }

        return true;
    });

    game::in("mmoQuestDisplay::SetNewDestination").hook<void(void* display, void* destination)>([](auto set, void* display, void* destination) {
        void* quest = weak_object(display, layout.displayed_quest);

        Objective objective = objectives::of(quest);
        std::vector<void*> targets = objectives::targets(quest);
        // A branch whose own target is no quest giver to send players to takes the pick in its place.
        bool replaces_own = targets.empty() || (objective == Objective::branch && !objectives::can_branch_to(quest, targets.front()));

        if (destination == nullptr || objective == Objective::standard || replaces_own) {
            set(display, destination);

            return;
        }

        objectives::add_target(quest, destination);
        questlines::changed();
        steering::changed();
        quest_details::refresh_if_showing(quest);
        display_quest(display, quest);
    });

    // The game destroys every quest whose own target goes; a tour's or a branch's next target becomes its own first.
    game::in("mmoQuestManager::QuestDestinationRemoved").before([](void*, void* destination) {
        questlines::update();

        for (const Questline& line : questlines::all()) {
            for (void* quest : line.quests) {
                std::vector<void*> targets = objectives::targets(quest);

                if (targets.size() > 1 && targets.front() == destination) {
                    objectives::remove_target(quest, 0);
                    questlines::changed();
                    steering::changed();
                }
            }
        }
    });
}

}
