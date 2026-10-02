#include "target_arrows.h"

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
        game::field<Place>(arrow.arrow, "mmoArrow::from") = from;
        game::field<Place>(arrow.arrow, "mmoArrow::to") = to;
        game::field<bool>(arrow.arrow, "mmoArrow::changed") = true;
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
    void* quest = game::field<game::WeakPointer>(display, "mmoQuestDisplay::quest").get();
    void* giver = quest != nullptr ? game::field<game::Link>(quest, "mmoQuest::questGiver").get() : nullptr;
    std::vector<void*> targets = giver != nullptr ? objectives::targets(quest) : std::vector<void*>{};
    auto found = arrows_of.find(display);

    if (targets.size() <= 1 && found == arrows_of.end()) {
        return;
    }

    std::vector<TourArrow>& arrows = arrows_of[display];
    std::size_t wanted = targets.size() > 1 ? targets.size() - 1 : 0;

    while (arrows.size() < wanted) {
        void* arrow = borrow_arrow(game::object_in(display, "mmoQuestDisplay::arrows"));
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
        if (game::object_in(const_cast<void*>(found->first), "mmoQuestDisplay::arrows") != pool) {
            continue;
        }

        for (TourArrow& arrow : found->second) {
            game::destroy(arrow.arrow);
            game::field<int>(pool, "vsPool<mmoArrow>::m_totalCount")--;
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
