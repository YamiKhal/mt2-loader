#pragma once

#include "chapter_quality.h"
#include "player_types.h"

// How much a chapter draws a player of a type: 1 is an ordinary player, 1.5 and up a player it was made for. Explorers
// play for the story and Achievers to finish; Socialisers like meeting several quest givers, Killers an elite.
namespace chapter_appeal {

constexpr float cares = 1.0f;
constexpr float cares_a_lot = 1.5f;

float of(PlayerType type, const Quality& quality);

}
