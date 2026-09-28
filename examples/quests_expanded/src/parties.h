#pragma once

#include <vector>

// A party's players (mmoParty::members, each an mmoPartyMember with its player).
namespace parties {

std::vector<void*> members(void* party);
// The other players in this player's party, if they're in one.
std::vector<void*> others_with(void* toon);

}
