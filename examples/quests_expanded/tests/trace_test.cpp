// Offline check of questlines' tracing: the real questlines.cpp, with the game replaced by quest givers and quests
// made here. From this folder, with MSYS2's g++:
//     g++ -std=c++20 -Wall -Wextra -I../src -I../../../sdk/include trace_test.cpp ../src/questlines.cpp -o trace_test
//     ./trace_test
#include "line_names.h"
#include "line_settings.h"
#include "objectives.h"
#include "quest.h"
#include "quest_giver.h"
#include "questlines.h"

#include <mt2loader.hpp>

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct FakeQuest;

struct FakeGiver {
    std::string name;
    std::vector<FakeQuest*> quests;
    bool chapter_mark = false;
};

struct FakeQuest {
    FakeGiver* giver = nullptr;
    FakeGiver* talk_to = nullptr;
    std::vector<FakeGiver*> branch_to;
    std::string name;
};

static std::vector<std::unique_ptr<FakeGiver>> givers;
static std::vector<std::unique_ptr<FakeQuest>> quests;

static FakeGiver* giver(const std::string& name, int count) {
    givers.push_back(std::make_unique<FakeGiver>());
    FakeGiver* made = givers.back().get();
    made->name = name;

    for (int index = 0; index < count; index++) {
        quests.push_back(std::make_unique<FakeQuest>());
        quests.back()->giver = made;
        quests.back()->name = name + std::to_string(index);
        made->quests.push_back(quests.back().get());
    }

    return made;
}

static void talk(FakeGiver* from, int index, FakeGiver* to) {
    from->quests[index]->talk_to = to;
}

static void branch(FakeGiver* from, int index, std::vector<FakeGiver*> to) {
    from->quests[index]->talk_to = to.front();
    from->quests[index]->branch_to = to;
}

static void reset() {
    givers.clear();
    quests.clear();
    questlines::forget_world();
}

void plugin::init() {}

namespace quest_giver {
std::vector<void*> all() {
    std::vector<void*> all;
    for (auto& each : givers) {
        if (!each->quests.empty()) {
            all.push_back(each.get());
        }
    }
    return all;
}
void* at_destination(void* destination) {
    auto* npc = static_cast<FakeGiver*>(destination);
    return npc != nullptr && !npc->quests.empty() ? npc : nullptr;
}
int quest_count(const void* npc) {
    return static_cast<int>(static_cast<const FakeGiver*>(npc)->quests.size());
}
void* quest(const void* npc, int index) {
    const auto* giver = static_cast<const FakeGiver*>(npc);
    return index >= 0 && index < static_cast<int>(giver->quests.size()) ? giver->quests[static_cast<std::size_t>(index)] : nullptr;
}
int index_of(const void*, const void*) { return -1; }
std::string name(const void* npc) { return static_cast<const FakeGiver*>(npc)->name; }
}

namespace quest {
void* giver(const void* quest) { return static_cast<const FakeQuest*>(quest)->giver; }
void* destination(const void* quest) { return static_cast<const FakeQuest*>(quest)->talk_to; }
bool is_talk(const void* quest) { return static_cast<const FakeQuest*>(quest)->talk_to != nullptr; }
QuestKind kind(const void*) { return QuestKind::errand; }
int min_level(const void*) { return 1; }
void refresh_advertisements(void*) {}
std::string name(const void* quest) { return static_cast<const FakeQuest*>(quest)->name; }
int times_accepted(const void*) { return 0; }
int times_completed(const void*) { return 0; }
}

namespace objectives {
Objective of(const void* quest) {
    return static_cast<const FakeQuest*>(quest)->branch_to.empty() ? Objective::standard : Objective::branch;
}
std::vector<void*> targets(const void* quest) {
    const auto* fake = static_cast<const FakeQuest*>(quest);
    std::vector<void*> all;
    if (fake->branch_to.empty()) {
        if (fake->talk_to != nullptr) {
            all.push_back(fake->talk_to);
        }
        return all;
    }
    for (FakeGiver* each : fake->branch_to) {
        all.push_back(each);
    }
    return all;
}
}

namespace line_names {
bool starts_chapter(const void* npc) { return static_cast<const FakeGiver*>(npc)->chapter_mark; }
}

namespace line_settings {
LineSettings of(const void*) { return LineSettings{}; }
}

static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    failures += ok ? 0 : 1;
}

static std::string names(const std::vector<void*>& list) {
    std::string text;
    for (void* each : list) {
        text += (text.empty() ? "" : " ") + static_cast<FakeQuest*>(each)->name;
    }
    return text;
}

// "1:a0 a1 | 2a:b0 b1 | 2b:c0 | 3<1,2:d0 d1" (from: the chapters leading in).
static std::string layout(const Questline& line) {
    std::string text;
    for (std::size_t chapter = 0; chapter < line.chapters.size(); chapter++) {
        const Chapter& each = line.chapters[chapter];
        std::size_t end = chapter + 1 < line.chapters.size() ? line.chapters[chapter + 1].first_quest : line.quests.size();
        text += (text.empty() ? "" : " | ") + std::to_string(each.number) + (each.letter ? std::string(1, each.letter) : "");
        if (each.from.size() > 1) {
            text += "<";
            for (std::size_t from : each.from) {
                text += std::to_string(from) + (from == each.from.back() ? "" : ",");
            }
        }
        text += ":" + names(std::vector<void*>(line.quests.begin() + static_cast<std::ptrdiff_t>(each.first_quest), line.quests.begin() + static_cast<std::ptrdiff_t>(end)));
    }
    return text;
}

static const Questline& only_line() {
    questlines::update();
    return questlines::all().front();
}

static void expect_layout(const std::string& want, const std::string& what) {
    std::string got = layout(only_line());
    check(got == want, what + " -> " + got + (got == want ? "" : "   (wanted " + want + ")"));
}

int main() {
    {
        reset();
        FakeGiver* a = giver("a", 2);
        FakeGiver* b = giver("b", 2);
        talk(a, 1, b);
        expect_layout("1:a0 a1 b0 b1", "plain chain");
        check(names(only_line().ends) == "b1", "plain chain ends at b1");
        check(questlines::chapters_led_into(a->quests[1]).empty(), "plain hand-off leads into no chapter");
    }
    {
        reset();
        FakeGiver* a = giver("a", 2);
        FakeGiver* b = giver("b", 2);
        FakeGiver* c = giver("c", 1);
        FakeGiver* d = giver("d", 2);
        branch(a, 1, { b, c });
        talk(b, 1, d);
        talk(c, 0, d);
        expect_layout("1:a0 a1 | 2a:b0 b1 | 2b:c0 | 3<1,2:d0 d1", "fork that merges");
        check(names(only_line().ends) == "d1", "merged line ends once");
        check(questlines::branches(a->quests[1]) != nullptr && questlines::branches(a->quests[1])->size() == 2, "branch quest has two branches");
        const std::vector<void*>* senders = questlines::hand_in_senders(d->quests[0]);
        check(senders != nullptr && senders->size() == 2, "merge has both senders");
        check(questlines::chapters_led_into(a->quests[1]).size() == 2, "branch quest leads into both branches");
        check(questlines::chapters_led_into(b->quests[1]) == std::vector<std::size_t>{ 3 }, "branch leads into the merge chapter");
        check(only_line().chapters[1].branch_of == a->quests[1] && only_line().chapters[3].branch_of == nullptr, "branch chapters know their branch quest");
        check(questlines::starts_chapter_anyway(b) && questlines::starts_chapter_anyway(d), "branch and merge givers start chapters anyway");
    }
    {
        reset();
        FakeGiver* a = giver("a", 1);
        FakeGiver* b = giver("b", 2);
        FakeGiver* c = giver("c", 1);
        branch(a, 0, { b, c });
        expect_layout("1:a0 | 2a:b0 b1 | 2b:c0", "fork that never merges");
        check(names(only_line().ends) == "b1 c0", "two endings");
        check(only_line().chapters[1].ending && only_line().chapters[2].ending, "both branches end the line");
    }
    {
        reset();
        FakeGiver* a = giver("a", 1);
        FakeGiver* b = giver("b", 1);
        FakeGiver* other = giver("x", 2);
        FakeGiver* y = giver("y", 1);
        talk(y, 0, other);
        branch(a, 0, { b, other });
        questlines::update();
        // Traced first, a's branch takes x into its line; y's hand-off to x is then the one into another line.
        const HandOff* from_y = questlines::hand_off(y->quests[0]);
        check(questlines::line_of_giver(other) == questlines::line_of_giver(a), "the line traced first takes the shared quest giver");
        check(from_y != nullptr && from_y->broken == Broken::other_line, "the other line's hand-off to it is broken");
        check(questlines::line_of_giver(a)->broken, "the line taking it is marked broken too");
    }
    {
        reset();
        FakeGiver* a = giver("a", 1);
        FakeGiver* b = giver("b", 1);
        FakeGiver* c = giver("c", 1);
        FakeGiver* d = giver("d", 1);
        FakeGiver* e = giver("e", 1);
        FakeGiver* f = giver("f", 1);
        branch(a, 0, { b, c });
        branch(b, 0, { d, e });
        talk(d, 0, f);
        talk(e, 0, f);
        talk(c, 0, f);
        expect_layout("1:a0 | 2a:b0 | 3a:d0 | 3b:e0 | 2b:c0 | 4<2,3,4:f0", "nested fork, all meeting at f");
    }
    {
        reset();
        FakeGiver* a = giver("a", 1);
        FakeGiver* b = giver("b", 2);
        FakeGiver* c = giver("c", 1);
        FakeGiver* m = giver("m", 2);
        branch(a, 0, { b, c });
        talk(b, 1, m);
        m->chapter_mark = true;
        expect_layout("1:a0 | 2a:b0 b1 | 3:m0 m1 | 2b:c0", "designer chapter inside a branch");
    }
    {
        reset();
        FakeGiver* a = giver("a", 2);
        FakeGiver* b = giver("b", 1);
        talk(a, 0, b);
        talk(b, 0, a);
        expect_layout("1:a0 b0 a1", "a hand-off back to the start giver's next part");
    }
    {
        reset();
        FakeGiver* a = giver("a", 1);
        FakeGiver* b = giver("b", 1);
        branch(a, 0, { b, a });
        expect_layout("1:a0 b0", "a branch to its own quest giver is a plain hand-off");
        check(questlines::branches(a->quests[0]) == nullptr && questlines::hand_off(a->quests[0]) != nullptr, "one-target branch has no branches");
    }
    {
        reset();
        FakeGiver* a = giver("a", 1);
        FakeGiver* b = giver("b", 2);
        FakeGiver* c = giver("c", 1);
        FakeGiver* d = giver("d", 3);
        branch(a, 0, { b, c });
        talk(b, 0, d);
        talk(d, 0, b);
        talk(b, 1, d);
        talk(c, 0, d);
        expect_layout("1:a0 | 2a:b0 | 2b:c0 | 3<1,2:d0 b1 d1 d2", "a branch meets the other at a quest giver it visits twice");
    }
    {
        reset();
        FakeGiver* a = giver("a", 1);
        FakeGiver* b = giver("b", 1);
        FakeGiver* c = giver("c", 1);
        FakeGiver* d = giver("d", 1);
        branch(a, 0, { b, c, d });
        talk(c, 0, b);
        // b is both a's branch and where c's branch meets it: after c, as the one chapter reached two ways.
        expect_layout("1:a0 | 2a:c0 | 3<0,1:b0 | 2b:d0", "a branch that runs into another's first chapter");
        check(only_line().chapters[2].branch_of == a->quests[0], "it is still one of a's branches");
    }

    std::printf("%d failed\n", failures);

    return failures == 0 ? 0 : 1;
}
