#pragma once

// Players keep their place in the main questline: when the game forgets the quests a player has outleveled (after a
// game loads, and when they level up), it keeps the record of an unfinished main questline quest. Finished and passed
// ones are forgotten as the game does.
namespace main_line_places {

void install();

}
