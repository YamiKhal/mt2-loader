#include "level_gates.h"

#include "chapter_gates.h"
#include "hand_ins.h"
#include "line_progress.h"
#include "player.h"
#include "quest.h"
#include "quest_giver.h"
#include "questlines.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <climits>
#include <vector>

// mmoRange, as mmoRange::Contains reads it.
struct Range {
    int minimum;
    int maximum;
    bool valid;
};

// The player whose search, plan or quest taking is asking.
static thread_local void* asking_player = nullptr;


static Range any_level() {
    return Range{ INT_MIN, INT_MAX, true };
}

static Range no_one() {
    return Range{ 0, 0, false };
}

static Range main_line_start(void* npc, const Questline& line) {
    if (line_progress::is_locked_out(asking_player, line)) {
        return no_one();
    }

    int gate = chapter_gates::of(line, 0).level;

    return Range{ gate > 0 ? gate : quest::min_level(quest_giver::quest(npc, 0)), INT_MAX, true };
}

// A level gate replaces the lowest level the game worked out from the line's content.
static Range side_line_start(const Questline& line, const Range& game_range) {
    int gate = chapter_gates::of(line, 0).level;

    if (gate <= 0 || !game_range.valid) {
        return game_range;
    }

    return Range{ gate, std::max(game_range.maximum, gate), true };
}

// Whether a player may take this quest giver's next quest: a hand-off into a later chapter needs that chapter's gate to
// let them in, a branch quest one of its chapters' (they take a branch they're let into).
static bool lets_on(void* npc, int next, const Questline& line) {
    void* quest = quest_giver::quest(npc, next);
    std::vector<std::size_t> chapters = quest != nullptr ? questlines::chapters_led_into(quest) : std::vector<std::size_t>{};

    return chapters.empty() || std::ranges::any_of(chapters, [&](std::size_t chapter) {
        return chapter_gates::lets_in(asking_player, line, chapter);
    });
}

static Range range_for(void* npc, const Range& game_range) {
    questlines::update();

    // Any line's quest giver: a plain chain can have gates too, not only the ones the mod otherwise manages.
    const Questline* line = questlines::line_of_giver(npc);

    if (line == nullptr || asking_player == nullptr) {
        return game_range;
    }

    if (hand_ins::is_giving(npc)) {
        return any_level();
    }

    int next = player::next_quest_index(asking_player, npc);

    if (questlines::is_hand_in(npc, next)) {
        return no_one();
    }

    bool at_start = npc == line->start && next == 0;
    bool let_on = at_start ? chapter_gates::lets_in(asking_player, *line, 0) : lets_on(npc, next, *line);

    if (!let_on) {
        return no_one();
    }

    if (line->settings.main) {
        return at_start ? main_line_start(npc, *line) : any_level();
    }

    if (at_start && line->settings.repeatable && line_progress::is_locked_out(asking_player, *line)) {
        return no_one();
    }

    return at_start ? side_line_start(*line, game_range) : game_range;
}


namespace level_gates {

void install() {
    for (const char* asking : { "mmoToon::FindNearbyNewQuestGiver", "mmoToon::GetSelfAdvertisements" }) {
        game::in(asking).before([](void* toon) {
            asking_player = toon;
        });
    }

    game::in("mmoToon::CollectQuestFromQuestGiver").before([](void* toon, void*) {
        asking_player = toon;
    });

    for (const char* asking : { "mmoToon::FindNearbyNewQuestGiver", "mmoToon::GetSelfAdvertisements", "mmoToon::CollectQuestFromQuestGiver" }) {
        game::in(asking).call("mmoNPC::GetQuestLevelRange").hook<Range(const void* npc)>([](auto level_range, const void* npc) {
            return range_for(const_cast<void*>(npc), level_range(npc));
        });
    }
}

}
