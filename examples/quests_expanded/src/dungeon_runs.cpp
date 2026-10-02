#include "dungeon_runs.h"

#include "dungeon_quests.h"
#include "parties.h"
#include "player.h"

#include <mt2loader.hpp>

// mmoParty::Mode a grinding party the game sends into a dungeon is put in.
constexpr int heading_for_dungeon = 2;
// How far and how many players a player forming a party at a dungeon's entrance gathers
// (mmoToonFormDungeonPartyAction::DoStart).
constexpr float gathering_distance = 100.0f;
constexpr int party_size = 4;

static game::Function<bool(const void* dungeon)> is_online{ "mmoDungeon::IsOnline" };
static game::Function<bool(void* dungeon, void* party)> queue_party{ "mmoDungeon::AddPartyToPartyQueue" };
static game::Function<void(void* party, void* dungeon)> set_desired_dungeon{ "mmoParty::SetDesiredDungeon" };
static game::Function<void(void* party, int mode)> set_mode{ "mmoParty::SetMode" };
static game::Function<void*(void* manager, void* toon, void* dungeon)> party_for_dungeon{ "mmoPartyManager::FindPartyForDungeon" };
static game::Function<void*(void* manager, void* toon, float distance, int size)> gather_party{ "mmoPartyManager::GeneratePartyNear" };
static game::Function<void(void* toon, void* party)> join_party{ "mmoToon::JoinParty" };
static game::Function<void*(void* shard)> run_in{ "mmoShard::DungeonInstance()" };


static void clear_for_members(void* party, const void* dungeon) {
    for (void* toon : parties::members(party)) {
        for (void* record : player::records_in_place(toon)) {
            void* instance = player::instance_of(record);

            if (dungeon_quests::dungeon_of(player::quest_of(instance)) == dungeon) {
                dungeon_quests::mark_cleared(instance);
            }
        }
    }
}

// The quest the party came for is the one the game just marked as reached: a dungeon to clear isn't done by that.
static void* dungeon_reached(void* party) {
    void* reached = nullptr;

    for (void* toon : parties::members(party)) {
        for (void* record : player::records_in_place(toon)) {
            void* instance = player::instance_of(record);
            void* dungeon = dungeon_quests::dungeon_of(player::quest_of(instance));
            bool& visited = game::field<bool>(instance, "mmoQuestInstance::visited");

            if (dungeon != nullptr && visited && !dungeon_quests::is_cleared(instance)) {
                visited = false;
                reached = dungeon;
            }
        }
    }

    return reached;
}

// As the game sends a grinding party in (mmoParty::SetUpMoveToGrind).
static void send_in(void* party, void* dungeon) {
    if (is_online(dungeon) && queue_party(dungeon, party)) {
        set_desired_dungeon(party, dungeon);
        set_mode(party, heading_for_dungeon);
    }
}

// As a player at a dungeon's entrance forms a party for it (mmoToonFormDungeonPartyAction::DoStart).
static void form_party(void* toon, void* dungeon) {
    void* region = game::field<void*>(toon, "mmoProp::region");
    void* manager = region != nullptr ? game::field<void*>(region, "mmoRegion::partyManager") : nullptr;

    if (manager == nullptr || !is_online(dungeon)) {
        return;
    }

    void* party = party_for_dungeon(manager, toon, dungeon);

    if (party == nullptr) {
        party = gather_party(manager, toon, gathering_distance, party_size);

        if (party == nullptr) {
            return;
        }

        set_desired_dungeon(party, dungeon);
    }

    join_party(toon, party);
}


namespace dungeon_runs {

void install() {
    // A party inside finds nothing left to do (no containers, collectables or dungeon bosses): its players have
    // cleared it. The run's own party is only the one it was held for, and is let go once the party is in.
    game::in("mmoParty::SetUpMoveToDungeonGoal").call("mmoParty::_CheckForDungeonCompletion")
        .hook<bool(void* party)>([](auto check, void* party) {
            bool done = check(party);
            void* shard = done ? game::field<void*>(party, "mmoProp::shard") : nullptr;
            void* run = shard != nullptr ? run_in(shard) : nullptr;

            if (run != nullptr) {
                clear_for_members(party, game::field<void*>(run, "mmoDungeonInstance::dungeon"));
            }

            return done;
        });

    // A player on their own reaching the entrance of a dungeon to clear goes in with a party, as one forming a party
    // there would.
    game::in("mmoToonDoQuestAction::HandleEvent").call("mmoToon::GetQuestInstanceForQuest")
        .hook<void*(void* toon, const void* quest)>([](auto instance_for, void* toon, const void* quest) {
            void* instance = instance_for(toon, quest);
            void* dungeon = instance != nullptr ? dungeon_quests::dungeon_of(quest) : nullptr;

            if (dungeon != nullptr && !dungeon_quests::is_cleared(instance) && game::field<void*>(toon, "mmoToon::party") == nullptr) {
                form_party(toon, dungeon);
            }

            return instance;
        });

    game::in("mmoParty::_ReachedQuestDestination() [clone .part.0]").after([](void* party) {
        if (void* dungeon = dungeon_reached(party)) {
            send_in(party, dungeon);
        }
    });
}

}
