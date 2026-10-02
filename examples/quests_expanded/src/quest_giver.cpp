#include "quest_giver.h"

#include <mt2loader.hpp>

#include <cstddef>

// mmoNPC::Type
constexpr int quest_giver_type = 0;


static bool is_quest_giver(const void* npc) {
    return game::field<int>(npc, "mmoNPC::type") == quest_giver_type && quest_giver::quest_count(npc) > 0;
}


namespace quest_giver {

std::vector<void*> all() {
    std::vector<void*> givers;
    void* map = game::singleton("mmoMap");

    if (map == nullptr) {
        return givers;
    }

    for (void* region : game::field<game::Objects>(map, "mmoMap::region")) {
        void* npcs = game::field<void*>(region, "mmoRegion::npcs");

        if (npcs == nullptr) {
            continue;
        }

        for (void* npc : game::field<game::Objects>(npcs, "mmoNPCManager::type")) {
            if (npc != nullptr && is_quest_giver(npc)) {
                givers.push_back(npc);
            }
        }
    }

    return givers;
}

void* at_destination(void* destination) {
    void* npc = game::as(destination, "mmoNPC");

    return npc != nullptr && is_quest_giver(npc) ? npc : nullptr;
}

int quest_count(const void* npc) {
    return static_cast<int>(game::field<game::Objects>(npc, "mmoNPC::quest").size());
}

void* quest(const void* npc, int index) {
    const game::Objects& quests = game::field<game::Objects>(npc, "mmoNPC::quest");

    if (index < 0 || static_cast<std::size_t>(index) >= quests.size()) {
        return nullptr;
    }

    return quests[static_cast<std::size_t>(index)];
}

int index_of(const void* npc, const void* quest) {
    int count = quest_count(npc);

    for (int index = 0; index < count; index++) {
        if (quest_giver::quest(npc, index) == quest) {
            return index;
        }
    }

    return -1;
}

std::string name(const void* npc) {
    return game::field<game::String>(npc, "mmoNPC::name").str();
}

}
