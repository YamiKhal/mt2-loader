#pragma once

#include "questlines.h"

// What a player has done with a main or repeatable questline, as far as the game's own records can't tell: kept in
// the saved game with the player, and only once they finish it.
namespace line_progress {

// Whether the player can't start the line now: they're in a run, finished it for good, or wait for its cooldown.
bool is_locked_out(void* toon, const Questline& line);

void finish(void* toon, const Questline& line);
// Whether the player finished the line before: only a main or repeatable line keeps it.
bool has_finished(const void* toon, const Questline& line);

}
