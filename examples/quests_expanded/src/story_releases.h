#pragma once

#include "questlines.h"

// A new chapter of a main questline is a release feature, as a new NPC type is: it shows in the release's changes,
// adds its points, and meets the "New story chapter" demand. A chapter counts once it isn't poor
// (chapter_quality). How many chapters each main line had at its last release is kept on its first quest giver; a line
// seen for the first time starts with the chapters it has.
namespace story_releases {

void install();

// Chapters of a main line that a release hasn't brought yet.
int unreleased_chapters(const Questline& line);

}
