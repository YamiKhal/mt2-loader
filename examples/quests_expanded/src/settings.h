#pragma once

// The player's choices in the mod manager (config.json).
struct Settings {
    // How much more players want the main questline's quests: the needs those quests meet, times 1 + this / 100.
    int main_preference = 50;
    // Days before a player can start a repeatable main questline again.
    int repeat_cooldown_days = 0;
};

extern Settings settings;

void read_settings();
