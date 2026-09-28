#include <mt2loader.hpp>


void plugin::init() {
    plugin::log("Hello from {}: game build {}, loader {}", plugin::id(), plugin::game_build(), plugin::loader_version());
}
