#pragma once

// The Quests tab's header names the quest giver's line in place of "Quest Chain:", or in a line of several chapters,
// the quest giver's chapter (with a "!" naming the line). It's a text edit where the name is given: a line's first
// quest giver (the line, or its first chapter) and a quest giver that starts a chapter.
namespace line_header {

void refresh(void* window, void* npc);
void rename(void* window, void* npc);

}
