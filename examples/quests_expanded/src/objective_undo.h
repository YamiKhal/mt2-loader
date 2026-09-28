#pragma once

// The quest card's undo rebuilds a quest giver's quests as new ones, from the game's own copy of them, which knows
// nothing of tours and branches. The mod keeps its own copy of each quest's objective and targets alongside the
// game's and puts it back on the rebuilt quest.
namespace objective_undo {

void install();

}
