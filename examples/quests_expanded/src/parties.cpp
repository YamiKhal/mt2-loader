#include "parties.h"

#include <mt2loader.hpp>

#include <algorithm>

namespace parties {

std::vector<void*> members(void* party) {
    std::vector<void*> found;

    for (void* member : game::field<game::Objects>(party, "mmoParty::members")) {
        if (void* toon = member != nullptr ? game::field<void*>(member, "mmoPartyMember::toon") : nullptr) {
            found.push_back(toon);
        }
    }

    return found;
}

std::vector<void*> others_with(void* toon) {
    void* party = game::field<void*>(toon, "mmoToon::party");
    std::vector<void*> found = party != nullptr ? members(party) : std::vector<void*>{};
    std::erase(found, toon);

    return found;
}

}
