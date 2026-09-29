#pragma once

#include <array>
#include <string>
#include <string_view>

// The captive gizmo (gizmo/definition/dungeon_captives_captive.def) extends characters as quest givers do: it has a
// variant per character of the MMO (as character_types names them) and pose, "<character>.<pose>" ("npc.3.sit_loop"),
// each made from its data variant (gizmo/dungeon_captives_captive/base.variant) the first time it's asked for, by the
// game's placing tool or by a saved captive loading. The placing tool lists one character's poses at a time.
namespace captive_variants {

constexpr std::string_view type_name = "dungeon_captives_captive";
constexpr std::string_view base_variant = "base";

// Animations every character has (mmoCharacterActor::Animation) that hold a pose, the first one standing.
constexpr std::array<std::string_view, 9> poses{
    "idle", "idle_combat", "socialise", "sit_loop", "stun_loop", "cast_loop", "cast_charge_loop", "ride", "ghost"
};

std::string variant_name(std::string_view character, std::string_view pose);
// The character and pose of a captive's variant name, or empty ones for any other name.
std::string_view character_of(std::string_view variant);
std::string_view pose_of(std::string_view variant);

// Until show_all, the placing tool lists only this character's poses (the data variant when there's no character).
void show_poses(std::string_view character);
void show_all();

void install();

}
