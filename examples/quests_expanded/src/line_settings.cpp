#include "line_settings.h"

#include <mt2loader.hpp>

// Ids start at 1: 0 is "none yet".
static int new_line_id() {
    game::Saved game_state = game::saved();
    int id = game_state.get("next_line", 1);

    game_state.set("next_line", id + 1);

    return id;
}

// The id stays when the line stops being main or repeatable, so players who finished it stay finished if it's set again.
static void give_id(void* start) {
    game::Saved saved = game::saved(start);

    if (saved.get("line", 0) == 0) {
        saved.set("line", new_line_id());
    }
}

// Only what differs from the default is kept.
static void keep(void* start, const char* key, bool value) {
    if (value) {
        game::saved(start).set(key, true);
    } else {
        game::saved(start).erase(key);
    }
}


namespace line_settings {

LineSettings of(const void* start) {
    game::Saved saved = game::saved(start);
    LineSettings settings;

    settings.main = saved.get("main", false);
    settings.repeatable = saved.get("repeatable", false);
    settings.id = saved.get("line", 0);

    return settings;
}

void set_main(void* start, bool main) {
    if (main) {
        give_id(start);
    }

    keep(start, "main", main);
}

void set_repeatable(void* start, bool repeatable) {
    if (repeatable) {
        give_id(start);
    }

    keep(start, "repeatable", repeatable);
}

}
