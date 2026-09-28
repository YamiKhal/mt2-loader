#include "quest_menu.h"

#include "chapter_gates.h"
#include "line_settings.h"
#include "line_titles.h"
#include "player_classes.h"
#include "quest_giver.h"
#include "questlines.h"
#include "story_demand.h"
#include "ui.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <vector>

constexpr const char* window_id = "quest_settings";

// The window once it has been opened, and the quest giver it shows.
static void* menu = nullptr;
static void* shown_npc = nullptr;
// The class dropdown's choices after "Any class".
static std::vector<PlayerClass> listed_classes;
// Setting the window's controls from here isn't the designer changing them.
static bool refreshing = false;


// The first chapter at the line's first quest giver, the chapter a quest giver starts, or else the chapter of its
// first quest.
static std::size_t chapter_of(void* npc, const Questline& line) {
    if (npc == line.start) {
        return 0;
    }

    if (std::optional<std::size_t> chapter = questlines::chapter_started_by(npc)) {
        return *chapter;
    }

    void* first_quest = quest_giver::quest(npc, 0);

    return first_quest != nullptr ? questlines::chapter_of_quest(first_quest) : 0;
}

static const Questline* shown_line() {
    questlines::update();

    return shown_npc != nullptr ? questlines::line_of_giver(shown_npc) : nullptr;
}

static void set_text(const char* id, std::string_view text) {
    if (void* pane = ui::find_pane(menu, id)) {
        ui::set_text(pane, text);
    }
}

static void fill_class_choices(void* dropdown, const ChapterGate& gate) {
    std::string any = ui::translated("{quests_expanded_any_class}");
    std::vector<std::string> choices{ any };
    std::string selected = any;

    listed_classes = player_classes::all();

    for (const PlayerClass& each : listed_classes) {
        choices.push_back(each.name);

        if (gate.player_class == each.index) {
            selected = each.name;
        }
    }

    ui::set_choices(dropdown, choices);
    ui::select_choice(dropdown, selected);
}

// The class dropdown is filled only when the window opens: a pick reaches the command from inside the dropdown, which
// is still reading its list, so refilling it then frees what it reads (the game crashed).
static void refresh(bool fill_dropdown) {
    const Questline* line = shown_line();

    if (menu == nullptr || line == nullptr) {
        return;
    }

    std::size_t chapter = chapter_of(shown_npc, *line);
    bool chapters = line->chapters.size() > 1;
    std::string title = chapters ? line_titles::chapter_title(*line, chapter) : line_titles::line_title(*line);
    ChapterGate gate = chapter_gates::of(*line, chapter);
    void* main = ui::find_pane(menu, "quests_expanded_menu_main");
    void* repeatable = ui::find_pane(menu, "quests_expanded_menu_repeatable");
    void* level = ui::find_pane(menu, "quests_expanded_menu_level");
    void* player_class = ui::find_pane(menu, "quests_expanded_menu_class");

    refreshing = true;
    ui::set_window_title(menu, "{quests_expanded_menu_title} " + title);
    set_text("quests_expanded_menu_line", "{quests_expanded_part_of} " + line_titles::line_title(*line));
    set_text("quests_expanded_menu_chapter", chapters ? title : "{quests_expanded_menu_whole_line}");

    if (main != nullptr && repeatable != nullptr) {
        ui::set_checked(main, line->settings.main);
        ui::set_checked(repeatable, line->settings.repeatable);
    }

    if (level != nullptr) {
        ui::set_edit_text(level, std::to_string(gate.level));
    }

    if (player_class != nullptr && fill_dropdown) {
        fill_class_choices(player_class, gate);
    }

    refreshing = false;
}

static void set_main(bool main) {
    const Questline* line = shown_line();

    if (line != nullptr) {
        line_settings::set_main(line->start, main);
        questlines::changed();
        // The story demand comes and goes with the main questline.
        story_demand::refresh();
    }
}

static void set_repeatable(bool repeatable) {
    const Questline* line = shown_line();

    if (line != nullptr) {
        line_settings::set_repeatable(line->start, repeatable);
        questlines::changed();
    }
}

// Anything but a number keeps the gate as it was.
static void set_level() {
    const Questline* line = shown_line();
    void* edit = ui::find_pane(menu, "quests_expanded_menu_level");

    if (line == nullptr || edit == nullptr) {
        return;
    }

    std::string text = ui::edit_text(edit);
    int level = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), level);

    if (error == std::errc() && end == text.data() + text.size()) {
        chapter_gates::set_level(*line, chapter_of(shown_npc, *line), level);
    }
}

static void set_class() {
    const Questline* line = shown_line();
    void* dropdown = ui::find_pane(menu, "quests_expanded_menu_class");

    if (line == nullptr || dropdown == nullptr) {
        return;
    }

    std::string selected = ui::selected_choice(dropdown);
    auto found = std::find_if(listed_classes.begin(), listed_classes.end(), [&](const PlayerClass& each) { return each.name == selected; });
    std::optional<int> player_class = found != listed_classes.end() ? std::optional<int>(found->index) : std::nullopt;

    chapter_gates::set_class(*line, chapter_of(shown_npc, *line), player_class);
}


namespace quest_menu {

void install() {
    game::in("_ZN6mmoNPCD1Ev").before([](void* npc) {
        if (npc == shown_npc) {
            shown_npc = nullptr;
        }
    });

    game::in("mmoModeInGame::DoInit").before([](void*) {
        menu = nullptr;
        shown_npc = nullptr;
    });

    // "QuestsExpanded menu_main 1" and "QuestsExpanded menu_repeatable 0" (a checkbox adds its value),
    // "QuestsExpanded menu_level" (the level edited) or "QuestsExpanded menu_class" (a class picked).
    game::in("mmoWindow::UICommand").after([](bool handled, void* window, game::Record command) {
        if (window != menu || command.label() != "QuestsExpanded") {
            return handled;
        }

        if (refreshing) {
            return true;
        }

        std::optional<int> checked = command.number(1);

        if (command.text(0) == "menu_main" && checked) {
            set_main(*checked != 0);
        } else if (command.text(0) == "menu_repeatable" && checked) {
            set_repeatable(*checked != 0);
        } else if (command.text(0) == "menu_level") {
            set_level();
        } else if (command.text(0) == "menu_class") {
            set_class();
        }

        refresh(false);

        return true;
    });
}

void open(void* npc) {
    shown_npc = npc;
    menu = ui::open_window(window_id);

    if (menu == nullptr) {
        plugin::log("{}.win is missing: add the mod with the mod manager, which puts its windows in place", window_id);

        return;
    }

    refresh(true);
}

}
