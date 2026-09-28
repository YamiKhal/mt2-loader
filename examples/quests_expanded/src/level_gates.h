#pragma once

// Which players a quest giver takes on: everything the game uses to pick quest givers asks for their level range
// (mmoNPC::GetQuestLevelRange), in three places. The mod changes the answer there, and only for quest givers in a
// questline:
// - a quest giver whose next quest for the player is a hand-in takes on no one: it's only reached through its hand-off;
// - the main questline's first quest giver takes on anyone from its first quest's level up, unless they're locked out;
// - the rest of the main questline takes on players at any level, so no one loses their place in it;
// - a chapter's gate (chapter_gates) comes first: a line's first quest giver takes on only players it lets in, from its
//   level up when it has one, and the quest that leads into a later chapter is given only to players that chapter
//   lets in, so the rest wait there until they reach its level.
namespace level_gates {

void install();

}
