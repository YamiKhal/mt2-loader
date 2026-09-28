#include "settings.h"

#include <mt2loader.hpp>

Settings settings;

void read_settings() {
    settings.main_preference = plugin::setting<int>("main_preference");
    settings.repeat_cooldown_days = plugin::setting<int>("repeat_cooldown_days");
}
