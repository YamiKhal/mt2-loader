#pragma once

#include "questlines.h"

#include <cstddef>
#include <optional>

// Who may start a chapter: from a level up, and of one class. A chapter the designer hasn't set keeps the chapter
// before's gate, so a new chapter needs no setting unless it should differ. Kept in the saved game with the chapter's
// quest giver (the line's first quest giver for the first chapter).
struct ChapterGate {
    // 0: no level gate.
    int level = 0;
    // A player class's index (player_classes); none: any class.
    std::optional<int> player_class;
};

namespace chapter_gates {

ChapterGate of(const Questline& line, std::size_t chapter);
void set_level(const Questline& line, std::size_t chapter, int level);
void set_class(const Questline& line, std::size_t chapter, std::optional<int> player_class);

// A class that no longer exists lets everyone in.
bool lets_in(const void* toon, const Questline& line, std::size_t chapter);

}
