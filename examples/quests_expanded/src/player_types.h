#pragma once

#include <optional>

// The game's player types (mmoSubscriber::Type, Bartle's): each subscriber has one, picked by the MMO's Explore,
// Social, Achieve and Kill statistics when they join.
enum class PlayerType {
    explorer,
    socialiser,
    achiever,
    killer,
    casual,
};

namespace player_types {

// The type of the subscriber playing this character, if the game knows it.
std::optional<PlayerType> of(void* toon);
void* subscriber_of(void* toon);

// How much the MMO's players care about its story, from 0 to 1: each type's share of the subscribers, weighted by how
// much that type cares.
float story_interest();

}
