#include "chapter_gates.h"

#include "player_classes.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <string>

// Kept for "any class" too, so a chapter can open up again after one that's for a class.
constexpr int any_class = -1;

struct GateKeys {
    void* npc = nullptr;
    std::string level;
    std::string player_class;
};

// The line's first quest giver may also start a later chapter (a loop hands players back to it), so the first
// chapter's gate has keys of its own.
static GateKeys keys_of(const Questline& line, std::size_t chapter) {
    if (chapter == 0) {
        return GateKeys{ line.start, "first_chapter_level", "first_chapter_class" };
    }

    return GateKeys{ line.chapters[chapter].giver, "chapter_level", "chapter_class" };
}


namespace chapter_gates {

ChapterGate of(const Questline& line, std::size_t chapter) {
    std::optional<int> level;
    std::optional<int> player_class;

    for (std::size_t each = chapter + 1; each-- > 0 && (!level || !player_class);) {
        GateKeys keys = keys_of(line, each);
        game::Saved saved = game::saved(keys.npc);

        if (!level) {
            level = saved.find<int>(keys.level);
        }

        if (!player_class) {
            player_class = saved.find<int>(keys.player_class);
        }
    }

    ChapterGate gate;
    gate.level = level.value_or(0);

    if (player_class && *player_class != any_class) {
        gate.player_class = player_class;
    }

    return gate;
}

void set_level(const Questline& line, std::size_t chapter, int level) {
    GateKeys keys = keys_of(line, chapter);
    game::saved(keys.npc).set(keys.level, std::max(level, 0));
}

void set_class(const Questline& line, std::size_t chapter, std::optional<int> player_class) {
    GateKeys keys = keys_of(line, chapter);
    game::saved(keys.npc).set(keys.player_class, player_class.value_or(any_class));
}

bool lets_in(const void* toon, const Questline& line, std::size_t chapter) {
    ChapterGate gate = of(line, chapter);

    if (gate.level > 0 && game::field<int>(toon, "mmoCharacter::level") < gate.level) {
        return false;
    }

    return !gate.player_class || !player_classes::exists(*gate.player_class) || player_classes::of(toon) == gate.player_class;
}

}
