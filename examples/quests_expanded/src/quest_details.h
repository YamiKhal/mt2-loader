#pragma once

// A quest's details window, opened from its card while editing: a tour's targets, each with a button to take it out,
// and the quest's lore (the game's own, unused, quest description). A tour's targets are added with the card's own
// Change target.
namespace quest_details {

void install();
void open(void* quest);
// After the card changed the quest's type or targets.
void refresh_if_showing(const void* quest);

}
