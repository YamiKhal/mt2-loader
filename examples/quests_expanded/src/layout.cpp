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

// mov rax, [rbx + pane]; test rax, rax; je; movzx edx, byte [rbx + selected]; mov [rax + visible], dl: a card showing or
// hiding its highlight.
static void find_pane_visibility() {
    game::Address shown = game::find("mmoQuestSelector::SetSelected").scan("48 8B 83 ?? ?? ?? ?? 48 85 C0 74 ?? 0F B6 93 ?? ?? ?? ?? 88 50 ??");
    layout.pane_visible = read_offset8(shown + 21);
}

// cmp qword [rcx + tooltip], 0: a button with no tooltip shows none.
static void find_button_tooltip() {
    game::Address check = game::find("mmoButtonPane::MouseOver").scan("48 83 B9 ?? ?? ?? ?? 00 74");
    layout.button_tooltip = read_offset32(check + 3);
}

// lea rdi, [rbx + quest]; mov rcx, rdi; call: the card reading its quest.
static void find_card_quest() {
    game::Address quest = game::find("mmoQuestSelector::RefreshQuest").scan("48 8D BB ?? ?? ?? ?? 48 89 F9 E8");
    layout.card_quest = read_offset32(quest + 3);
}

// mov [rdi + action], 7; movss [rdi + advance], xmm0; movss [rdi + loot], xmm0; later lea r12, [rdi + npc]: the
// player's "Get quest" advertisement being made.
static void find_advertisement_fields() {
    game::Address self = game::find("mmoToon::GetSelfAdvertisements");
    game::Address needs = self.scan("C7 47 ?? 07 00 00 00 F3 0F 11 87 ?? ?? ?? ?? F3 0F 11 87 ?? ?? ?? ??");
    game::Address npc = needs.scan("4C 8D A7 ?? ?? ?? ??", 0x80);

    layout.advertisement_action = read_offset8(needs + 2);
    layout.advance_need = read_offset32(needs + 11);
    layout.loot_need = read_offset32(needs + 19);
    layout.advertised_npc = read_offset32(npc + 3);
}

// mov rcx, [rcx + advertisement]; mov r8, [rcx + ...]: a quest renaming its "Do quest" advertisement.
static void find_quest_advertisement() {
    game::Address advertisement = game::find("mmoQuest::SetAdvertisements() [clone .part.0]").scan("48 8B 89 ?? ?? ?? ?? 4C 8B 41");
    layout.quest_advertisement = read_offset32(advertisement + 3);
}

// mov rdi, [rax + npcs]; mov eax, [rdi + count]: a player looking through the NPCs of their region.
static void find_region_npcs() {
    game::Address npcs = game::find("mmoToon::FindNearbyNewQuestGiver").scan("48 8B B8 ?? ?? ?? ?? 8B 47 ??");
    constexpr std::ptrdiff_t count_in_list = 0x10;

    layout.region_npcs = read_offset32(npcs + 3);
    layout.npc_list = read_offset8(npcs + 9) - count_in_list;
}

// lea rcx, [rbx + keys]; mov rdx, rsi; call: a release demand adding a change key that meets it.
static void find_demand_change_keys() {
    game::Address keys = game::find("mmoReleaseDemand::_SetMatchingChangeKeys").scan("48 8D 4B ?? 48 89 F2 E8");
    layout.demand_change_keys = read_offset8(keys + 3);
}

void read_layout() {
    find_pane_visibility();
    find_button_tooltip();
    find_card_quest();
    find_advertisement_fields();
    find_quest_advertisement();
    find_region_npcs();
    find_demand_change_keys();
}

void* weak_object(const void* owner, std::ptrdiff_t pointer_at) {
    struct WeakPointer {
        void* pointer_class;
        void* object;
        void** proxy;
    };

    const auto& pointer = game::field<WeakPointer>(owner, static_cast<std::size_t>(pointer_at));

    if (pointer.proxy == nullptr || *pointer.proxy == nullptr) {
        return nullptr;
    }

    return pointer.object;
}
