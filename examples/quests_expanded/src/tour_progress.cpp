#include "tour_progress.h"

#include "objectives.h"
#include "places.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>

// A player holds one quest per quest giver, so a few tours at once at most.
constexpr int tour_slots = 8;


static std::string slot_key(int slot) {
    return std::format("tour.{}", slot);
}

static std::string done_key(int slot, std::size_t index) {
    return std::format("tour.{}.done.{}", slot, index);
}

static std::optional<int> slot_of(void* toon, const void* quest) {
    game::Saved saved = game::saved(toon);

    for (int slot = 0; slot < tour_slots; slot++) {
        if (saved.link(slot_key(slot)) == quest) {
            return slot;
        }
    }

    return std::nullopt;
}

// A slot whose quest is gone is free again.
static std::optional<int> free_slot(void* toon) {
    game::Saved saved = game::saved(toon);

    for (int slot = 0; slot < tour_slots; slot++) {
        if (saved.link(slot_key(slot)) == nullptr) {
            return slot;
        }
    }

    return std::nullopt;
}

static bool is_done(void* toon, int slot, const void* destination) {
    game::Saved saved = game::saved(toon);
    const void* object = objectives::object_of(destination);

    for (std::size_t index = 0; index < objectives::most_tour_targets; index++) {
        if (saved.link(done_key(slot, index)) == object) {
            return true;
        }
    }

    return false;
}

static void clear(void* toon, int slot) {
    game::Saved saved = game::saved(toon);
    saved.erase(slot_key(slot));

    for (std::size_t index = 0; index < objectives::most_tour_targets; index++) {
        saved.erase(done_key(slot, index));
    }
}

static std::vector<void*> left(void* toon, const void* quest) {
    std::vector<void*> targets = objectives::targets(quest);
    std::optional<int> slot = slot_of(toon, quest);

    if (slot) {
        std::erase_if(targets, [&](void* target) { return is_done(toon, *slot, target); });
    }

    return targets;
}

// The target done last, as a place: where the route goes on from.
static std::optional<Place> last_done_place(void* toon, const void* quest) {
    std::optional<int> slot = slot_of(toon, quest);
    const void* last = nullptr;

    for (std::size_t index = 0; slot && index < objectives::most_tour_targets; index++) {
        if (const void* object = game::saved(toon).link(done_key(*slot, index))) {
            last = object;
        }
    }

    for (void* target : last != nullptr ? objectives::targets(quest) : std::vector<void*>{}) {
        if (objectives::object_of(target) == last) {
            return places::of(target);
        }
    }

    return std::nullopt;
}

static Place start_of(void* toon, const void* quest) {
    if (std::optional<Place> last = last_done_place(toon, quest)) {
        return *last;
    }

    void* giver = game::field<game::Link>(quest, "mmoQuest::questGiver").get();

    return giver != nullptr ? places::of(giver) : places::of(toon);
}


namespace tour_progress {

std::vector<void*> route(void* toon, const void* quest) {
    std::vector<void*> targets = left(toon, quest);
    std::vector<void*> ordered;
    Place from = start_of(toon, quest);

    while (!targets.empty()) {
        auto nearest = std::ranges::min_element(targets, {}, [&](void* target) { return places::distance(from, places::of(target)); });
        from = places::of(*nearest);
        ordered.push_back(*nearest);
        targets.erase(nearest);
    }

    return ordered;
}

void* current_target(void* toon, const void* quest) {
    std::vector<void*> targets = route(toon, quest);

    return targets.empty() ? nullptr : targets.front();
}

float route_length(void* toon, const void* quest) {
    std::vector<void*> targets = route(toon, quest);
    float length = 0.0f;

    for (std::size_t index = 1; index < targets.size(); index++) {
        length += places::distance(places::of(targets[index - 1]), places::of(targets[index]));
    }

    return length;
}

std::size_t targets_left(void* toon, const void* quest) {
    return left(toon, quest).size();
}

void mark_done(void* toon, const void* quest, void* destination) {
    std::optional<int> slot = slot_of(toon, quest);

    if (!slot) {
        slot = free_slot(toon);

        if (!slot) {
            return;
        }

        clear(toon, *slot);
        game::saved(toon).set_link(slot_key(*slot), quest);
    }

    game::Saved saved = game::saved(toon);

    if (is_done(toon, *slot, destination)) {
        return;
    }

    for (std::size_t index = 0; index < objectives::most_tour_targets; index++) {
        if (saved.link(done_key(*slot, index)) == nullptr) {
            saved.set_link(done_key(*slot, index), objectives::object_of(destination));

            return;
        }
    }
}

void forget(void* toon, const void* quest) {
    if (std::optional<int> slot = slot_of(toon, quest)) {
        clear(toon, *slot);
    }
}

}
