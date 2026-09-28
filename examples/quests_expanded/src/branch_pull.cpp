#include "branch_pull.h"

#include "chapter_appeal.h"
#include "chapter_quality.h"

#include <array>

constexpr std::array<PlayerType, 5> every_type{ PlayerType::explorer, PlayerType::socialiser, PlayerType::achiever,
    PlayerType::killer, PlayerType::casual };


namespace branch_pull {

std::vector<PlayerType> of(const Questline& line, std::size_t chapter) {
    std::vector<PlayerType> drawn;
    const void* branch_quest = line.chapters[chapter].branch_of;

    if (branch_quest == nullptr) {
        return drawn;
    }

    Quality quality = chapter_quality::of(line, chapter);
    std::vector<Quality> others;

    for (std::size_t other = 0; other < line.chapters.size(); other++) {
        if (other != chapter && line.chapters[other].branch_of == branch_quest) {
            others.push_back(chapter_quality::of(line, other));
        }
    }

    for (PlayerType type : every_type) {
        float here = chapter_appeal::of(type, quality);
        bool leans_here = !others.empty();

        for (const Quality& other : others) {
            leans_here = leans_here && here > chapter_appeal::of(type, other);
        }

        if (leans_here) {
            drawn.push_back(type);
        }
    }

    return drawn;
}

}
