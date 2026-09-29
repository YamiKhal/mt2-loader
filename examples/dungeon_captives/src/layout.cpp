#include "layout.h"

#include <mt2loader.hpp>

#include <cstdint>

Layout layout;


static std::ptrdiff_t read_offset32(game::Address at) {
    return at.read<std::int32_t>();
}

static std::ptrdiff_t read_offset8(game::Address at) {
    return at.read<std::int8_t>();
}

// lea rbp, [rcx + variants]: a gizmo type looking through its variants by name.
static void find_definition_variants() {
    game::Address variants = game::find("mmoGizmoDefinition::GetVariant").scan("48 8D A9 ?? ?? ?? ??");
    layout.definition_variants = read_offset32(variants + 3);
}

// mov ecx, size; mov rbp, rax; call new; lea rdx, [rbp + skeleton]: a character's actor made for its costume's
// skeleton. Then mov r8d, [r12 + colors]; call: the costume put on it in the type's colors.
static void find_character_actor_fields() {
    game::Address make = game::find("mmoCharacterModelManager::MakeActor_Internal");
    game::Address actor = make.scan("B9 ?? ?? 00 00 48 89 C5 E8 ?? ?? ?? ?? 48 8D 55 ??");
    game::Address colors = make.scan("45 8B 44 24 ?? E8");

    layout.actor_size = read_offset32(actor + 1);
    layout.costume_skeleton = read_offset8(actor + 16);
    layout.type_colors = read_offset8(colors + 4);
}

void read_layout() {
    find_definition_variants();
    find_character_actor_fields();
}
