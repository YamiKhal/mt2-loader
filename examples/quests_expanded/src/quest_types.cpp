#include "quest_types.h"

#include "objectives.h"
#include "quest_details.h"
#include "questlines.h"
#include "steering.h"
#include "ui.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <string>
#include <vector>

constexpr const char* dropdown_id = "quests_expanded_quest_type";

static game::Function<void(void* card)> refresh_card{ "mmoQuestSelector::RefreshQuest" };

// A pick reaches the command from inside the dropdown, which still reads its list: it isn't filled again meanwhile,
// and setting it from here isn't the designer picking.
static bool busy = false;


// The dropdown's choices, as the player reads them, in the order of Objective.
static std::vector<std::string> choices() {
    return { ui::translated("{quests_expanded_type_standard}"), ui::translated("{quests_expanded_type_tour}"),
        ui::translated("{quests_expanded_type_branch}") };
}

static void fill(void* card) {
    void* dropdown = ui::find_pane(card, dropdown_id);
    void* quest = game::field<game::WeakPointer>(card, "mmoQuestSelector::quest").get();

    if (dropdown == nullptr || quest == nullptr || busy) {
        return;
    }

    std::vector<std::string> all = choices();
    const std::string& type = all[static_cast<std::size_t>(objectives::of(quest))];

    // A card is rebuilt with an empty dropdown, and reused for another quest.
    if (ui::selected_choice(dropdown) == type) {
        return;
    }

    busy = true;
    ui::set_choices(dropdown, all);
    ui::select_choice(dropdown, type);
    busy = false;
}

static void pick(void* card) {
    void* dropdown = ui::find_pane(card, dropdown_id);
    void* quest = game::field<game::WeakPointer>(card, "mmoQuestSelector::quest").get();

    if (dropdown == nullptr || quest == nullptr) {
        return;
    }

    std::vector<std::string> all = choices();
    auto picked = std::ranges::find(all, ui::selected_choice(dropdown));

    if (picked == all.end()) {
        return;
    }

    auto objective = static_cast<Objective>(picked - all.begin());

    if (objective == objectives::of(quest)) {
        return;
    }

    objectives::set(quest, objective);
    // A branch quest makes its line a graph.
    questlines::changed();
    steering::changed();
    quest_details::refresh_if_showing(quest);
    refresh_card(card);
}


namespace quest_types {

void install() {
    game::in("mmoQuestSelector::RefreshQuest").after([](void* card) {
        fill(card);
    });

    game::in("mmoQuestSelector::UICommand").after([](bool handled, void* card, game::Record command) {
        if (command.label() != "QuestsExpanded" || command.text(0) != "quest_type") {
            return handled;
        }

        if (!busy) {
            busy = true;
            pick(card);
            busy = false;
        }

        return true;
    });
}

}
