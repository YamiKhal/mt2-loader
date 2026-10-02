#include "quest_details.h"

#include "objectives.h"
#include "quest.h"
#include "questlines.h"
#include "steering.h"
#include "ui.h"

#include <mt2loader.hpp>

#include <format>
#include <optional>
#include <string>
#include <vector>

constexpr const char* window_id = "quest_details";

// The game's texts for what a quest has players do there, by mmoQuest::Action; a hunt's needs a count, so it has the
// mod's own.
constexpr const char* action_texts[] = {
    "{quest_action_enjoyscenery}", "{quests_expanded_action_hunt}", "{quest_action_sethome}", "{quest_action_buyweapon}",
    "{quest_action_buypotion}", "{quest_action_sellloot}", "{quest_action_defeatit}", "{quest_action_talk}",
};

static game::Function<int(void* destination)> default_action{ "mmoQuest::DefaultQuestActionForDestination" };

static void* window = nullptr;
static void* shown_quest = nullptr;
// What the lore edit last showed, so a refresh doesn't undo what the designer is typing.
static std::string shown_lore;
// Setting the window's controls from here isn't the designer changing them.
static bool refreshing = false;


static std::string target_text(std::size_t index, void* target) {
    const void* object = objectives::object_of(target);
    std::string name(game::field<game::String>(object, "mmoProp::name").view());
    int action = default_action(target);
    const char* doing = action >= 0 && action < static_cast<int>(std::size(action_texts)) ? action_texts[action] : "";

    return std::format("{}. {}: {}", index + 1, name, doing);
}

static void fill_targets() {
    std::vector<void*> targets = objectives::targets(shown_quest);
    bool removable = objectives::of(shown_quest) != Objective::standard && targets.size() > 1;

    for (std::size_t index = 0; index < objectives::most_tour_targets; index++) {
        void* text = ui::find_pane(window, std::format("quests_expanded_target_{}", index));
        void* remove = ui::find_pane(window, std::format("quests_expanded_remove_target_{}", index));
        bool shown = index < targets.size();

        if (text == nullptr || remove == nullptr) {
            continue;
        }

        ui::set_visible(text, shown);
        ui::set_visible(remove, shown && removable);

        if (shown) {
            ui::set_text(text, target_text(index, targets[index]));
        }
    }
}

static void fill_lore() {
    void* edit = ui::find_pane(window, "quests_expanded_lore");
    std::string lore(game::field<game::String>(shown_quest, "mmoQuest::description").view());

    if (edit != nullptr && lore != shown_lore) {
        ui::set_edit_text(edit, lore);
        shown_lore = lore;
    }
}

static void refresh() {
    if (window == nullptr || shown_quest == nullptr) {
        return;
    }

    refreshing = true;
    ui::set_window_title(window, "{quests_expanded_details_title} " + quest::name(shown_quest));
    fill_targets();
    fill_lore();
    refreshing = false;
}

// Also when its quest is gone (the card's undo remakes quests), which left it open and empty.
static void close() {
    if (window != nullptr) {
        ui::close_window(window);
    }

    shown_quest = nullptr;
}

static void remove_target(std::optional<int> index) {
    if (index && *index >= 0) {
        objectives::remove_target(shown_quest, static_cast<std::size_t>(*index));
        questlines::changed();
        steering::changed();
    }
}

static void set_lore() {
    if (void* edit = ui::find_pane(window, "quests_expanded_lore")) {
        std::string lore = ui::edit_text(edit);
        // Braces would read as translation keys wherever the lore is shown.
        std::erase_if(lore, [](char letter) { return letter == '{' || letter == '}'; });
        game::field<game::String>(shown_quest, "mmoQuest::description") = lore;
    }
}


namespace quest_details {

void install() {
    // "QuestsExpanded remove_target 2" or "QuestsExpanded lore" (the lore edited).
    game::in("mmoWindow::UICommand").after([](bool handled, void* shown, game::Record command) {
        if (shown != window || command.label() != "QuestsExpanded") {
            return handled;
        }

        if (refreshing || shown_quest == nullptr) {
            return true;
        }

        if (command.text(0) == "remove_target") {
            remove_target(command.number(1));
        } else if (command.text(0) == "lore") {
            set_lore();
        }

        refresh();

        return true;
    });

    // The window belongs to the card being edited: it closes once that card isn't selected (another is, or another
    // NPC is shown), or the cards are hidden.
    game::in("mmoQuestList::SetSelected").after([](void*, void* card) {
        if (card == nullptr || game::field<game::WeakPointer>(card, "mmoQuestSelector::quest").get() != shown_quest) {
            close();
        }
    });

    game::in("mmoQuestList::OnHide").after([](void*) {
        close();
    });

    // A quest destroyed while its window is open (the card's undo destroys and remakes them too).
    game::on_destroy("mmoQuest", [](void* quest) {
        if (quest == shown_quest) {
            shown_quest = nullptr;
        }
    });

    // Another game starts or loads.
    game::in("mmoModeInGame::DoInit").before([](void*) {
        window = nullptr;
        shown_quest = nullptr;
    });
}

void open(void* quest) {
    shown_quest = quest;
    shown_lore.clear();
    window = ui::open_window(window_id);

    if (window == nullptr) {
        plugin::log("{}.win is missing: add the mod with the mod manager, which puts its windows in place", window_id);

        return;
    }

    refresh();
}

void refresh_if_showing(const void* quest) {
    if (quest == shown_quest) {
        refresh();
    }
}

}
