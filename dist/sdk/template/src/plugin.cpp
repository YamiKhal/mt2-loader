#include <mt2loader.hpp>


void plugin::init() {
    // mmoModeTitle::Init runs when the title screen opens.
    game::in("mmoModeTitle::Init").before([](void* title) {
        plugin::log("The title screen is opening");
    });

    plugin::log("Ready on game build {}", plugin::game_build());
}
