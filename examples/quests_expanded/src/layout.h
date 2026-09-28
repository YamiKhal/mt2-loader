#pragma once

#include <cstddef>

// Where the game keeps what it has no saved names for, read from its own code when the plugin starts.
struct Layout {
    std::ptrdiff_t pane_visible = 0;
    std::ptrdiff_t button_tooltip = 0;
    std::ptrdiff_t card_quest = 0;
    std::ptrdiff_t advertisement_action = 0;
    std::ptrdiff_t advance_need = 0;
    std::ptrdiff_t loot_need = 0;
    std::ptrdiff_t advertised_npc = 0;
    std::ptrdiff_t quest_advertisement = 0;
    std::ptrdiff_t region_npcs = 0;
    std::ptrdiff_t npc_list = 0;
    std::ptrdiff_t demand_change_keys = 0;
    std::ptrdiff_t action_toon = 0;
    std::ptrdiff_t action_quest = 0;
    std::ptrdiff_t displayed_quest = 0;
    std::ptrdiff_t list_npc = 0;
    std::ptrdiff_t position = 0;
    std::ptrdiff_t speed = 0;
    std::ptrdiff_t plan_toon = 0;
    std::ptrdiff_t advertisement_position = 0;
    std::ptrdiff_t advertised_quest = 0;
    std::ptrdiff_t min_level_slot = 0;
    std::ptrdiff_t max_level_slot = 0;
    std::ptrdiff_t arrow_pool = 0;
    std::ptrdiff_t arrow_from = 0;
    std::ptrdiff_t arrow_changed = 0;
    std::ptrdiff_t arrow_to = 0;
    std::ptrdiff_t pool_count = 0;
    std::ptrdiff_t object_uid = 0;
};

extern Layout layout;

void read_layout();

// A vsWeakPointer inside a game object, at pointer_at: the object it points to, or nullptr once that's gone.
void* weak_object(const void* owner, std::ptrdiff_t pointer_at);
