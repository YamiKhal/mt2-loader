#include "retracing.h"

#include "questlines.h"

#include <mt2loader.hpp>

namespace retracing {

void install() {
    // Before the game's cleanup, which runs inside it and asks about main questline quests.
    game::in("mmoMap::PostLoad").before([](void*) {
        questlines::changed();
    });

    game::in("_ZN6mmoMapD1Ev").before([](void*) {
        questlines::forget_world();
    });

    for (const char* gone : { "_ZN6mmoNPCD1Ev", "_ZN8mmoQuestD1Ev" }) {
        game::in(gone).before([](void*) {
            questlines::changed();
        });
    }

    // Chains changed from the quest card, the quest list and undo (mmoDoSetNPCQuests::Do sets and adds quests).
    game::in("mmoQuest::Set").before([](void*, int, void*, int, int, void*) {
        questlines::changed();
    });

    game::in("mmoQuest::SetDestination").before([](void*, void*, int, int) {
        questlines::changed();
    });

    game::in("mmoNPC::AddQuest(mmoQuest*)").before([](void*, void*) {
        questlines::changed();
    });

    game::in("mmoNPC::RemoveQuest(int)").before([](void*, int) {
        questlines::changed();
    });

    game::in("mmoNPC::ReorderQuest").before([](void*, void*, int) {
        questlines::changed();
    });

    game::in("mmoNPC::GenerateQuest()").before([](void*) {
        questlines::changed();
    });
}

}
