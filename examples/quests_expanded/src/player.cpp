#include "player.h"

#include <mt2loader.hpp>

#include <algorithm>

static game::Function<void*(void* toon, void* npc)> find_record{ "mmoToon::FindQuestGiverProgressFor" };


namespace player {

std::vector<void*> records(void* toon) {
    const game::Objects& all = records_in_place(toon);

    return std::vector<void*>(all.begin(), all.end());
}

const game::Objects& records_in_place(void* toon) {
    return game::field<game::Objects>(toon, "mmoToon::questGiverProgress");
}

void* record_for(void* toon, void* npc) {
    return find_record(toon, npc);
}

void* giver_of(const void* record) {
    return game::field<game::Link>(record, "mmoQuestGiverProgress::questGiver").get();
}

int step(const void* record) {
    return game::field<int>(record, "mmoQuestGiverProgress::questsCompleted");
}

void set_step(void* record, int step) {
    game::field<int>(record, "mmoQuestGiverProgress::questsCompleted") = step;
}

void* instance_of(void* record) {
    return game::object_in(record, "mmoQuestGiverProgress::quest");
}

void* quest_of(const void* instance) {
    return game::field<game::Link>(instance, "mmoQuestInstance::quest").get();
}

bool is_finished(const void* instance) {
    return game::field<bool>(instance, "mmoQuestInstance::complete");
}

int next_quest_index(void* toon, void* npc) {
    void* record = record_for(toon, npc);

    if (record == nullptr) {
        return 0;
    }

    return std::max(step(record), 0);
}

}
