#include "player_types.h"

#include <mt2loader.hpp>

#include <array>

struct Interest {
    PlayerType type;
    float weight;
};

// Explorers want the story most, then Achievers (lines to finish) and Socialisers (quest givers to meet); Killers want
// fights more than stories, and Casuals care least.
constexpr std::array<Interest, 5> interest_by_type{ {
    { PlayerType::explorer, 1.0f },
    { PlayerType::achiever, 0.7f },
    { PlayerType::socialiser, 0.6f },
    { PlayerType::killer, 0.3f },
    { PlayerType::casual, 0.2f },
} };

// With no players yet, the story matters as much as an average MMO's.
constexpr float interest_without_players = 0.5f;

static game::Function<int(void* manager, int type)> subscriber_count{ "mmoSubscriberManager::GetSubscriberCount(mmoSubscriber::Type)" };


namespace player_types {

void* subscriber_of(void* toon) {
    return toon != nullptr ? game::field<void*>(toon, "mmoToon::subscriber") : nullptr;
}

std::optional<PlayerType> of(void* toon) {
    void* subscriber = subscriber_of(toon);

    if (subscriber == nullptr) {
        return std::nullopt;
    }

    int type = game::field<int>(subscriber, "mmoSubscriber::type");

    if (type < static_cast<int>(PlayerType::explorer) || type > static_cast<int>(PlayerType::casual)) {
        return std::nullopt;
    }

    return static_cast<PlayerType>(type);
}

float story_interest() {
    void* manager = game::singleton("mmoSubscriberManager");

    if (manager == nullptr) {
        return interest_without_players;
    }

    int players = 0;
    float interest = 0.0f;

    for (const Interest& type : interest_by_type) {
        int count = subscriber_count(manager, static_cast<int>(type.type));

        players += count;
        interest += type.weight * static_cast<float>(count);
    }

    return players > 0 ? interest / static_cast<float>(players) : interest_without_players;
}

}
