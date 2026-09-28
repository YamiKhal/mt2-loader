#pragma once

// The NPC window's quest cards are kept from one NPC to the next and only refilled, so the card being edited stayed
// selected with the map still editing the last NPC's quest. Showing another NPC now lets go of it first, as the game
// does when an NPC's quests change.
namespace card_selection {

void install();

}
