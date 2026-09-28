#include "quest_card.h"

#include "layout.h"
#include "line_titles.h"
#include "objectives.h"
#include "quest.h"
#include "quest_giver.h"
#include "questlines.h"
#include "ui.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string>

// Which strip the card shows in place of its number.
enum class Strip {
    hand_off,
    hand_in,
    branch,
};

struct Arrow {
    Strip strip = Strip::hand_off;
    std::string icon;
    std::string tooltip;
    std::string details;
};

constexpr std::array<const char*, 3> strip_ids{ "quests_expanded_hand_off", "quests_expanded_hand_in", "quests_expanded_branch" };


static std::string broken_note(Broken broken) {
    switch (broken) {
    case Broken::nothing_left:
        return " {quests_expanded_nothing_left}";
    case Broken::other_line:
        return " {quests_expanded_other_line}";
    default:
        return "";
    }
}

static Arrow hand_off_arrow(const HandOff& hand_off) {
    std::string tooltip = std::format("{{quests_expanded_sends_to}} {}{}", quest_giver::name(hand_off.target), broken_note(hand_off.broken));
    const char* icon = hand_off.broken == Broken::no ? "icon-quests_expanded_hand_off" : "icon-warn";

    return Arrow{ Strip::hand_off, icon, tooltip, "" };
}

// One line per quest giver it can send players to; the warning icon once none leads anywhere.
static Arrow branch_arrow(const std::vector<HandOff>& branches) {
    std::string details;

    for (const HandOff& branch : branches) {
        details += std::format("{}{}{}", details.empty() ? "" : "\n", quest_giver::name(branch.target), broken_note(branch.broken));
    }

    bool leads_somewhere = std::ranges::any_of(branches, [](const HandOff& branch) { return branch.broken == Broken::no; });

    return Arrow{ Strip::branch, leads_somewhere ? "icon-quests_expanded_branch" : "icon-warn", "{quests_expanded_branches_to}", details };
}

static Arrow hand_in_arrow(const void* quest, const std::vector<void*>& senders) {
    std::string names;

    for (void* sender : senders) {
        names += std::format("{}{}", names.empty() ? "" : ", ", quest_giver::name(sender));
    }

    std::string tooltip = std::format("{{quests_expanded_sent_from}} {}", names);
    const Questline* line = questlines::line_of_quest(quest);
    std::size_t chapter = questlines::chapter_of_quest(quest);

    if (line == nullptr || chapter == 0 || line->quests[line->chapters[chapter].first_quest] != quest) {
        return Arrow{ Strip::hand_in, "icon-quests_expanded_hand_in", tooltip, "" };
    }

    std::string details = std::format("{{quests_expanded_starts_chapter}} {}", line_titles::chapter_title(*line, chapter));

    if (line->chapters[chapter].from.size() > 1) {
        details += "\n{quests_expanded_branches_meet}";
    }

    return Arrow{ Strip::hand_in, "icon-quests_expanded_chapter", tooltip, details };
}

static std::optional<Arrow> arrow_for(const void* quest) {
    if (const std::vector<HandOff>* branches = questlines::branches(quest)) {
        return branch_arrow(*branches);
    }

    if (const HandOff* hand_off = questlines::hand_off(quest)) {
        return hand_off_arrow(*hand_off);
    }

    if (const std::vector<void*>* senders = questlines::hand_in_senders(quest)) {
        return hand_in_arrow(quest, *senders);
    }

    return std::nullopt;
}

static void show(void* card, const std::optional<Arrow>& arrow) {
    void* number = ui::find_pane(card, "number");
    std::array<void*, strip_ids.size()> strips{};

    for (std::size_t each = 0; each < strips.size(); each++) {
        strips[each] = ui::find_pane(card, strip_ids[each]);
    }

    if (number == nullptr || std::ranges::find(strips, nullptr) != strips.end()) {
        return;
    }

    ui::set_visible(number, !arrow);

    for (std::size_t each = 0; each < strips.size(); each++) {
        ui::set_visible(strips[each], arrow && static_cast<std::size_t>(arrow->strip) == each);
    }

    if (!arrow) {
        return;
    }

    // Emptied as well, since the hidden number still showed in game. The game writes it again on the next refresh.
    ui::set_text(number, "");

    void* button = strips[static_cast<std::size_t>(arrow->strip)];
    ui::set_icon(button, arrow->icon);
    ui::set_tooltip(button, arrow->tooltip, arrow->details);
}

// The card's last line: where a hand-off carries on, or how many more targets a tour or a branch has.
static void show_more(void* card, const void* quest) {
    void* then = ui::find_pane(card, "and");

    if (then == nullptr) {
        return;
    }

    const HandOff* hand_off = questlines::hand_off(quest);
    std::size_t targets = objectives::targets(quest).size();

    if (hand_off != nullptr && hand_off->broken == Broken::no) {
        ui::set_text(then, "{quests_expanded_and_carry_on}");
    } else if (objectives::of(quest) == Objective::tour && targets > 1) {
        ui::set_text(then, std::format("+{} {{quests_expanded_tour_more}}", targets - 1));
    } else if (objectives::of(quest) == Objective::branch && targets > 1) {
        ui::set_text(then, std::format("+{} {{quests_expanded_branch_more}}", targets - 1));
    }
}

static void refresh(void* card) {
    void* quest = weak_object(card, layout.card_quest);

    if (quest == nullptr) {
        show(card, std::nullopt);

        return;
    }

    questlines::update();
    show(card, arrow_for(quest));
    show_more(card, quest);
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
