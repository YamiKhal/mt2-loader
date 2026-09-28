#pragma once

#include "chapter_quality.h"

#include <string>

// What players say online about a chapter: one remark for each thing that would raise its score, as the Quests
// report's Buzz tooltip shows them.
namespace chapter_buzz {

std::string of(const Quality& quality);

}
