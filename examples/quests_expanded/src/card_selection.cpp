#include "card_selection.h"

#include "layout.h"

#include <mt2loader.hpp>

static game::Function<void(void* list, void* card)> select_card{ "mmoQuestList::SetSelected" };


namespace card_selection {

void install() {
    game::in("mmoQuestList::InitForNPC").before([](void* list, void* npc) {
        if (weak_object(list, layout.list_npc) != npc) {
            select_card(list, nullptr);
        }
    });
}

}
