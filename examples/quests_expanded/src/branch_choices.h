#pragma once

#include "questlines.h"

// Which branch of a branch quest each player takes, kept with the player in the saved game (as links to the quest and
// the quest giver), from when the game first asks where they're going until they turn it in.
//
// A player picks among the branches whose first chapter lets them in (its gates), and if none does, among all that
// lead somewhere; each is weighted by how much that chapter draws their player type (chapter_appeal). A player in a
// party goes where another member on the same quest already chose.
namespace branch_choices {

// The quest giver this player goes to: their choice, made now if they have none. Only the game's main thread chooses
// or checks a choice against the branches; elsewhere it's the choice as kept, if any.
void* target(void* toon, const void* quest);
// Their choice as kept, without making one: safe from any thread.
void* chosen(void* toon, const void* quest);
// Where the player is sent on turning the quest in (main thread): the branch whose quest giver they're standing at
// (a party goes where one member chose), else their choice, else the nearest. Good until the lines are traced again.
const HandOff* hand_off_for(void* toon, const void* quest);
void forget(void* toon, const void* quest);

}
