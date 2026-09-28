#pragma once

// What the designer set on a chain's first quest giver (its Quests tab), kept in the saved game with that quest
// giver: whether the questline it starts is the main one, whether players can repeat it, and the id players' progress
// refers to.
struct LineSettings {
    bool main = false;
    bool repeatable = false;
    int id = 0;
};

namespace line_settings {

LineSettings of(const void* start);
void set_main(void* start, bool main);
void set_repeatable(void* start, bool repeatable);

}
