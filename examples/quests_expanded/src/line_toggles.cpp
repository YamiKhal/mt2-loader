#include "line_toggles.h"

#include "line_names.h"
#include "questlines.h"
#include "ui.h"

#include <format>

// The window file has the cog in each place along the buttons' row, counted from the right.
constexpr int settings_places = 2;

static bool can_start_chapter(const void* npc) {
    return npc != nullptr && questlines::gets_hand_in(npc);
}

// The cog opens the quest giver's line and chapter settings (quest_menu), next to the New chapter button when shown.
static void show_settings(void* window, void* npc, int buttons_shown) {
    bool shown = npc != nullptr && questlines::line_of_giver(npc) != nullptr;

    for (int place = 0; place < settings_places; place++) {
        if (void* button = ui::find_pane(window, std::format("quests_expanded_settings_{}", place))) {
            ui::set_visible(button, shown && place == buttons_shown);
        }
    }
}


namespace line_toggles {

void refresh(void* window, void* npc) {
    void* chapter_button = ui::find_pane(window, "quests_expanded_chapter");

    if (chapter_button == nullptr) {
        return;
    }

    bool chapter = can_start_chapter(npc);
    ui::set_visible(chapter_button, chapter);

    if (chapter) {
        ui::set_toggled(chapter_button, line_names::starts_chapter(npc));
    }

    show_settings(window, npc, chapter ? 1 : 0);
}

void toggle(void* npc, std::string_view which) {
    if (which == "chapter" && can_start_chapter(npc)) {
        line_names::set_starts_chapter(npc, !line_names::starts_chapter(npc));
        questlines::changed();
    }
}

}
