#pragma once

// Players on a tour quest: each one's copy of the quest (mmoQuestInstance) points at their next target (the nearest
// not done yet), with that target's own action, and is ready to turn in once every target is done. Finishing one
// target ends the player's quest action, so they plan again, now for the next; a party's members finish it together.
// Weighing the quest against what else to do, a player counts the walk round the whole tour.
namespace steering {

void install();
// Tours or their players' progress changed: what each player is steered to is worked out again.
void changed();

}
