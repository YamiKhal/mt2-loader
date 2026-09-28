#include "quest.h"

#include <mt2loader.hpp>

// mmoQuest::Action values.
constexpr int kill_monsters_action = 1;
constexpr int kill_it_action = 6;
constexpr int talk_action = 7;

static game::Function<int(const void* quest)> quest_min_level{ "mmoQuest::GetQuestMinLevel" };
static game::Function<void(void* quest)> set_advertisements{ "mmoQuest::SetAdvertisements()" };

static int total_of(const void* quest, const char* daily_count) {
    void* count = game::object_in(const_cast<void*>(quest), daily_count);

    return game::field<int>(count, "mmoDailyCount::total");
}


namespace quest {

void* giver(const void* quest) {
    return game::field<game::Link>(quest, "mmoQuest::questGiver").get();
}

void* destination(const void* quest) {
    return game::field<game::Link>(quest, "mmoQuest::questDestination").get();
}

bool is_talk(const void* quest) {
    return game::field<int>(quest, "mmoQuest::action") == talk_action;
}

QuestKind kind(const void* quest) {
    switch (game::field<int>(quest, "mmoQuest::action")) {
    case kill_monsters_action:
        return game::field<int>(quest, "mmoQuest::monstersToKill") > 0 ? QuestKind::hunt : QuestKind::fetch;
    case kill_it_action:
        return QuestKind::elite;
    case talk_action:
        return QuestKind::talk;
    default:
        return QuestKind::errand;
    }
}

int min_level(const void* quest) {
    return quest_min_level(quest);
}

void refresh_advertisements(void* quest) {
    set_advertisements(quest);
}

std::string name(const void* quest) {
    return std::string(game::field<game::String>(quest, "mmoQuest::name").view());
}

int times_accepted(const void* quest) {
    return total_of(quest, "mmoQuest::accepted");
}

int times_completed(const void* quest) {
    return total_of(quest, "mmoQuest::completed");
}

}
