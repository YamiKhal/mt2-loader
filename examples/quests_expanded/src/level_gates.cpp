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
#include <optional>

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

// The later chapter this quest giver's next quest leads into: the quest right before the chapter's first.
static std::optional<std::size_t> chapter_led_into(void* npc, int next, const Questline& line) {
    void* quest = quest_giver::quest(npc, next);
    auto found = std::find(line.quests.begin(), line.quests.end(), quest);

    if (quest == nullptr || found == line.quests.end()) {
        return std::nullopt;
    }

    std::size_t index = static_cast<std::size_t>(found - line.quests.begin());

    for (std::size_t chapter = 1; chapter < line.chapters.size(); chapter++) {
        if (line.chapters[chapter].first_quest == index + 1) {
            return chapter;
        }
    }

    return std::nullopt;
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
    std::optional<std::size_t> gated_chapter = at_start ? std::optional<std::size_t>(0) : chapter_led_into(npc, next, *line);

    if (gated_chapter && !chapter_gates::lets_in(asking_player, *line, *gated_chapter)) {
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
