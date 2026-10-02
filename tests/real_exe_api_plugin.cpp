// Runs against the real MT2.exe (probe_plugin_real_exe): the lookups the API makes from the game's code, where they
// can be made without the game running.
#include <mt2loader.hpp>

#include <cstdint>

static game::Virtual<int(void* destination)> min_level_of{ "mmoQuestDestination::GetQuestMinLevel" };

void plugin::init() {
    game::Array<game::String> texts("vsArray<std::string>", {});
    game::Array<std::int32_t> kinds("vsArray<CharacterType>", {});
    plugin::log("Arrays of the game's types found");

    game::on_destroy("mmoDoSetNPCQuests", [](void*) {});
    game::on_destroy("mmoNPC", [](void*) {});
}
