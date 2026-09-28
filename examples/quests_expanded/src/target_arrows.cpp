#include "target_arrows.h"

#include "layout.h"
#include "objectives.h"
#include "places.h"

#include <mt2loader.hpp>

#include <cstddef>
#include <unordered_map>
#include <vector>

struct TourArrow {
    void* arrow = nullptr;
    bool shown = false;
    Place from;
    Place to;
};

static game::Function<void*(void* pool)> borrow_arrow{ "vsPool<mmoArrow>::Borrow" };
static game::Function<void(void* arrow, int layer)> put_on_scene{ "vsEntity::RegisterOnScene(int)" };
static game::Function<void(void* arrow, float opacity)> fade{ "mmoArrow::Fade" };
static game::Function<float(void* map, const Place* place)> height_at{ "mmoMap::GetHeightAt" };

// The map's quest display's extra arrows, borrowed from its own pool and kept for the next tour; the game gives its
// one arrow back with code of its own, so these go when the pool closes.
static std::unordered_map<const void*, std::vector<TourArrow>> arrows_of;


static bool same(const Place& one, const Place& other) {
    return one.x == other.x && one.y == other.y && one.z == other.z;
}

static void point(TourArrow& arrow, void* target, void* giver) {
    Place from = places::of(target);
    from.y = height_at(game::singleton("mmoMap"), &from);
    Place to = places::of(giver);

    if (!same(from, arrow.from) || !same(to, arrow.to)) {
        arrow.from = from;
        arrow.to = to;
        game::field<Place>(arrow.arrow, static_cast<std::size_t>(layout.arrow_from)) = from;
        game::field<Place>(arrow.arrow, static_cast<std::size_t>(layout.arrow_to)) = to;
        game::field<bool>(arrow.arrow, static_cast<std::size_t>(layout.arrow_changed)) = true;
    }

    if (!arrow.shown) {
        fade(arrow.arrow, 1.0f);
        arrow.shown = true;
    }
}

static void hide(TourArrow& arrow) {
    if (arrow.shown) {
        fade(arrow.arrow, 0.0f);
        arrow.shown = false;
    }
}

// The quest's own target has the game's arrow; each of its other targets gets one of these.
static void show_arrows(void* display) {
    void* quest = weak_object(display, layout.displayed_quest);
    void* giver = quest != nullptr ? game::field<game::Link>(quest, "mmoQuest::questGiver").get() : nullptr;
    std::vector<void*> targets = giver != nullptr ? objectives::targets(quest) : std::vector<void*>{};
    auto found = arrows_of.find(display);

    if (targets.size() <= 1 && found == arrows_of.end()) {
        return;
    }

    std::vector<TourArrow>& arrows = arrows_of[display];
    std::size_t wanted = targets.size() > 1 ? targets.size() - 1 : 0;

    while (arrows.size() < wanted) {
        void* arrow = borrow_arrow(static_cast<std::byte*>(display) + layout.arrow_pool);
        put_on_scene(arrow, 0);
        arrows.push_back(TourArrow{ arrow, false, {}, {} });
    }

    for (std::size_t index = 0; index < arrows.size(); index++) {
        if (index < wanted) {
            point(arrows[index], targets[index + 1], giver);
        } else {
            hide(arrows[index]);
        }
    }
}


// The pool checks every arrow it made came back: these are deleted as it would, and no longer counted.
static void release_arrows(void* pool) {
    for (auto found = arrows_of.begin(); found != arrows_of.end(); ++found) {
        if (static_cast<const std::byte*>(found->first) + layout.arrow_pool != pool) {
            continue;
        }

        for (TourArrow& arrow : found->second) {
            using Delete = void (*)(void* arrow);
            void* const* table = *static_cast<void* const* const*>(arrow.arrow);
            constexpr std::size_t deleting_destructor = 1;

            reinterpret_cast<Delete>(table[deleting_destructor])(arrow.arrow);
            game::field<int>(pool, static_cast<std::size_t>(layout.pool_count))--;
        }

        arrows_of.erase(found);

        return;
    }
}


namespace target_arrows {

void install() {
    game::in("vsPool<mmoArrow>::~vsPool").before([](void* pool) {
        release_arrows(pool);
    });

    // A new game's display: the last one's arrows went with it.
    game::in("mmoQuestDisplay::mmoQuestDisplay").after([](void* display) {
        arrows_of.erase(display);
    });

    game::in("mmoQuestDisplay::DisplayQuest").after([](void* display, void*) {
        show_arrows(display);
    });

    // Targets move (an elite roams), and targets are added while the quest shows.
    game::in("mmoQuestDisplay::Update(float)").after([](void* display, float) {
        if (arrows_of.contains(display)) {
            show_arrows(display);
        }
    });
}

}
