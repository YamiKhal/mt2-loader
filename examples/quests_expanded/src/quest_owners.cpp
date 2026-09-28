#include "quest_owners.h"

#include "player.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <unordered_map>

// How long an instance no player holds is taken as that before it's searched for again.
constexpr std::chrono::seconds unowned_for{ 10 };

struct Owner {
    void* toon = nullptr;
    std::chrono::steady_clock::time_point found_at;
};

// Found before, and checked again on each use: records come and go, so an instance's place can be reused.
static std::unordered_map<const void*, Owner> owners;
// The game may ask from its worker threads too.
static std::mutex owners_lock;


static bool holds(void* toon, const void* instance) {
    std::vector<void*> records = player::records(toon);

    return std::ranges::any_of(records, [&](void* record) { return player::instance_of(record) == instance; });
}

static void find_owners() {
    owners.clear();
    auto now = std::chrono::steady_clock::now();
    void* subscribers = game::singleton("mmoSubscriberManager");

    if (subscribers == nullptr) {
        return;
    }

    for (void* subscriber : game::field<game::Objects>(subscribers, "mmoSubscriberManager::subscriber")) {
        void* toon = game::field<void*>(subscriber, "mmoSubscriber::main");

        if (toon == nullptr) {
            continue;
        }

        for (void* record : player::records(toon)) {
            owners[player::instance_of(record)] = Owner{ toon, now };
        }
    }
}

static void remember(void* toon, const void* instance) {
    std::lock_guard remembering(owners_lock);
    owners[instance] = Owner{ toon, std::chrono::steady_clock::now() };
}


namespace quest_owners {

// A player who leaves takes the records with them: what was found goes, before its player is read again.
void install() {
    game::in("_ZN7mmoToonD1Ev").before([](void*) {
        std::lock_guard forgetting(owners_lock);
        owners.clear();
    });

    game::in("mmoModeInGame::DoInit").before([](void*) {
        std::lock_guard forgetting(owners_lock);
        owners.clear();
    });

    // A quest taken from a quest giver is the taker's, found without searching every player (a hand-in too).
    game::in("mmoToon::CollectQuestFromQuestGiver").after([](bool collected, void* toon, void* npc) {
        void* record = toon != nullptr && npc != nullptr ? player::record_for(toon, npc) : nullptr;

        if (record != nullptr) {
            remember(toon, player::instance_of(record));
        }

        return collected;
    });
}

// One no player holds is remembered as that for a while, so it isn't searched for on every call.
void* player_of(const void* instance) {
    std::lock_guard using_owners(owners_lock);
    auto now = std::chrono::steady_clock::now();
    auto found = owners.find(instance);

    if (found != owners.end()) {
        const Owner& owner = found->second;

        if (owner.toon != nullptr ? holds(owner.toon, instance) : now - owner.found_at < unowned_for) {
            return owner.toon;
        }
    }

    find_owners();
    auto [owner, added] = owners.try_emplace(instance, Owner{ nullptr, now });

    return owner->second.toon;
}

}
