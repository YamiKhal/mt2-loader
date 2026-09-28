#include "branch_choices.h"

#include "chapter_appeal.h"
#include "chapter_gates.h"
#include "chapter_quality.h"
#include "main_thread.h"
#include "parties.h"
#include "places.h"
#include "player_types.h"
#include "quest_giver.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <random>
#include <string>
#include <vector>

// A player holds one quest per quest giver, so a few branch quests at once at most.
constexpr int choice_slots = 8;
// Every branch is taken now and then, however little it draws a player.
constexpr float least_weight = 0.25f;
// Standing this close to a branch's quest giver (in the map's units) is having arrived there: players turn a talk in
// the moment they arrive, and a player getting a quest walks to within 3.
constexpr float arrived_within = 10.0f;


static std::string slot_key(int slot) {
    return std::format("branch.{}", slot);
}

static std::string target_key(int slot) {
    return std::format("branch.{}.to", slot);
}

static std::optional<int> slot_of(void* toon, const void* quest) {
    game::Saved saved = game::saved(toon);

    for (int slot = 0; slot < choice_slots; slot++) {
        if (saved.link(slot_key(slot)) == quest) {
            return slot;
        }
    }

    return std::nullopt;
}

// A slot whose quest is gone is free again.
static std::optional<int> free_slot(void* toon) {
    game::Saved saved = game::saved(toon);

    for (int slot = 0; slot < choice_slots; slot++) {
        if (saved.link(slot_key(slot)) == nullptr) {
            return slot;
        }
    }

    return std::nullopt;
}

// The branches that lead somewhere.
static std::vector<const HandOff*> working_branches(const void* quest) {
    std::vector<const HandOff*> working;
    const std::vector<HandOff>* all = questlines::branches(quest);

    if (all == nullptr) {
        return working;
    }

    for (const HandOff& branch : *all) {
        if (branch.broken == Broken::no) {
            working.push_back(&branch);
        }
    }

    return working;
}

static const HandOff* branch_to(const void* quest, const void* npc) {
    for (const HandOff* branch : working_branches(quest)) {
        if (branch->target == npc) {
            return branch;
        }
    }

    return nullptr;
}

// The chapter a branch starts: the one its first quest (the hand-in) is in.
static std::size_t chapter_of(const HandOff& branch) {
    return questlines::chapter_of_quest(quest_giver::quest(branch.target, branch.hand_in));
}

// Read from the saved game alone, so any thread may ask.
static void* kept_choice(void* toon, const void* quest) {
    std::optional<int> slot = slot_of(toon, quest);

    return slot ? game::saved(toon).link(target_key(*slot)) : nullptr;
}

// A choice still among the branches (the designer may have changed them since).
static void* stored_choice(void* toon, const void* quest) {
    void* npc = kept_choice(toon, quest);

    return npc != nullptr && branch_to(quest, npc) != nullptr ? npc : nullptr;
}

static void keep_choice(void* toon, const void* quest, void* npc) {
    std::optional<int> slot = slot_of(toon, quest);

    if (!slot) {
        slot = free_slot(toon);
    }

    if (!slot) {
        return;
    }

    game::Saved saved = game::saved(toon);
    saved.set_link(slot_key(*slot), quest);
    saved.set_link(target_key(*slot), npc);
}

// Another member of the player's party on the same quest, and where they're going.
static void* party_choice(void* toon, const void* quest) {
    for (void* member : parties::others_with(toon)) {
        if (void* npc = stored_choice(member, quest)) {
            return npc;
        }
    }

    return nullptr;
}

static float weight_of(void* toon, const Questline& line, const HandOff& branch) {
    std::optional<PlayerType> type = player_types::of(toon);

    if (!type) {
        return 1.0f;
    }

    return std::max(chapter_appeal::of(*type, chapter_quality::of(line, chapter_of(branch))), least_weight);
}

static void* choose(void* toon, const void* quest) {
    const Questline* line = questlines::line_of_quest(quest);
    std::vector<const HandOff*> options = working_branches(quest);

    if (line == nullptr || options.empty()) {
        return nullptr;
    }

    std::vector<const HandOff*> allowed = options;
    std::erase_if(allowed, [&](const HandOff* branch) { return !chapter_gates::lets_in(toon, *line, chapter_of(*branch)); });

    if (allowed.empty()) {
        allowed = options;
    }

    void* with_party = party_choice(toon, quest);

    if (with_party != nullptr && std::ranges::any_of(allowed, [&](const HandOff* branch) { return branch->target == with_party; })) {
        return with_party;
    }

    std::vector<float> weights;

    for (const HandOff* branch : allowed) {
        weights.push_back(weight_of(toon, *line, *branch));
    }

    static thread_local std::mt19937 random{ std::random_device{}() };
    std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());

    return allowed[pick(random)]->target;
}


namespace branch_choices {

void* target(void* toon, const void* quest) {
    // The traced lines change on the main thread only.
    if (!main_thread::is_current()) {
        return kept_choice(toon, quest);
    }

    questlines::update();

    if (void* npc = stored_choice(toon, quest)) {
        return npc;
    }

    void* npc = choose(toon, quest);

    if (npc != nullptr) {
        keep_choice(toon, quest, npc);
    }

    return npc;
}

void* chosen(void* toon, const void* quest) {
    return kept_choice(toon, quest);
}

// Where the player arrived: a party walks to one member's choice, and a player the mod didn't steer to their own
// target; then their choice; then the nearest.
const HandOff* hand_off_for(void* toon, const void* quest) {
    questlines::update();

    std::vector<const HandOff*> options = working_branches(quest);

    if (options.empty()) {
        return nullptr;
    }

    Place here = places::of(toon);
    auto distance_to = [&](const HandOff* branch) { return places::distance(here, places::of(branch->target)); };
    const HandOff* nearest = *std::ranges::min_element(options, {}, distance_to);

    if (distance_to(nearest) <= arrived_within) {
        return nearest;
    }

    void* npc = stored_choice(toon, quest);

    return npc != nullptr ? branch_to(quest, npc) : nearest;
}

void forget(void* toon, const void* quest) {
    if (std::optional<int> slot = slot_of(toon, quest)) {
        game::Saved saved = game::saved(toon);
        saved.erase(slot_key(*slot));
        saved.erase(target_key(*slot));
    }
}

}
