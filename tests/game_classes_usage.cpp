#include "mt2game.hpp"

// Each kind of function mt2game.hpp makes, used the way a plugin would: compiled by every compiler the tests have.
int use_game_classes(void* npc_pointer, void* quest_pointer) {
    mt2::mmoNPC npc(npc_pointer);
    mt2::mmoQuest quest(quest_pointer);
    int changes = 0;

    if (npc.state() == mt2::mmoNPC::State::Combat) {
        changes++;
    }

    npc.level() += 1;
    npc.spawnPoint().y = 0.0f;

    game::String& name = quest.name();
    mt2::mmoNPC giver = quest.questGiver();

    for (void* item : npc.quest()) {
        changes += item != nullptr ? 1 : 0;
    }

    npc.set_marker(mt2::Object(nullptr));

    return changes + (giver ? 1 : 0) + static_cast<int>(name.size());
}
