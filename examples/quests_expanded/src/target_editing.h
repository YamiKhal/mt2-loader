#pragma once

// Editing a quest's targets from the quest card: its details button opens the quest's details window, and while a
// quest is a tour or a branch, the card's Change target adds a target instead of replacing its own (a branch only
// takes other quest givers). One whose own target is destroyed keeps going with the next, where the game would
// destroy the quest.
namespace target_editing {

void install();

}
