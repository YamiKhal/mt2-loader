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

// A quest that sends players on to another quest giver ("talk to them"), and the quest they're given there. A branch
// quest has one for each quest giver it can send players to.
struct HandOff {
    void* target = nullptr;
    int hand_in = -1;
    Broken broken = Broken::no;
};

// A stretch of a line: its first quest giver's quests from a hand-in (or the line's start) to the next hand-off, then
// on through hand-offs to chapters of their own. A chapter starts at the line's start, at each branch of a branch quest,
// where branches meet again, and at a hand-in the designer marked (line_names). Its quests are
// line.quests[first_quest, the next chapter's first_quest).
struct Chapter {
    void* giver = nullptr;
    std::size_t first_quest = 0;
    // The chapters that hand players on to this one: none for the first, several where branches meet.
    std::vector<std::size_t> from;
    // Started by a branch or where branches meet, rather than by the designer.
    bool automatic = false;
    // The branch quest whose branch this chapter is, if it's one (a branch another branch runs into is still one).
    const void* branch_of = nullptr;
    // What players read: its depth in the line from 1, and a letter where several chapters share it ("3a", "3b").
    int number = 1;
    char letter = '\0';
    // Its last quest ends the line (or one of its ends, for a line whose branches never meet again).
    bool ending = false;
};

// The chain parts joined by hand-offs, from a quest giver no one sends players to (or, for a loop of hand-offs, from
// one of its quest givers). A branch quest makes it a graph; its chapters are laid out so each one's quests run
// together, a chapter after the ones that lead to it.
struct Questline {
    void* start = nullptr;
    LineSettings settings;
    std::vector<void*> givers;
    std::vector<void*> quests;
    std::vector<Chapter> chapters;
    // The last quest of each way through the line.
    std::vector<void*> ends;
    bool broken = false;
};

// The world's questlines, worked out from the quest givers' chains and never saved: traced when a game loads and
// again after a chain changes, when next needed.
namespace questlines {

void changed();
void forget_world();
bool up_to_date();
void update();

// A plain hand-off (a branch quest has branches instead).
const HandOff* hand_off(const void* quest);
// A branch quest's branches, one for each quest giver it can send players to, in the order of its targets.
const std::vector<HandOff>* branches(const void* quest);
// The quest givers whose hand-offs lead to this quest (several where branches meet).
const std::vector<void*>* hand_in_senders(const void* quest);
bool is_hand_in(const void* npc, int index);
bool is_hand_off_target(const void* npc);
// Whether a line hands players to this quest giver, which can then start a chapter.
bool gets_hand_in(const void* npc);
// Whether this quest giver's first hand-in starts a chapter anyway (a branch or where branches meet).
bool starts_chapter_anyway(const void* npc);

const Questline* line_of_giver(const void* npc);
const Questline* line_of_quest(const void* quest);
const Questline* main_line_of_quest(const void* quest);

// Which of its line's chapters a quest is in, from 0.
std::size_t chapter_of_quest(const void* quest);
// The chapter a quest giver's first hand-in starts, if it starts one.
std::optional<std::size_t> chapter_started_by(const void* npc);
// The chapters a hand-off or branch quest leads into (none for a hand-off within a chapter).
std::vector<std::size_t> chapters_led_into(const void* quest);
bool is_end(const Questline& line, const void* quest);

// Every traced line, for the Quests window.
const std::vector<Questline>& all();

}
