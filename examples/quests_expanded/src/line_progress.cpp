#include "line_progress.h"

#include "player.h"
#include "settings.h"

#include <mt2loader.hpp>

#include <format>
#include <set>
#include <sstream>

constexpr const char* finished_key = "finished";


static int today() {
    void* clock = game::singleton("mmoClock");

    return clock != nullptr ? game::field<int>(clock, "mmoClock::flatDay") : 0;
}

static std::string finish_day_key(int line_id) {
    return std::format("finish_day.{}", line_id);
}

// "3 7": the ids of the main and repeatable questlines the player finished.
static std::set<int> finished_lines(const void* toon) {
    std::istringstream words{ game::saved(toon).get(finished_key, "") };
    std::set<int> ids;
    int id = 0;

    while (words >> id) {
        ids.insert(id);
    }

    return ids;
}

static void keep_finished_lines(const void* toon, const std::set<int>& ids) {
    std::string text;

    for (int id : ids) {
        text += text.empty() ? std::to_string(id) : " " + std::to_string(id);
    }

    game::saved(toon).set(finished_key, text);
}

// A run is the player holding an unfinished quest of the line: from its first quest to its last, they always hold one.
static bool is_in_run(void* toon, const Questline& line) {
    for (void* record : player::records(toon)) {
        void* instance = player::instance_of(record);
        void* quest = player::quest_of(instance);

        if (quest != nullptr && !player::is_finished(instance) && questlines::line_of_quest(quest) == &line) {
            return true;
        }
    }

    return false;
}

static bool is_cooling_down(const void* toon, const Questline& line) {
    if (settings.repeat_cooldown_days <= 0) {
        return false;
    }

    std::optional<int> finished_on = game::saved(toon).find<int>(finish_day_key(line.settings.id));

    return finished_on && today() < *finished_on + settings.repeat_cooldown_days;
}

// The line's first quest giver offers its first quest again, as it does to players who just met it.
static void open_again(void* toon, const Questline& line) {
    if (void* record = player::record_for(toon, line.start)) {
        player::set_step(record, player::met_nothing_taken);
    }
}


namespace line_progress {

bool is_locked_out(void* toon, const Questline& line) {
    if (is_in_run(toon, line)) {
        return true;
    }

    if (!line.settings.repeatable) {
        return finished_lines(toon).contains(line.settings.id);
    }

    return is_cooling_down(toon, line);
}

void finish(void* toon, const Questline& line) {
    if (line.settings.id == 0) {
        return;
    }

    if (line.settings.main || line.settings.repeatable) {
        std::set<int> ids = finished_lines(toon);
        ids.insert(line.settings.id);
        keep_finished_lines(toon, ids);
    }

    if (line.settings.repeatable) {
        if (settings.repeat_cooldown_days > 0) {
            game::saved(toon).set(finish_day_key(line.settings.id), today());
        }

        open_again(toon, line);
    }
}

bool has_finished(const void* toon, const Questline& line) {
    return line.settings.id != 0 && finished_lines(toon).contains(line.settings.id);
}

}
