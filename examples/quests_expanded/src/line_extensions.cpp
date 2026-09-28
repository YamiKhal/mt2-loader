#include "line_extensions.h"

#include "player.h"
#include "quest_giver.h"
#include "questlines.h"

#include <mt2loader.hpp>

#include <array>
#include <cstddef>

// Read by the game in place of a record, for its step only.
static thread_local std::array<std::byte, 0x100> stand_in{};
static thread_local void* stand_in_slot = nullptr;


// The player turned in the record's quest, and its quest giver has a next one they haven't taken. A quest the game
// refused for their level waits the same way; the game checks the level again before inviting them.
static bool waits_on_new_quest(void* record) {
    void* npc = player::giver_of(record);
    int step = player::step(record);

    if (npc == nullptr || step < 0 || step >= quest_giver::quest_count(npc) || questlines::line_of_giver(npc) == nullptr) {
        return false;
    }

    void* instance = player::instance_of(record);
    void* quest = player::quest_of(instance);

    return quest != nullptr && player::is_finished(instance) && quest != quest_giver::quest(npc, step);
}

static void* stand_in_for(void* record) {
    auto* step = reinterpret_cast<std::byte*>(&game::field<int>(record, "mmoQuestGiverProgress::questsCompleted"));
    std::size_t offset = static_cast<std::size_t>(step - static_cast<std::byte*>(record));

    if (offset + sizeof(int) > stand_in.size()) {
        return nullptr;
    }

    *reinterpret_cast<int*>(stand_in.data() + offset) = player::met_nothing_taken;

    return stand_in.data();
}


namespace line_extensions {

void install() {
    // The game offers "Get quest" only where a record's step says "met, nothing taken" (its second look at each record).
    game::in("mmoToon::GetSelfAdvertisements").call("vsArrayStore<mmoQuestGiverProgress>::GetItem").nth(2)
        .hook<void**(void* records, int index)>([](auto get_item, void* records, int index) {
            void** slot = get_item(records, index);
            questlines::update();

            if (*slot == nullptr || !waits_on_new_quest(*slot)) {
                return slot;
            }

            stand_in_slot = stand_in_for(*slot);

            return stand_in_slot != nullptr ? &stand_in_slot : slot;
        });
}

}
