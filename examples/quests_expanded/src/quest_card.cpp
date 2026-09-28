#include "quest_card.h"

#include "layout.h"
#include "line_titles.h"
#include "quest.h"
#include "quest_giver.h"
#include "questlines.h"
#include "ui.h"

#include <mt2loader.hpp>

#include <format>
#include <optional>
#include <string>

struct Arrow {
    bool hand_off = false;
    std::string icon;
    std::string tooltip;
    std::string details;
};

static Arrow hand_off_arrow(const HandOff& hand_off) {
    std::string target = quest_giver::name(hand_off.target);

    switch (hand_off.broken) {
    case Broken::nothing_left:
        return Arrow{ true, "icon-warn", std::format("{{quests_expanded_sends_to}} {} {{quests_expanded_nothing_left}}", target), "" };
    case Broken::other_line:
        return Arrow{ true, "icon-warn", std::format("{{quests_expanded_sends_to}} {} {{quests_expanded_other_line}}", target), "" };
    default:
        return Arrow{ true, "icon-quests_expanded_hand_off", std::format("{{quests_expanded_sends_to}} {}", target), "" };
    }
}

static Arrow hand_in_arrow(const void* quest, void* sender) {
    std::string tooltip = std::format("{{quests_expanded_sent_from}} {}", quest_giver::name(sender));
    const Questline* line = questlines::line_of_quest(quest);
    std::size_t chapter = questlines::chapter_of_quest(quest);

    if (line == nullptr || chapter == 0 || line->quests[line->chapters[chapter].first_quest] != quest) {
        return Arrow{ false, "icon-quests_expanded_hand_in", tooltip, "" };
    }

    std::string details = std::format("{{quests_expanded_starts_chapter}} {}", line_titles::chapter_title(*line, chapter));

    return Arrow{ false, "icon-quests_expanded_chapter", tooltip, details };
}

static std::optional<Arrow> arrow_for(const void* quest) {
    if (const HandOff* hand_off = questlines::hand_off(quest)) {
        return hand_off_arrow(*hand_off);
    }

    if (void* sender = questlines::hand_in_sender(quest)) {
        return hand_in_arrow(quest, sender);
    }

    return std::nullopt;
}

static void show(void* card, const std::optional<Arrow>& arrow) {
    void* hand_off_button = ui::find_pane(card, "quests_expanded_hand_off");
    void* hand_in_button = ui::find_pane(card, "quests_expanded_hand_in");
    void* number = ui::find_pane(card, "number");

    if (hand_off_button == nullptr || hand_in_button == nullptr || number == nullptr) {
        return;
    }

    ui::set_visible(number, !arrow);
    ui::set_visible(hand_off_button, arrow && arrow->hand_off);
    ui::set_visible(hand_in_button, arrow && !arrow->hand_off);

    if (!arrow) {
        return;
    }

    // Emptied as well, since the hidden number still showed in game. The game writes it again on the next refresh.
    ui::set_text(number, "");

    void* button = arrow->hand_off ? hand_off_button : hand_in_button;
    ui::set_icon(button, arrow->icon);
    ui::set_tooltip(button, arrow->tooltip, arrow->details);
}

static void refresh(void* card) {
    void* quest = weak_object(card, layout.card_quest);

    if (quest == nullptr) {
        show(card, std::nullopt);

        return;
    }

    questlines::update();
    show(card, arrow_for(quest));

    const HandOff* hand_off = questlines::hand_off(quest);
    void* then = ui::find_pane(card, "and");

    if (hand_off != nullptr && hand_off->broken == Broken::no && then != nullptr) {
        ui::set_text(then, "{quests_expanded_and_carry_on}");
    }
}


namespace quest_card {

void install() {
    game::in("mmoQuestSelector::RefreshQuest").after([](void* card) {
        refresh(card);
    });

    // A card is reused as the empty slot or the "Add new quest" one without a refresh, and those are see-through.
    for (const char* empty_slot : { "mmoQuestSelector::SetupAsAdd", "mmoQuestSelector::SetupAsBlank" }) {
        game::in(empty_slot).after([](void* card) {
            show(card, std::nullopt);
        });
    }
}

}
