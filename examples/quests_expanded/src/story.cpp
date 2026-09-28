#include "story.h"

#include <mt2loader.hpp>

#include <optional>

static std::optional<game::CustomRule> disable_story;


namespace story {

void install() {
    disable_story = game::custom_rule("quests_expanded_disable_story");
}

bool enabled() {
    return game::is_game_open() && disable_story && !disable_story->enabled();
}

}
