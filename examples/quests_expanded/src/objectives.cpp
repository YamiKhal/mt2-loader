#include "objectives.h"

#include "quest.h"
#include "quest_giver.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>

constexpr const char* objective_key = "objective";

// mmoQuest::Action, as DefaultQuestActionForDestination picks it.
constexpr int kill_monsters_action = 1;
constexpr int kill_it_action = 6;
// The game's own kill counts: a hunt picked on the card, and an elite.
constexpr int picked_hunt_kills = 8;

static game::Function<int(void* destination)> default_action{ "mmoQuest::DefaultQuestActionForDestination" };
static game::Function<void(void* quest, void* destination, int action, int count)> set_destination{ "mmoQuest::SetDestination" };


static std::string target_key(std::size_t index) {
    return std::format("target.{}", index);
}

static std::string offset_key(std::size_t index) {
    return std::format("target.{}.offset", index);
}

static void* own_target(const void* quest) {
    return quest::destination(quest);
}

// The quest's own target is 0; the rest are kept from 1.
static void* kept_target(const void* quest, std::size_t index) {
    game::Saved saved = game::saved(quest);
    auto* object = static_cast<std::uint8_t*>(saved.link(target_key(index)));

    return object != nullptr ? object + saved.get(offset_key(index), 0) : nullptr;
}

static void keep_target(void* quest, std::size_t index, void* destination) {
    game::Saved saved = game::saved(quest);

    if (destination == nullptr) {
        saved.erase(target_key(index));
        saved.erase(offset_key(index));

        return;
    }

    const auto* object = static_cast<const std::uint8_t*>(objectives::object_of(destination));
    saved.set_link(target_key(index), object);
    saved.set(offset_key(index), static_cast<int>(static_cast<std::uint8_t*>(destination) - object));
}

static void keep_targets(void* quest, const std::vector<void*>& extra) {
    for (std::size_t index = 1; index < objectives::most_tour_targets; index++) {
        keep_target(quest, index, index - 1 < extra.size() ? extra[index - 1] : nullptr);
    }
}

// As the card does when a target is picked: the kills a hunt asks for (8 unless set), 1 for an elite.
static void make_own_target(void* quest, void* destination) {
    int action = default_action(destination);
    int kills = game::field<int>(quest, "mmoQuest::monstersToKill");
    int count = action == kill_monsters_action ? (kills > 0 ? kills : picked_hunt_kills) : action == kill_it_action ? 1 : 0;

    set_destination(quest, destination, action, count);
}


namespace objectives {

// A building's quest destination sits inside it, after its other bases, as far as its class table says (just before
// its functions). Links keep the object, so each target also keeps how far inside it its destination is.
const void* object_of(const void* destination) {
    const std::intptr_t* table = *static_cast<const std::intptr_t* const*>(destination);

    return static_cast<const std::uint8_t*>(destination) + table[-2];
}

Objective of(const void* quest) {
    if (quest == nullptr) {
        return Objective::standard;
    }

    int kept = game::saved(quest).get(objective_key, 0);

    return kept == static_cast<int>(Objective::tour) || kept == static_cast<int>(Objective::branch) ? static_cast<Objective>(kept)
                                                                                                    : Objective::standard;
}

void set(void* quest, Objective objective) {
    std::vector<void*> all = targets(quest);

    if (objective == Objective::standard) {
        game::saved(quest).erase(objective_key);
        keep_targets(quest, {});

        return;
    }

    game::saved(quest).set(objective_key, static_cast<int>(objective));
    std::vector<void*> extra(all.size() > 1 ? all.begin() + 1 : all.end(), all.end());

    if (objective == Objective::branch) {
        std::erase_if(extra, [&](void* destination) { return !can_branch_to(quest, destination); });
    }

    extra.resize(std::min(extra.size(), most_targets(objective) - 1));
    keep_targets(quest, extra);
}

std::size_t most_targets(Objective objective) {
    switch (objective) {
    case Objective::tour:
        return most_tour_targets;
    case Objective::branch:
        return most_branch_targets;
    default:
        return 1;
    }
}

std::vector<void*> targets(const void* quest) {
    std::vector<void*> found;

    if (void* own = own_target(quest)) {
        found.push_back(own);
    }

    std::size_t most = most_targets(of(quest));

    for (std::size_t index = 1; index < most; index++) {
        if (void* target = kept_target(quest, index)) {
            found.push_back(target);
        }
    }

    return found;
}

bool can_branch_to(const void* quest, void* destination) {
    void* npc = quest_giver::at_destination(destination);

    return npc != nullptr && npc != quest::giver(quest);
}

bool add_target(void* quest, void* destination) {
    Objective objective = of(quest);
    std::vector<void*> all = targets(quest);

    if (all.size() >= most_targets(objective) || std::ranges::find(all, destination) != all.end()) {
        return false;
    }

    if (objective == Objective::branch && !can_branch_to(quest, destination)) {
        return false;
    }

    if (all.empty()) {
        make_own_target(quest, destination);

        return true;
    }

    std::vector<void*> extra(all.begin() + 1, all.end());
    extra.push_back(destination);
    keep_targets(quest, extra);

    return true;
}

void remove_target(void* quest, std::size_t index) {
    std::vector<void*> all = targets(quest);

    if (index >= all.size() || all.size() <= 1) {
        return;
    }

    all.erase(all.begin() + static_cast<std::ptrdiff_t>(index));

    if (index == 0) {
        make_own_target(quest, all.front());
    }

    keep_targets(quest, std::vector<void*>(all.begin() + 1, all.end()));
}

}
