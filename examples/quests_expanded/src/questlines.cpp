#include "questlines.h"

#include "line_names.h"
#include "quest.h"
#include "quest_giver.h"

#include <mt2loader.hpp>

#include <unordered_map>
#include <unordered_set>

struct Tables {
    std::vector<Questline> lines;
    std::unordered_map<const void*, HandOff> hand_offs;
    std::unordered_map<const void*, void*> hand_in_senders;
    std::unordered_set<const void*> hand_off_targets;
    std::unordered_set<const void*> handed_in_givers;
    std::unordered_map<const void*, std::size_t> line_of_giver;
    std::unordered_map<const void*, std::size_t> line_of_quest;
    std::unordered_map<const void*, std::size_t> chapter_of_quest;
};

// Every quest in the world that is a hand-off, and who it sends players to.
using HandOffTargets = std::unordered_map<const void*, void*>;

static Tables tables;
static bool traced = false;
// Main questline quests whose "Do quest" advertisement has the main bonus in it.
static std::vector<void*> boosted_quests;


static void* hand_off_target(void* quest, void* giver) {
    if (!quest::is_talk(quest)) {
        return nullptr;
    }

    void* target = quest_giver::at_destination(quest::destination(quest));

    return target != giver ? target : nullptr;
}

// Where each part of a quest giver's chain starts: at its first quest, and right after each of its hand-offs.
static std::vector<int> part_starts(const void* npc, const HandOffTargets& targets) {
    std::vector<int> starts{ 0 };
    int count = quest_giver::quest_count(npc);

    for (int index = 0; index + 1 < count; index++) {
        if (targets.contains(quest_giver::quest(npc, index))) {
            starts.push_back(index + 1);
        }
    }

    return starts;
}

static HandOff follow(void* quest, void* target, std::size_t line, std::unordered_map<const void*, std::size_t>& parts_used,
    const HandOffTargets& targets) {
    HandOff hand_off{ target };
    auto owner = tables.line_of_giver.find(target);

    if (owner != tables.line_of_giver.end() && owner->second != line) {
        hand_off.broken = Broken::other_line;
        tables.lines[owner->second].broken = true;

        return hand_off;
    }

    std::vector<int> parts = part_starts(target, targets);
    std::size_t& used = parts_used[target];

    if (used >= parts.size()) {
        hand_off.broken = Broken::nothing_left;

        return hand_off;
    }

    hand_off.hand_in = parts[used];
    used++;
    tables.hand_in_senders[quest_giver::quest(target, hand_off.hand_in)] = quest::giver(quest);

    return hand_off;
}

// Walks the line part by part: each part runs to a hand-off, which leads to the target's next part this line hasn't
// used, or to the chain's end, where the line ends.
static void trace_line(void* start, const HandOffTargets& targets) {
    std::size_t line = tables.lines.size();
    std::unordered_map<const void*, std::size_t> parts_used{ { start, 1 } };

    Questline new_line;
    new_line.start = start;
    new_line.settings = line_settings::of(start);
    new_line.chapters.push_back(Chapter{ start, 0 });
    tables.lines.push_back(new_line);

    void* giver = start;
    int first = 0;
    std::unordered_set<const void*> handed_in;

    while (giver != nullptr) {
        Questline& current = tables.lines[line];
        int count = quest_giver::quest_count(giver);
        void* next_giver = nullptr;
        int next_first = 0;
        bool ends_in_hand_off = false;

        if (!tables.line_of_giver.contains(giver)) {
            current.givers.push_back(giver);
            tables.line_of_giver[giver] = line;
        }

        bool first_part = giver == start && first == 0;
        bool first_hand_in = !first_part && handed_in.insert(giver).second;

        if (first_hand_in) {
            tables.handed_in_givers.insert(giver);
        }

        if (first_hand_in && line_names::starts_chapter(giver)) {
            current.chapters.push_back(Chapter{ giver, current.quests.size() });
        }

        for (int index = first; index < count && !ends_in_hand_off; index++) {
            void* quest = quest_giver::quest(giver, index);
            auto target = targets.find(quest);

            current.quests.push_back(quest);
            tables.line_of_quest[quest] = line;
            tables.chapter_of_quest[quest] = current.chapters.size() - 1;

            if (target == targets.end()) {
                continue;
            }

            HandOff hand_off = follow(quest, target->second, line, parts_used, targets);
            tables.hand_offs[quest] = hand_off;
            ends_in_hand_off = true;

            if (hand_off.broken != Broken::no) {
                current.broken = true;
            } else {
                next_giver = hand_off.target;
                next_first = hand_off.hand_in;
            }
        }

        if (!ends_in_hand_off) {
            current.last_quest = quest_giver::quest(giver, count - 1);
        }

        giver = next_giver;
        first = next_first;
    }
}

// Each quest makes its advertisements once, so the main bonus goes into them again for quests that joined or left
// the main questline. Quests that are gone are left out.
static void refresh_boosted_quests(const std::unordered_set<const void*>& quests_in_world) {
    std::unordered_set<void*> to_refresh;

    for (void* quest : boosted_quests) {
        if (quests_in_world.contains(quest)) {
            to_refresh.insert(quest);
        }
    }

    boosted_quests.clear();

    for (const auto& [quest, line] : tables.line_of_quest) {
        if (tables.lines[line].settings.main) {
            boosted_quests.push_back(const_cast<void*>(quest));
            to_refresh.insert(const_cast<void*>(quest));
        }
    }

    for (void* quest : to_refresh) {
        quest::refresh_advertisements(quest);
    }
}

static void trace() {
    tables = Tables{};

    std::vector<void*> givers = quest_giver::all();
    std::unordered_set<const void*> quests_in_world;
    HandOffTargets targets;

    for (void* giver : givers) {
        int count = quest_giver::quest_count(giver);

        for (int index = 0; index < count; index++) {
            void* quest = quest_giver::quest(giver, index);
            quests_in_world.insert(quest);

            if (void* target = hand_off_target(quest, giver)) {
                targets[quest] = target;
                tables.hand_off_targets.insert(target);
            }
        }
    }

    for (void* giver : givers) {
        if (quest_giver::quest_count(giver) > 0 && !tables.hand_off_targets.contains(giver)) {
            trace_line(giver, targets);
        }
    }

    // Quest givers that only hand players around in a loop: the first one left starts that loop's line.
    for (void* giver : givers) {
        if (quest_giver::quest_count(giver) > 0 && !tables.line_of_giver.contains(giver)) {
            trace_line(giver, targets);
        }
    }

    traced = true;
    refresh_boosted_quests(quests_in_world);
}

template<class Table>
static const Questline* line_in(const Table& table, const void* key) {
    auto found = table.find(key);

    return found != table.end() ? &tables.lines[found->second] : nullptr;
}


namespace questlines {

void changed() {
    traced = false;
}

void forget_world() {
    tables = Tables{};
    boosted_quests.clear();
    traced = false;
}

bool up_to_date() {
    return traced;
}

void update() {
    if (!traced) {
        trace();
    }
}

const HandOff* hand_off(const void* quest) {
    auto found = tables.hand_offs.find(quest);

    return found != tables.hand_offs.end() ? &found->second : nullptr;
}

void* hand_in_sender(const void* quest) {
    auto found = tables.hand_in_senders.find(quest);

    return found != tables.hand_in_senders.end() ? found->second : nullptr;
}

bool is_hand_in(const void* npc, int index) {
    void* quest = quest_giver::quest(npc, index);

    return quest != nullptr && tables.hand_in_senders.contains(quest);
}

bool is_hand_off_target(const void* npc) {
    return tables.hand_off_targets.contains(npc);
}

bool gets_hand_in(const void* npc) {
    return tables.handed_in_givers.contains(npc);
}

const Questline* line_of_giver(const void* npc) {
    return line_in(tables.line_of_giver, npc);
}

const Questline* line_of_quest(const void* quest) {
    return line_in(tables.line_of_quest, quest);
}

const Questline* main_line_of_quest(const void* quest) {
    const Questline* line = line_of_quest(quest);

    return line != nullptr && line->settings.main ? line : nullptr;
}

std::size_t chapter_of_quest(const void* quest) {
    auto found = tables.chapter_of_quest.find(quest);

    return found != tables.chapter_of_quest.end() ? found->second : 0;
}

std::optional<std::size_t> chapter_started_by(const void* npc) {
    const Questline* line = line_of_giver(npc);

    if (line == nullptr) {
        return std::nullopt;
    }

    for (std::size_t chapter = 1; chapter < line->chapters.size(); chapter++) {
        if (line->chapters[chapter].giver == npc) {
            return chapter;
        }
    }

    return std::nullopt;
}

const std::vector<Questline>& all() {
    return tables.lines;
}

}
