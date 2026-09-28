#include "line_thoughts.h"

#include <mt2loader.hpp>

#include <format>
#include <string>

struct Thought {
    const char* word;
    int value;
};

// The game counts a thought as positive below 8 and negative above 7 (mmoToonThought::IsPositive, IsNegative, also
// inlined), and its own are 0 to 49: a value below 0 is positive, one above 49 negative.
constexpr Thought great_thought{ "questline_great", -2 };
constexpr Thought poor_thought{ "questline_poor", 60 };


namespace line_thoughts {

void install() {
    game::Enumeration thoughts = game::enumeration("mmoToonThought::Type");

    for (const Thought& thought : { great_thought, poor_thought }) {
        thoughts.add(thought.word, thought.value);
    }

    // Its table has only the game's own: "{toon_thought_questline_great}" and so on for the mod's.
    game::in("mmoToonThought::GetThoughtString(mmoToonThought::Type)").after([](game::String text, int type) {
        for (const Thought& thought : { great_thought, poor_thought }) {
            if (type == thought.value) {
                return game::String(std::format("{{toon_thought_{}}}", thought.word));
            }
        }

        return text;
    });
}

int great() {
    return great_thought.value;
}

int poor() {
    return poor_thought.value;
}

}
