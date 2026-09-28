#include "hand_offs.h"

#include "branch_choices.h"
#include "hand_ins.h"
#include "player.h"
#include "questlines.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <optional>

static const HandOff* working_hand_off(const void* quest) {
    questlines::update();

    const HandOff* hand_off = quest != nullptr ? questlines::hand_off(quest) : nullptr;

    return hand_off != nullptr && hand_off->broken == Broken::no ? hand_off : nullptr;
}

static bool has_working_branch(const void* quest) {
    questlines::update();

    const std::vector<HandOff>* branches = quest != nullptr ? questlines::branches(quest) : nullptr;

    return branches != nullptr && std::ranges::any_of(*branches, [](const HandOff& branch) { return branch.broken == Broken::no; });
}

// Copied: handing the quest in traces the lines again if they changed, which would leave a pointer behind.
static std::optional<HandOff> hand_off_on_turning_in(void* toon, const void* quest) {
    if (const HandOff* hand_off = working_hand_off(quest)) {
        return *hand_off;
    }

    if (has_working_branch(quest)) {
        const HandOff* branch = branch_choices::hand_off_for(toon, quest);

        return branch != nullptr ? std::optional<HandOff>(*branch) : std::nullopt;
    }

    return std::nullopt;
}


namespace hand_offs {

void install() {
    // The game turns in a ready quest wherever the player is when this says so (mmoToon::CheckForQuestCompletion). A
    // hand-off or a branch is ready once the player has arrived.
    game::in("mmoQuestInstance::CanTurnInAnywhere").after([](bool anywhere, const void* instance) {
        const void* quest = player::quest_of(instance);

        return anywhere || working_hand_off(quest) != nullptr || has_working_branch(quest);
    });

    game::in("mmoToon::TurnInQuest").after([](void* toon, void* instance) {
        void* quest = player::quest_of(instance);
        std::optional<HandOff> hand_off = hand_off_on_turning_in(toon, quest);

        if (quest != nullptr) {
            branch_choices::forget(toon, quest);
        }

        if (hand_off) {
            hand_ins::give(toon, hand_off->target, hand_off->hand_in);
        }
    });
}

}
