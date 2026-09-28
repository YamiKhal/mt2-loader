#include "main_preference.h"

#include "layout.h"
#include "questlines.h"
#include "settings.h"

#include <mt2loader.hpp>

// mmoAction::Type
constexpr int get_quest_action = 7;


static void add_bonus(void* advertisement) {
    float factor = 1.0f + static_cast<float>(settings.main_preference) / 100.0f;

    game::field<float>(advertisement, static_cast<std::size_t>(layout.advance_need)) *= factor;
    game::field<float>(advertisement, static_cast<std::size_t>(layout.loot_need)) *= factor;
}

// The line's first quest giver, or one where the line grew past a player who finished it (line_extensions).
static bool offers_main_line_quest(void* advertisement) {
    if (game::field<int>(advertisement, static_cast<std::size_t>(layout.advertisement_action)) != get_quest_action) {
        return false;
    }

    void* npc = weak_object(advertisement, layout.advertised_npc);
    const Questline* line = npc != nullptr ? questlines::line_of_giver(npc) : nullptr;

    return line != nullptr && line->settings.main;
}


namespace main_preference {

void install() {
    // Also made again for each main quest after the lines are traced, so the bonus follows a line that became main.
    game::in("mmoQuest::SetAdvertisements() [clone .part.0]").after([](void* quest) {
        if (questlines::up_to_date() && questlines::main_line_of_quest(quest) != nullptr) {
            add_bonus(game::field<void*>(quest, static_cast<std::size_t>(layout.quest_advertisement)));
        }
    });

    game::in("mmoToon::GetSelfAdvertisements").after([](void* advertisements, void*) {
        questlines::update();

        for (void* advertisement : game::field<game::Objects>(advertisements, std::size_t{ 0 })) {
            if (advertisement != nullptr && offers_main_line_quest(advertisement)) {
                add_bonus(advertisement);
            }
        }

        return advertisements;
    });
}

}
