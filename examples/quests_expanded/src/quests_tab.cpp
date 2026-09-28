#include "quests_tab.h"

#include "line_header.h"
#include "line_toggles.h"
#include "quest_menu.h"
#include "questlines.h"

#include <mt2loader.hpp>

#include <map>

static std::map<void*, void*> npc_in_window;
// Setting a button from here isn't the designer clicking it.
static bool refreshing = false;


static void refresh(void* window) {
    void* npc = npc_in_window[window];

    questlines::update();
    refreshing = true;
    line_header::refresh(window, npc);
    line_toggles::refresh(window, npc);
    refreshing = false;
}

// "QuestsExpanded chapter" (the New chapter toggle), "QuestsExpanded settings" (the cog) or "QuestsExpanded rename" (the
// header's name edited).
static void handle_command(void* window, const game::Record& command) {
    void* npc = npc_in_window[window];

    questlines::update();

    if (command.text(0) == "rename") {
        line_header::rename(window, npc);
    } else if (command.text(0) == "settings") {
        quest_menu::open(npc);
    } else {
        line_toggles::toggle(npc, command.text(0));
    }

    refresh(window);
}


namespace quests_tab {

void install() {
    game::in("_ZN6mmoNPCD1Ev").before([](void* npc) {
        for (auto& [window, shown] : npc_in_window) {
            if (shown == npc) {
                shown = nullptr;
            }
        }
    });

    game::in("mmoModeInGame::DoInit").before([](void*) {
        npc_in_window.clear();
    });

    game::in("mmoNPCInfoWindow::SetNPC").after([](void* window, void* npc) {
        npc_in_window[window] = npc;
        refresh(window);
    });

    game::in("mmoNPCInfoWindow::UpdateContents").after([](void* window) {
        refresh(window);
    });

    game::in("mmoNPCInfoWindow::UICommand").after([](bool handled, void* window, game::Record command) {
        if (command.label() != "QuestsExpanded") {
            return handled;
        }

        if (!refreshing) {
            handle_command(window, command);
        }

        return true;
    });
}

}
