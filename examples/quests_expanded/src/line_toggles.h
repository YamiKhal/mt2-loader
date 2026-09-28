#pragma once

#include <string_view>

// The buttons in the Quests tab header: New chapter on a quest giver a line hands players to, and a cog on every
// quest giver in a line, which opens its settings (quest_menu).
namespace line_toggles {

void refresh(void* window, void* npc);
void toggle(void* npc, std::string_view which);

}
