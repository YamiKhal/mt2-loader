#include "story_releases.h"

#include "line_titles.h"
#include "story_demand.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <format>
#include <optional>

constexpr const char* released_key = "released_chapters";
// As many as a new NPC type brings.
constexpr int feature_points = 10;

static game::Function<void(void* release, void* object, const game::String& key, const game::LocalizedText& text, int points)> add_feature{
    "mmoRelease::AddFeature" };
static game::Function<void(void* release, void* object, const game::String& key)> remove_feature{ "mmoRelease::_EnsureNoFeature" };
static game::Function<bool(void* release, void* object, const game::String& key)> has_feature{
    "mmoRelease::_HasFeature(mmoObject*, std::string const&)" };


// Every chapter counts, good or bad: a poor one brings bad buzz once players reach it.
static int story_chapters(const Questline& line) {
    return static_cast<int>(line.chapters.size());
}

static int released_chapters(const Questline& line) {
    game::Saved saved = game::saved(line.start);

    if (std::optional<int> released = saved.find<int>(released_key)) {
        return *released;
    }

    int now = story_chapters(line);
    saved.set(released_key, now);

    return now;
}

// Lines that were main once have a count; they lose the feature when they stop being main.
static void scan(void* release) {
    questlines::update();

    game::String key(story_demand::change_key());

    for (const Questline& line : questlines::all()) {
        bool counted = game::saved(line.start).has(released_key);

        if (line.settings.main && story_releases::unreleased_chapters(line) > 0) {
            game::LocalizedText text(std::format("{{quests_expanded_feature_chapter}} {}", line_titles::line_title(line)));
            add_feature(release, line.start, key, text, feature_points);
        } else if (counted) {
            remove_feature(release, line.start, key);
        }
    }
}

static void released(void* manager) {
    void* release = game::field<void*>(manager, "mmoReleaseManager::current");

    if (release == nullptr) {
        return;
    }

    questlines::update();

    game::String key(story_demand::change_key());

    for (const Questline& line : questlines::all()) {
        if (line.settings.main && has_feature(release, line.start, key)) {
            game::saved(line.start).set(released_key, story_chapters(line));
        }
    }
}


namespace story_releases {

void install() {
    // The last of the game's own scans, which the release runs as it ticks and before it's shown.
    game::in("mmoRelease::_ScanFor_NewDungeons").after([](void* release) {
        scan(release);
    });

    game::in("mmoReleaseManager::DoRelease").before([](void* manager) {
        released(manager);
    });
}

int unreleased_chapters(const Questline& line) {
    return line.settings.main ? std::max(story_chapters(line) - released_chapters(line), 0) : 0;
}

}
