#pragma once

#include "questlines.h"

#include <cstddef>

enum class Rating {
    poor,
    fine,
    great,
};

// A good chapter length, in quests. Quests have no stages, so one errand ("talk to him, come back, search the house")
// takes several, and a quest giver has up to 5.
struct LengthRange {
    int shortest = 0;
    int longest = 0;
};

constexpr LengthRange main_length{ 16, 80 };
constexpr LengthRange side_length{ 6, 30 };

// How good a chapter is to play, worked out from its quests whenever it's asked (a chapter is a handful of quests).
// Out of 100, a main line's: kinds of quest (up to 40), an elite to kill (15), a good length (main_length, 20), levels
// that climb (15), and more than one quest giver (10). A side line may be small and one-note: two kinds are enough for
// the 40, side_length is a good length, and it isn't asked for more than one quest giver. A hand-off that leads nowhere
// makes either poor.
struct Quality {
    bool main = false;
    int score = 0;
    Rating rating = Rating::poor;
    int kinds = 0;
    bool elite = false;
    int length = 0;
    bool climbs = false;
    int givers = 0;
    bool broken = false;
};

namespace chapter_quality {

constexpr const LengthRange& length_range(bool main) {
    return main ? main_length : side_length;
}

Quality of(const Questline& line, std::size_t chapter);

// Where a chapter's quests are in line.quests: [first, end).
std::size_t first_quest(const Questline& line, std::size_t chapter);
std::size_t end_quest(const Questline& line, std::size_t chapter);

}
