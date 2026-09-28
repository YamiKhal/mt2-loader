// Logs its settings, for tests/plugin_tests.sh: set in the mod manager, deployed next to the plugin, read here.
#include <mt2loader.hpp>

void plugin::init() {
    plugin::log("greeting={} count={}", plugin::setting<std::string>("greeting"), plugin::setting<int>("count"));
}
