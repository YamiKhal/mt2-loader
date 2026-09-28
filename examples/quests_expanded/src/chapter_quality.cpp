#include "chapter_quality.h"

#include "quest.h"

#include <algorithm>
#include <set>
#include <vector>

constexpr int kinds_points = 40;
constexpr int main_kinds_wanted = 4;
constexpr int side_kinds_wanted = 2;
constexpr int elite_points = 15;
constexpr int good_length_points = 20;
constexpr int near_length_points = 10;
constexpr int climbing_points = 15;
constexpr int givers_points = 10;
constexpr int broken_most = 20;

constexpr int great_from = 70;
constexpr int fine_from = 40;

// A level lower than the quest before by this much still climbs: players outlevel a quest or two.
constexpr int level_dip_allowed = 1;

static int length_points(int length, bool main) {
    const LengthRange& range = chapter_quality::length_range(main);

    if (length >= range.shortest && length <= range.longest) {
        return good_length_points;
    }

    // Half as long, or a quarter longer, still earns the near points.
    if (length >= range.shortest / 2 && length <= range.longest + range.longest / 4) {
        return near_length_points;
    }

    return 0;
}

static bool levels_climb(const std::vector<void*>& quests) {
    for (std::size_t index = 1; index < quests.size(); index++) {
        if (quest::min_level(quests[index]) < quest::min_level(quests[index - 1]) - level_dip_allowed) {
            return false;
        }
    }

    return true;
}

static Rating rating_of(int score) {
    if (score >= great_from) {
        return Rating::great;
    }

    return score >= fine_from ? Rating::fine : Rating::poor;
}


namespace chapter_quality {

std::size_t first_quest(const Questline& line, std::size_t chapter) {
    return line.chapters[chapter].first_quest;
}

std::size_t end_quest(const Questline& line, std::size_t chapter) {
    return chapter + 1 < line.chapters.size() ? line.chapters[chapter + 1].first_quest : line.quests.size();
}

Quality of(const Questline& line, std::size_t chapter) {
    std::vector<void*> quests(line.quests.begin() + first_quest(line, chapter), line.quests.begin() + end_quest(line, chapter));
    std::set<QuestKind> kinds;
    std::set<void*> givers;
    Quality quality;

    for (void* each : quests) {
        const HandOff* hand_off = questlines::hand_off(each);

        kinds.insert(quest::kind(each));
        givers.insert(quest::giver(each));
        quality.broken = quality.broken || (hand_off != nullptr && hand_off->broken != Broken::no);
    }

    int kinds_wanted = line.settings.main ? main_kinds_wanted : side_kinds_wanted;

    quality.main = line.settings.main;
    quality.kinds = static_cast<int>(kinds.size());
    quality.elite = kinds.contains(QuestKind::elite);
    quality.length = static_cast<int>(quests.size());
    quality.climbs = levels_climb(quests);
    quality.givers = static_cast<int>(givers.size());

    quality.score = std::min(quality.kinds, kinds_wanted) * kinds_points / kinds_wanted + length_points(quality.length, quality.main);
    quality.score += (quality.elite ? elite_points : 0) + (quality.climbs ? climbing_points : 0);
    quality.score += quality.main && quality.givers > 1 ? givers_points : 0;

    if (quality.broken) {
        quality.score = std::min(quality.score, broken_most);
    }

    quality.rating = rating_of(quality.score);

    return quality;
}

}
