#include "chapter_buzz.h"

#include <vector>

namespace chapter_buzz {

std::string of(const Quality& quality) {
    std::vector<const char*> remarks;

    if (quality.broken) {
        remarks.push_back("{quests_expanded_buzz_hand_off}");
    }

    // A side line is asked for less: two kinds of quest, a shorter length, and no elite or second quest giver.
    if (quality.kinds < (quality.main ? 4 : 2)) {
        remarks.push_back("{quests_expanded_buzz_kinds}");
    }

    if (quality.main && !quality.elite) {
        remarks.push_back("{quests_expanded_buzz_elite}");
    }

    const LengthRange& length = chapter_quality::length_range(quality.main);

    if (quality.length < length.shortest) {
        remarks.push_back("{quests_expanded_buzz_short}");
    } else if (quality.length > length.longest) {
        remarks.push_back("{quests_expanded_buzz_long}");
    }

    if (!quality.climbs) {
        remarks.push_back("{quests_expanded_buzz_levels}");
    }

    if (quality.main && quality.givers < 2) {
        remarks.push_back("{quests_expanded_buzz_givers}");
    }

    if (remarks.empty()) {
        remarks.push_back("{quests_expanded_buzz_great}");
    }

    std::string text;

    for (const char* remark : remarks) {
        if (!text.empty()) {
            text += "\n";
        }

        text += std::string("\"") + remark + "\"";
    }

    return text;
}

}
