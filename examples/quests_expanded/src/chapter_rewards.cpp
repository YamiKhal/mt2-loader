#include "chapter_rewards.h"

#include "chapter_appeal.h"
#include "chapter_quality.h"
#include "line_progress.h"
#include "line_thoughts.h"
#include "player.h"
#include "player_types.h"
#include "questlines.h"
#include "story.h"

#include <mt2loader.hpp>

#include <optional>

static game::Function<void(void* subscriber)> reward{ "mmoSubscriber::Reward" };
static game::Function<void(void* subscriber)> big_reward{ "mmoSubscriber::BigReward" };
static game::Function<void(void* subscriber)> soft_frustrate{ "mmoSubscriber::SoftFrustrate" };
static game::Function<void(void* subscriber, int thought, void* building)> frustrate{ "mmoSubscriber::Frustrate" };
static game::Function<void(void* subscriber, int thought)> big_frustrate{ "mmoSubscriber::BigFrustrate" };
static game::Function<void(void* toon, int thought)> add_thought{ "mmoToon::AddThought(mmoToonThought::Type)" };

// Whether the quest being turned in is in a run of a line the player finished before, read before the turn-in, which
// may finish the line.
static bool repeat_run = false;


// A main line's chapter the first time through: players expect a story.
static void react_fully(void* toon, void* subscriber, const Quality& quality, float care) {
    switch (quality.rating) {
    case Rating::great:
        if (care >= chapter_appeal::cares_a_lot) {
            big_reward(subscriber);
        } else {
            reward(subscriber);
        }

        if (care >= chapter_appeal::cares) {
            add_thought(toon, line_thoughts::great());
        }

        break;
    case Rating::fine:
        if (care >= chapter_appeal::cares_a_lot) {
            reward(subscriber);
        }

        break;
    // Everyone who plays it through is let down; the more they care, the more.
    case Rating::poor:
        if (!story::enabled()) {
            break;
        }

        if (care >= chapter_appeal::cares_a_lot) {
            big_frustrate(subscriber, line_thoughts::poor());
        } else if (care >= chapter_appeal::cares) {
            frustrate(subscriber, line_thoughts::poor(), nullptr);
        } else {
            soft_frustrate(subscriber);
        }

        break;
    }
}

// A side line's chapter, or any chapter again: players expect less, so both ways they feel it less and don't dwell on it.
// A main line's poor chapter still lets everyone down; a side line's only the players who care a lot.
static void react_mildly(void* subscriber, const Quality& quality, float care) {
    switch (quality.rating) {
    case Rating::great:
        reward(subscriber);

        break;
    case Rating::fine:
        break;
    case Rating::poor:
        if (story::enabled() && (quality.main || care >= chapter_appeal::cares_a_lot)) {
            soft_frustrate(subscriber);
        }

        break;
    }
}

static void react(void* toon, const Quality& quality) {
    void* subscriber = player_types::subscriber_of(toon);
    std::optional<PlayerType> type = player_types::of(toon);

    if (subscriber == nullptr || !type) {
        return;
    }

    float care = chapter_appeal::of(*type, quality);

    if (quality.main && !repeat_run) {
        react_fully(toon, subscriber, quality, care);
    } else {
        react_mildly(subscriber, quality, care);
    }
}

// The chapter a quest ends, if it's the last of one.
static std::optional<std::size_t> chapter_ended_by(const Questline& line, const void* quest) {
    std::size_t chapter = questlines::chapter_of_quest(quest);
    std::size_t end = chapter_quality::end_quest(line, chapter);

    if (end == 0 || line.quests[end - 1] != quest) {
        return std::nullopt;
    }

    return chapter;
}


namespace chapter_rewards {

void install() {
    game::in("mmoToon::TurnInQuest").before([](void* toon, void* instance) {
        void* quest = player::quest_of(instance);
        const Questline* line = quest != nullptr ? questlines::line_of_quest(quest) : nullptr;

        repeat_run = line != nullptr && line_progress::has_finished(toon, *line);
    });

    game::in("mmoToon::TurnInQuest").after([](void* toon, void* instance) {
        questlines::update();

        void* quest = player::quest_of(instance);
        const Questline* line = quest != nullptr ? questlines::line_of_quest(quest) : nullptr;
        std::optional<std::size_t> chapter = line != nullptr ? chapter_ended_by(*line, quest) : std::nullopt;

        if (chapter) {
            react(toon, chapter_quality::of(*line, *chapter));
        }
    });
}

}
