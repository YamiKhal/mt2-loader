#pragma once

#include "player_types.h"
#include "questlines.h"

#include <cstddef>
#include <vector>

// What draws players to a branch: the player types that lean to it over the other branches of its branch quest, as
// players choose (chapter_appeal of each branch's first chapter).
namespace branch_pull {

// Empty for a chapter that isn't a branch, and for one that draws every type as its other branches do.
std::vector<PlayerType> of(const Questline& line, std::size_t chapter);

}
