#include "objective_undo.h"

#include "layout.h"
#include "objectives.h"
#include "quest_giver.h"
#include "steering.h"

#include <mt2loader.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// A target kept as the game keeps them in its copy: by its object's id, which a destroyed object gives up, and how far
// inside that object its quest destination is.
struct KeptTarget {
    std::uint64_t uid = 0;
    std::ptrdiff_t offset = 0;
};

struct KeptQuest {
    Objective objective = Objective::standard;
    KeptTarget own;
    std::vector<KeptTarget> extra;
};

struct KeptGiver {
    std::uint64_t npc = 0;
    std::vector<KeptQuest> quests;
};

static game::Function<void*(std::uint64_t uid)> find_uid{ "mmoObject::FindUID" };

// The mod's copy of each of the game's (an mmoDoSetNPCQuests), until the undo history is cleared.
static std::unordered_map<const void*, KeptGiver> kept;


static std::uint64_t uid_of(const void* object) {
    return game::field<std::uint64_t>(object, static_cast<std::size_t>(layout.object_uid));
}

static KeptTarget keep(void* target) {
    const auto* object = static_cast<const std::uint8_t*>(objectives::object_of(target));

    return KeptTarget{ uid_of(object), static_cast<std::uint8_t*>(target) - object };
}

static void* find(const KeptTarget& target) {
    auto* object = static_cast<std::uint8_t*>(find_uid(target.uid));

    return object != nullptr ? object + target.offset : nullptr;
}

// Every quest, of any objective, so the rebuilt ones can be matched up by their own target: the game leaves out a quest
// whose target is gone.
static KeptGiver keep_giver(void* npc) {
    KeptGiver giver{ uid_of(npc), {} };

    for (int index = 0; index < quest_giver::quest_count(npc); index++) {
        void* quest = quest_giver::quest(npc, index);
        std::vector<void*> targets = objectives::targets(quest);

        if (targets.empty()) {
            continue;
        }

        KeptQuest copy{ objectives::of(quest), keep(targets.front()), {} };

        for (std::size_t target = 1; target < targets.size(); target++) {
            copy.extra.push_back(keep(targets[target]));
        }

        giver.quests.push_back(copy);
    }

    return giver;
}

static void put_back(const KeptGiver& giver) {
    void* npc = find_uid(giver.npc);
    std::size_t next = 0;

    for (int index = 0; npc != nullptr && index < quest_giver::quest_count(npc); index++) {
        void* quest = quest_giver::quest(npc, index);
        std::vector<void*> own = objectives::targets(quest);

        while (next < giver.quests.size() && (own.empty() || find(giver.quests[next].own) != own.front())) {
            next++;
        }

        if (next == giver.quests.size()) {
            return;
        }

        const KeptQuest& copy = giver.quests[next++];

        if (copy.objective == Objective::standard) {
            continue;
        }

        objectives::set(quest, copy.objective);

        for (const KeptTarget& target : copy.extra) {
            if (void* destination = find(target)) {
                objectives::add_target(quest, destination);
            }
        }
    }
}


namespace objective_undo {

void install() {
    game::in("mmoDoSetNPCQuests::mmoDoSetNPCQuests").after([](void* copy, void* npc, bool) {
        kept[copy] = keep_giver(npc);
    });

    game::in("mmoDoSetNPCQuests::Do").after([](void* copy, void*) {
        if (auto found = kept.find(copy); found != kept.end()) {
            put_back(found->second);
            steering::changed();
        }
    });

    // Its deleting destructor has the complete one's work inline, so both are watched.
    for (const char* destructor : { "_ZN17mmoDoSetNPCQuestsD1Ev", "_ZN17mmoDoSetNPCQuestsD0Ev" }) {
        game::in(destructor).before([](void* copy) {
            kept.erase(copy);
        });
    }

    game::in("mmoUndoManager::Clear").after([](void*) {
        kept.clear();
    });
}

}
