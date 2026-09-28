#include "chapter_buzz.h"

#include "line_stats.h"

#include <vector>

static std::vector<const char*> quality_remarks(const Quality& quality) {
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

    return remarks;
}

// A branch no one has taken while another of its branch quest's has.
static bool passed_by(const Questline& line, std::size_t chapter) {
    const void* branch_quest = line.chapters[chapter].branch_of;

    if (branch_quest == nullptr || line_stats::players_starting(line, chapter) > 0) {
        return false;
    }

    for (std::size_t other = 0; other < line.chapters.size(); other++) {
        if (other != chapter && line.chapters[other].branch_of == branch_quest && line_stats::players_starting(line, other) > 0) {
            return true;
        }
    }

    return false;
}


namespace chapter_buzz {

std::string of(const Questline& line, std::size_t chapter) {
    std::vector<const char*> remarks = quality_remarks(chapter_quality::of(line, chapter));

    if (passed_by(line, chapter)) {
        remarks.push_back("{quests_expanded_buzz_passed_by}");
    }

    if (remarks.empty()) {
        remarks.push_back("{quests_expanded_buzz_great}");
    }

    if (line.chapters[chapter].ending && line.ends.size() > 1) {
        remarks.push_back("{quests_expanded_buzz_ending}");
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
