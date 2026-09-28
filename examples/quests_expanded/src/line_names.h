#pragma once

#include <string>
#include <string_view>

// The names the designer gives questlines and their chapters, kept in the saved game with the quest givers: a line's
// name and its first chapter's name on its first quest giver, and a later chapter's start and name on the quest giver
// whose first hand-in starts it. Empty names aren't kept.
namespace line_names {

std::string line_name(const void* start);
void set_line_name(void* start, std::string_view name);

bool starts_chapter(const void* npc);
void set_starts_chapter(void* npc, bool starts);
std::string chapter_name(const void* npc);
void set_chapter_name(void* npc, std::string_view name);
// Apart from chapter_name: a loop can hand players back to its first quest giver to start a later chapter there.
std::string first_chapter_name(const void* start);
void set_first_chapter_name(void* start, std::string_view name);

}
