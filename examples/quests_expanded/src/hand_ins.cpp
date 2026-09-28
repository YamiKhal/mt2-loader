#include "hand_ins.h"

#include "player.h"

#include <mt2loader.hpp>

struct Giving {
    const void* npc = nullptr;
    int index = 0;
};

static game::Function<bool(void* toon, void* npc)> collect_quest{ "mmoToon::CollectQuestFromQuestGiver" };

static thread_local Giving giving;


namespace hand_ins {

void give(void* toon, void* npc, int index) {
    // An existing record takes the quest at its own step; a new one starts at 0, which the hook below turns into index.
    if (void* record = player::record_for(toon, npc)) {
        player::set_step(record, index);
    }

    giving = Giving{ npc, index };
    collect_quest(toon, npc);
    giving = Giving{};

    if (void* record = player::record_for(toon, npc)) {
        player::set_step(record, index);
    }
}

bool is_giving(const void* npc) {
    return npc != nullptr && giving.npc == npc;
}

void install() {
    game::in("mmoToon::CollectQuestFromQuestGiver").call("mmoNPC::GetQuest(int)")
        .hook<void*(void* npc, int index)>([](auto get_quest, void* npc, int index) {
            return get_quest(npc, is_giving(npc) ? giving.index : index);
        });

    // The quest giver turns to the player only when online; a hand-in is theirs either way, as the talk happened.
    game::in("mmoToon::CollectQuestFromQuestGiver").call("mmoNPC::Interact")
        .hook<bool(void* npc, void* toon)>([](auto interact, void* npc, void* toon) {
            return interact(npc, toon) || is_giving(npc);
        });
}

}
