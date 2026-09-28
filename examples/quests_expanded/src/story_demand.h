#pragma once

// "New story chapter", a release demand beside the game's own (mmoReleaseDemand::Type): players want it while the MMO
// has a main questline and the Story demand is enabled. Its heat grows faster the more the players care about story
// (player_types::story_interest). A release with a new main questline chapter meets it (story_releases).
namespace story_demand {

void install();

// Adds or takes away the demand now, after the main questline changed.
void refresh();

// The change key a release feature meets it with.
const char* change_key();

}
