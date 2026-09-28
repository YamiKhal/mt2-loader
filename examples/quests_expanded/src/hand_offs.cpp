#include "hand_offs.h"

#include "hand_ins.h"
#include "player.h"
#include "questlines.h"

#include <mt2loader.hpp>

static const HandOff* working_hand_off(const void* quest) {
    questlines::update();

    const HandOff* hand_off = quest != nullptr ? questlines::hand_off(quest) : nullptr;

    return hand_off != nullptr && hand_off->broken == Broken::no ? hand_off : nullptr;
}


namespace hand_offs {

void install() {
    // The game turns in a ready quest wherever the player is when this says so (mmoToon::CheckForQuestCompletion). A
    // hand-off is ready once the player has arrived.
    game::in("mmoQuestInstance::CanTurnInAnywhere").after([](bool anywhere, const void* instance) {
        return anywhere || working_hand_off(player::quest_of(instance)) != nullptr;
    });

    game::in("mmoToon::TurnInQuest").after([](void* toon, void* instance) {
        if (const HandOff* hand_off = working_hand_off(player::quest_of(instance))) {
            hand_ins::give(toon, hand_off->target, hand_off->hand_in);
        }
    });
}

}
