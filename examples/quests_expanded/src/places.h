#pragma once

// Where things are on the map: players, NPCs and quest targets (a quest destination, as the game points at it).
struct Place {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

namespace places {

Place of(const void* object);
// Across the ground, as the game measures a walk: height left out.
float distance(const Place& from, const Place& to);

}
