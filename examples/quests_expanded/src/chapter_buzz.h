#pragma once

#include "chapter_quality.h"
#include "questlines.h"

#include <cstddef>
#include <string>

// What players say about a chapter, as quotes: one per thing they'd change, from its quality (a side line's asked
// for less), or praise when there's nothing. A branch no one reaches while another is taken, and an ending in a line
// with several, have a say too.
namespace chapter_buzz {

std::string of(const Questline& line, std::size_t chapter);

}
