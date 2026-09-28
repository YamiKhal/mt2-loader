#include "line_finish.h"

#include "line_progress.h"
#include "player.h"
#include "questlines.h"

#include <mt2loader.hpp>

namespace line_finish {

void install() {
    game::in("mmoToon::TurnInQuest").after([](void* toon, void* instance) {
        questlines::update();

        void* quest = player::quest_of(instance);
        const Questline* line = quest != nullptr ? questlines::line_of_quest(quest) : nullptr;

        if (line != nullptr && line->last_quest == quest) {
            line_progress::finish(toon, *line);
        }
    });
}

}
