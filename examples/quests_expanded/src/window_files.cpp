#include "window_files.h"

#include <mt2loader.hpp>

static const char* const mod_windows[] = { "report_quests.win", "quest_settings.win" };

static game::Function<int(void* windows, const game::String& filename)> append_from_file{ "vsObjectArray<mmoWindow>::AppendFromFilename" };
static game::Function<bool(const game::String& filename)> file_exists{ "vsFile::Exists" };


namespace window_files {

void install() {
    // The third call loads the main list of window files, one by one; the mod's come right after the Thoughts report.
    game::in("mmoAssetPreload::BeginLoad").call("vsObjectArray<mmoWindow>::AppendFromFilename").nth(3)
        .after([](int loaded, void* windows, const game::String& filename) {
            if (filename.view() != "report_thoughts.win") {
                return loaded;
            }

            for (const char* mod_window : mod_windows) {
                if (file_exists(game::String(mod_window))) {
                    append_from_file(windows, game::String(mod_window));
                } else {
                    plugin::log("{} is missing: add the mod with the mod manager, which puts its windows in place", mod_window);
                }
            }

            return loaded;
        });
}

}
