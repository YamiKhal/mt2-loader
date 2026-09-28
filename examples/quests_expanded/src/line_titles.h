#pragma once

#include "questlines.h"

#include <cstddef>
#include <string>

// What the player reads for a line or a chapter: the name the designer gave it, or a default made from where it is
// ("Quests from Alda", "Chapter 2").
namespace line_titles {

std::string default_line_title(const Questline& line);
std::string line_title(const Questline& line);
void rename_line(const Questline& line, std::string name);

std::string default_chapter_title(std::size_t chapter);
std::string chapter_title(const Questline& line, std::size_t chapter);
void rename_chapter(const Questline& line, std::size_t chapter, std::string name);

}
