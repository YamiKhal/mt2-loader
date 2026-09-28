#include "layout.h"

#include <mt2loader.hpp>

#include <cstdint>
#include <string_view>

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

// mov rcx, [rbx + toon]; call: a player's quest action finding the player's copy of its quest.
static void find_action_toon() {
    game::Address toon = game::find("mmoToonDoQuestAction::Tick").scan("48 8B 4B ?? E8");
    layout.action_toon = read_offset8(toon + 3);
}

// mov rdx, [rbx + quest]; test rdx, rdx: a player's quest step reading its quest's weak pointer (its class, then the
// quest).
static void find_action_quest() {
    game::Address quest = game::find("mmoToonDoQuestAction::Tick").scan("48 8B 93 ?? ?? ?? ?? 48 85 D2");
    layout.action_quest = read_offset32(quest + 3) - static_cast<std::ptrdiff_t>(sizeof(void*));
}

// lea r12, [rsi + quest]: the map's quest display keeping the quest whose target is being picked.
static void find_displayed_quest() {
    game::Address quest = game::find("mmoQuestDisplay::SetNewDestination").scan("4C 8D A6 ?? ?? ?? ??");
    layout.displayed_quest = read_offset32(quest + 3);
}

// lea r13, [rbx + cards]; lea r14, [rbx + npc]: the quest cards checking they still show their NPC's quests.
static void find_list_npc() {
    game::Address npc = game::find("mmoQuestList::UpdateUI").scan("4C 8D AB ?? ?? ?? ?? 4C 8D B3 ?? ?? ?? ??");
    layout.list_npc = read_offset32(npc + 10);
}

// mov rax, [rcx + toon]; movss xmm6, [rax + x]; ...; movss xmm0, [rbx + x]: a player's plan timing the walk to an
// advertisement, from the player's position; then divss xmm0, [rax + speed].
static void find_travel_fields() {
    game::Address travel = game::find("mmoToonPlanTask::_EstimateTravelTimeTo");
    game::Address toon = travel.scan("48 8B 41 ?? F3 0F 10 B0 ?? ?? ?? ?? F3 0F 10 B8 ?? ?? ?? ??");
    game::Address place = travel.scan("F3 0F 10 43 ?? F3 0F 10 4B ??");
    game::Address speed = travel.scan("F3 0F 5E 80 ?? ?? ?? ?? 48 83 C4");

    layout.plan_toon = read_offset8(toon + 3);
    layout.position = read_offset32(toon + 8);
    layout.advertisement_position = read_offset8(place + 4);
    layout.speed = read_offset32(speed + 4);
}

// mov rdi, [rbp + quest]; test rcx, rcx: a quest's advertisement making the player's plan.
static void find_advertised_quest() {
    game::Address quest = game::find("mmoQuestAdvertisement::BuildPlan").scan("48 8B BD ?? ?? ?? ?? 48 85 C9");
    layout.advertised_quest = read_offset32(quest + 3);
}

// mov rax, [rcx]; mov rax, [rax + slot]; add rsp, 0x20: a quest asking its target for its level.
static void find_level_slots() {
    constexpr std::string_view ask = "48 8B 01 48 8B 80 ?? ?? ?? ?? 48 83 C4 20";

    layout.min_level_slot = read_offset32(game::find("mmoQuest::GetQuestMinLevel").scan(ask) + 6);
    layout.max_level_slot = read_offset32(game::find("mmoQuest::GetQuestMaxLevel").scan(ask) + 6);
}

// lea rcx, [rbp + pool]; ...; call Borrow; xor edx, edx; mov [rbp + arrow], rax; ...; mov [rcx + from], rax; ...;
// mov byte [rcx + changed], 1; ...; mov rax, [rsi + position]; mov [rcx + to], rax: the map's quest display pointing
// its arrow from the quest's target to its quest giver.
static void find_arrow_fields() {
    game::Address arrow = game::find("mmoQuestDisplay::DisplayQuest").scan(
        "48 8D 8D ?? ?? ?? ?? F3 0F 11 44 24 ?? E8 ?? ?? ?? ?? 31 D2 48 89 85 ?? ?? ?? ?? 48 89 C1 E8 ?? ?? ?? ?? "
        "48 8B 8D ?? ?? ?? ?? 48 8B 44 24 ?? F3 0F 10 0D ?? ?? ?? ?? 48 89 81 ?? ?? ?? ?? 8B 44 24 ?? "
        "C6 81 ?? ?? ?? ?? 01 89 81 ?? ?? ?? ?? 48 8B 86 ?? ?? ?? ?? 48 89 81 ?? ?? ?? ??");

    layout.arrow_pool = read_offset32(arrow + 3);
    layout.arrow_from = read_offset32(arrow + 58);
    layout.arrow_changed = read_offset32(arrow + 68);
    layout.arrow_to = read_offset32(arrow + 89);
}

// mov eax, [rcx + unused]; mov rbx, rcx; cmp [rcx + count], eax: an arrow pool, closing, checking every arrow came
// back.
static void find_pool_count() {
    game::Address count = game::find("vsPool<mmoArrow>::~vsPool").scan("8B 41 ?? 48 89 CB 39 41 ??");
    layout.pool_count = read_offset8(count + 8);
}

// mov rax, [rcx + uid]; mov rsi, rcx; mov rbx, rdx: an object given a new id.
static void find_object_uid() {
    game::Address uid = game::find("mmoObject::SetUID").scan("48 8B 41 ?? 48 89 CE 48 89 D3");
    layout.object_uid = read_offset8(uid + 3);
}

void read_layout() {
    find_pane_visibility();
    find_button_tooltip();
    find_card_quest();
    find_advertisement_fields();
    find_quest_advertisement();
    find_region_npcs();
    find_demand_change_keys();
    find_action_toon();
    find_action_quest();
    find_displayed_quest();
    find_list_npc();
    find_travel_fields();
    find_advertised_quest();
    find_level_slots();
    find_arrow_fields();
    find_pool_count();
    find_object_uid();
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
