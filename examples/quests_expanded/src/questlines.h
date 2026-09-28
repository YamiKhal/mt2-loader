#pragma once

#include "line_settings.h"

#include <cstddef>
#include <optional>
#include <vector>

// Why a hand-off leads nowhere.
enum class Broken {
    no,
    nothing_left,
    other_line,
};

// A quest that sends players on to another quest giver ("talk to them"), and the quest they're given there.
struct HandOff {
    void* target = nullptr;
    int hand_in = -1;
    Broken broken = Broken::no;
};

// Where a chapter starts: the line's first quest, or the first hand-in of a quest giver the designer marked
// (line_names).
struct Chapter {
    void* giver = nullptr;
    std::size_t first_quest = 0;
};

// The chain parts joined by hand-offs, from a quest giver no one sends players to (or, for a loop of hand-offs, from
// one of its quest givers).
struct Questline {
    void* start = nullptr;
    LineSettings settings;
    std::vector<void*> givers;
    std::vector<void*> quests;
    std::vector<Chapter> chapters;
    void* last_quest = nullptr;
    bool broken = false;
};

// The world's questlines, worked out from the quest givers' chains and never saved: traced when a game loads and
// again after a chain changes, when next needed.
namespace questlines {

void changed();
void forget_world();
bool up_to_date();
void update();

const HandOff* hand_off(const void* quest);
void* hand_in_sender(const void* quest);
bool is_hand_in(const void* npc, int index);
bool is_hand_off_target(const void* npc);
// Whether a line hands players to this quest giver, which can then start a chapter.
bool gets_hand_in(const void* npc);

const Questline* line_of_giver(const void* npc);
const Questline* line_of_quest(const void* quest);
const Questline* main_line_of_quest(const void* quest);

// Which of its line's chapters a quest is in, from 0.
std::size_t chapter_of_quest(const void* quest);
// The chapter a quest giver's first hand-in starts, if the designer made it start one.
std::optional<std::size_t> chapter_started_by(const void* npc);

// Every traced line, for the Quests window.
const std::vector<Questline>& all();

}
