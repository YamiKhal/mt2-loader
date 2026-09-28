#include "questlines.h"

#include "line_names.h"
#include "objectives.h"
#include "quest.h"
#include "quest_giver.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

struct Tables {
    std::vector<Questline> lines;
    std::unordered_map<const void*, HandOff> hand_offs;
    std::unordered_map<const void*, std::vector<HandOff>> branches;
    std::unordered_map<const void*, std::vector<void*>> hand_in_senders;
    std::unordered_map<const void*, std::vector<std::size_t>> chapters_led_into;
    std::unordered_set<const void*> hand_off_targets;
    std::unordered_set<const void*> handed_in_givers;
    std::unordered_set<const void*> automatic_chapter_givers;
    std::unordered_map<const void*, std::size_t> line_of_giver;
    std::unordered_map<const void*, std::size_t> line_of_quest;
    std::unordered_map<const void*, std::size_t> chapter_of_quest;
};

// Where each quest that ends a part of a chain sends players: one quest giver for a hand-off, 2 to 4 for a branch.
using HandOffTargets = std::unordered_map<const void*, std::vector<void*>>;

// How many of each quest giver's chain parts a way through a line has used: a hand-off to a quest giver leads to its
// next part. Each branch goes on with its own count, so branches handing off to the same quest giver meet there.
using UsedParts = std::unordered_map<const void*, std::size_t>;

struct Edge {
    std::size_t to = 0;
    bool branch = false;
};

// A part of a quest giver's chain, as a line reaches it: its quests from a hand-in (or the start) to its hand-off.
struct Part {
    void* giver = nullptr;
    int first = 0;
    std::vector<void*> quests;
    std::vector<Edge> next;
    int leading_in = 0;
    bool by_branch = false;
    // The branch quest that first reached it, for a branch.
    const void* branch_of = nullptr;
    bool first_hand_in = false;
    bool ends_line = false;
};

struct LineTrace {
    std::size_t line = 0;
    std::vector<Part> parts;
    std::map<std::pair<const void*, int>, std::size_t> part_at;
    std::unordered_set<const void*> handed_in;
};

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

// A branch quest's quest givers, in its targets' order, its own first: without it, it's no hand-off at all.
static std::vector<void*> branch_targets(void* quest, void* giver) {
    std::vector<void*> givers;

    if (hand_off_target(quest, giver) == nullptr) {
        return givers;
    }

    for (void* target : objectives::targets(quest)) {
        void* npc = quest_giver::at_destination(target);

        if (npc != nullptr && npc != giver && std::ranges::find(givers, npc) == givers.end()) {
            givers.push_back(npc);
        }
    }

    return givers;
}

static std::vector<void*> targets_of(void* quest, void* giver) {
    if (objectives::of(quest) == Objective::branch) {
        return branch_targets(quest, giver);
    }

    void* target = hand_off_target(quest, giver);

    return target != nullptr ? std::vector<void*>{ target } : std::vector<void*>{};
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

static void add_sender(void* hand_in, void* sender) {
    std::vector<void*>& senders = tables.hand_in_senders[hand_in];

    if (std::ranges::find(senders, sender) == senders.end()) {
        senders.push_back(sender);
    }
}

static HandOff follow(void* quest, void* target, std::size_t line, UsedParts& used, const HandOffTargets& targets) {
    HandOff hand_off{ target };
    auto owner = tables.line_of_giver.find(target);

    if (owner != tables.line_of_giver.end() && owner->second != line) {
        hand_off.broken = Broken::other_line;
        tables.lines[owner->second].broken = true;

        return hand_off;
    }

    std::vector<int> parts = part_starts(target, targets);
    std::size_t& next = used[target];

    if (next >= parts.size()) {
        hand_off.broken = Broken::nothing_left;

        return hand_off;
    }

    hand_off.hand_in = parts[next];
    next++;
    add_sender(quest_giver::quest(target, hand_off.hand_in), quest::giver(quest));

    return hand_off;
}

// A part is traced once: a hand-off to a part already reached (from another branch) leads there too.
static std::size_t visit(LineTrace& trace, void* giver, int first, UsedParts& used, const HandOffTargets& targets);

static void follow_on(LineTrace& trace, std::size_t from, void* quest, const std::vector<void*>& to, UsedParts& used,
    const HandOffTargets& targets) {
    Questline& line = tables.lines[trace.line];
    bool fork = to.size() > 1;
    std::vector<HandOff> hand_offs;

    for (void* target : to) {
        UsedParts branch_used = fork ? used : UsedParts{};
        UsedParts& this_way = fork ? branch_used : used;
        HandOff hand_off = follow(quest, target, trace.line, this_way, targets);
        hand_offs.push_back(hand_off);

        if (hand_off.broken != Broken::no) {
            line.broken = true;

            continue;
        }

        std::size_t child = visit(trace, hand_off.target, hand_off.hand_in, this_way, targets);
        trace.parts[child].leading_in++;
        trace.parts[child].by_branch = trace.parts[child].by_branch || fork;

        if (fork && trace.parts[child].branch_of == nullptr) {
            trace.parts[child].branch_of = quest;
        }
        trace.parts[from].next.push_back(Edge{ child, fork });
    }

    if (fork) {
        tables.branches[quest] = hand_offs;
    } else {
        tables.hand_offs[quest] = hand_offs.front();
    }
}

static std::size_t visit(LineTrace& trace, void* giver, int first, UsedParts& used, const HandOffTargets& targets) {
    auto key = std::make_pair(static_cast<const void*>(giver), first);

    if (auto found = trace.part_at.find(key); found != trace.part_at.end()) {
        return found->second;
    }

    std::size_t index = trace.parts.size();
    Part part;
    part.giver = giver;
    part.first = first;
    trace.parts.push_back(part);
    trace.part_at[key] = index;

    Questline& line = tables.lines[trace.line];

    if (!tables.line_of_giver.contains(giver)) {
        line.givers.push_back(giver);
        tables.line_of_giver[giver] = trace.line;
    }

    bool starts_line = giver == line.start && first == 0;

    if (!starts_line && trace.handed_in.insert(giver).second) {
        trace.parts[index].first_hand_in = true;
        tables.handed_in_givers.insert(giver);
    }

    int count = quest_giver::quest_count(giver);

    for (int each = first; each < count; each++) {
        void* quest = quest_giver::quest(giver, each);
        trace.parts[index].quests.push_back(quest);
        tables.line_of_quest[quest] = trace.line;

        if (auto target = targets.find(quest); target != targets.end()) {
            follow_on(trace, index, quest, target->second, used, targets);

            return index;
        }
    }

    trace.parts[index].ends_line = true;

    return index;
}

static bool starts_chapter(const Part& part, std::size_t index) {
    bool marked = part.first_hand_in && line_names::starts_chapter(part.giver);

    return index == 0 || part.by_branch || part.leading_in > 1 || marked;
}

// A chapter goes after every chapter leading to it; among those free to go, the one reached first.
static std::vector<std::size_t> chapter_order(const std::vector<std::vector<std::size_t>>& from) {
    std::vector<std::size_t> order;
    std::vector<bool> placed(from.size(), false);

    while (order.size() < from.size()) {
        std::size_t next = from.size();

        for (std::size_t chapter = 0; chapter < from.size() && next == from.size(); chapter++) {
            bool free = !placed[chapter] && std::ranges::all_of(from[chapter], [&](std::size_t before) { return placed[before]; });

            if (free) {
                next = chapter;
            }
        }

        // Only a loop of chapters leaves none free; it keeps the order they were reached in.
        if (next == from.size()) {
            next = static_cast<std::size_t>(std::ranges::find(placed, false) - placed.begin());
        }

        placed[next] = true;
        order.push_back(next);
    }

    return order;
}

static void number_chapters(Questline& line) {
    std::map<int, std::vector<std::size_t>> by_number;

    for (std::size_t chapter = 0; chapter < line.chapters.size(); chapter++) {
        Chapter& current = line.chapters[chapter];

        for (std::size_t before : current.from) {
            current.number = std::max(current.number, line.chapters[before].number + 1);
        }

        by_number[current.number].push_back(chapter);
    }

    for (const auto& [number, chapters] : by_number) {
        for (std::size_t each = 0; chapters.size() > 1 && each < chapters.size() && each < 26; each++) {
            line.chapters[chapters[each]].letter = static_cast<char>('a' + each);
        }
    }
}

// Groups the parts into chapters, lays the chapters out and lists the line's quests in their order.
static void lay_out(LineTrace& trace) {
    Questline& line = tables.lines[trace.line];
    std::vector<std::size_t> chapter_of_part(trace.parts.size(), 0);
    std::vector<std::vector<std::size_t>> parts_of_chapter;

    for (std::size_t part = 0; part < trace.parts.size(); part++) {
        if (!starts_chapter(trace.parts[part], part)) {
            continue;
        }

        std::vector<std::size_t> run{ part };

        // On through plain hand-offs, until a part that starts a chapter of its own.
        while (trace.parts[run.back()].next.size() == 1) {
            std::size_t child = trace.parts[run.back()].next.front().to;

            if (starts_chapter(trace.parts[child], child)) {
                break;
            }

            run.push_back(child);
        }

        for (std::size_t each : run) {
            chapter_of_part[each] = parts_of_chapter.size();
        }

        parts_of_chapter.push_back(run);
    }

    std::vector<std::vector<std::size_t>> from(parts_of_chapter.size());

    for (std::size_t part = 0; part < trace.parts.size(); part++) {
        for (const Edge& edge : trace.parts[part].next) {
            std::size_t to = chapter_of_part[edge.to];
            std::size_t here = chapter_of_part[part];

            if (to != here && std::ranges::find(from[to], here) == from[to].end()) {
                from[to].push_back(here);
            }
        }
    }

    std::vector<std::size_t> order = chapter_order(from);
    std::vector<std::size_t> placed_as(order.size());

    for (std::size_t place = 0; place < order.size(); place++) {
        placed_as[order[place]] = place;
    }

    for (std::size_t chapter : order) {
        const Part& first = trace.parts[parts_of_chapter[chapter].front()];
        Chapter laid;
        laid.giver = first.giver;
        laid.first_quest = line.quests.size();
        laid.automatic = chapter != 0 && (first.by_branch || first.leading_in > 1);
        laid.branch_of = first.branch_of;

        for (std::size_t before : from[chapter]) {
            laid.from.push_back(placed_as[before]);
        }

        if (laid.automatic && first.first_hand_in) {
            tables.automatic_chapter_givers.insert(first.giver);
        }

        for (std::size_t part : parts_of_chapter[chapter]) {
            for (void* quest : trace.parts[part].quests) {
                tables.chapter_of_quest[quest] = line.chapters.size();
                line.quests.push_back(quest);
            }

            if (trace.parts[part].ends_line && !trace.parts[part].quests.empty()) {
                laid.ending = true;
                line.ends.push_back(trace.parts[part].quests.back());
            }
        }

        line.chapters.push_back(laid);
    }

    for (std::size_t part = 0; part < trace.parts.size(); part++) {
        const Part& current = trace.parts[part];

        for (const Edge& edge : current.next) {
            if (chapter_of_part[edge.to] != chapter_of_part[part] && !current.quests.empty()) {
                tables.chapters_led_into[current.quests.back()].push_back(placed_as[chapter_of_part[edge.to]]);
            }
        }
    }

    number_chapters(line);
}

static void trace_line(void* start, const HandOffTargets& targets) {
    LineTrace trace;
    trace.line = tables.lines.size();

    Questline new_line;
    new_line.start = start;
    new_line.settings = line_settings::of(start);
    tables.lines.push_back(new_line);

    UsedParts used{ { start, 1 } };
    visit(trace, start, 0, used, targets);
    lay_out(trace);
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
            std::vector<void*> to = targets_of(quest, giver);

            if (!to.empty()) {
                tables.hand_off_targets.insert(to.begin(), to.end());
                targets[quest] = to;
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

const std::vector<HandOff>* branches(const void* quest) {
    auto found = tables.branches.find(quest);

    return found != tables.branches.end() ? &found->second : nullptr;
}

const std::vector<void*>* hand_in_senders(const void* quest) {
    auto found = tables.hand_in_senders.find(quest);

    return found != tables.hand_in_senders.end() ? &found->second : nullptr;
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

bool starts_chapter_anyway(const void* npc) {
    return tables.automatic_chapter_givers.contains(npc);
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

std::vector<std::size_t> chapters_led_into(const void* quest) {
    auto found = tables.chapters_led_into.find(quest);

    return found != tables.chapters_led_into.end() ? found->second : std::vector<std::size_t>{};
}

bool is_end(const Questline& line, const void* quest) {
    return std::ranges::find(line.ends, quest) != line.ends.end();
}

const std::vector<Questline>& all() {
    return tables.lines;
}

}
