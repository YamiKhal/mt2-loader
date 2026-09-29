#pragma once

#include <cstddef>

// Where the game keeps what it has no saved names for, read from its own code when the plugin starts.
struct Layout {
    std::ptrdiff_t definition_variants = 0;
    std::ptrdiff_t actor_size = 0;
    std::ptrdiff_t costume_skeleton = 0;
    std::ptrdiff_t type_colors = 0;
};

extern Layout layout;

void read_layout();
