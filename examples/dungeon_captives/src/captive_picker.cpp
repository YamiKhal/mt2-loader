#include "captive_picker.h"

#include "captive_variants.h"
#include "character_types.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

constexpr const char* window_name = "character_theme";
// The list every placing tool fills (model_window.win), which the window would cover.
constexpr const char* list_name = "model_window";
constexpr const char* window_title = "{dungeon_captives_palette_title}";
// What the window's edit button does, as for quest givers.
constexpr const char* edit_command = "MessageTo WindowName \"collection_npcs\" Show";

struct Point {
    float x;
    float y;
};

// As the engine lays out a vsArray: its class, the items, how many, and room for how many.
template<class Item>
struct GameArray {
    const void* array_class;
    const Item* items;
    std::int32_t count;
    std::int32_t capacity;
};

static game::Function<bool(const void* tool, const game::String& type)> tool_lists{ "mmoCursorBehaviourGizmo::_ShouldDisplayType" };
static game::Function<void(void* tool)> activate{ "mmoCursorBehaviourGizmo::Activate" };
static game::Function<void(void* tool)> deactivate{ "mmoCursorBehaviourGizmo::Deactivate" };
static game::Function<void*(const game::String& name)> window_named{ "mmoWindow::Find" };
static game::Function<void(void* window, bool shown, bool instantly)> show{ "_ZN9mmoWindow4ShowEbb" };
static game::Function<void(void* window, const game::String& title)> set_title{ "mmoCharacterThemeWindow::SetTitle" };
static game::Function<void(void* window, const void* collections, const void* kinds, const game::String& edit)> set_types{ "mmoCharacterThemeWindow::SetTypes" };
static game::Function<void*(void* window)> selection_in{ "mmoCharacterThemeWindow::GetSelection" };
static game::Function<void(void* window, void* type)> select{ "mmoCharacterThemeWindow::SetSelection" };
static game::Function<void(void* window)> reposition{ "mmoWindow::RepositionBasedUponAnchor" };

// The character whose poses the tool lists, and the window's pick last looked at; the tool and the window live on the
// main thread.
static void* picked = nullptr;
static void* seen = nullptr;
// While the tool refills its list it's put away and brought back, and the window stays open.
static bool refilling = false;
// Where the list stands for the other tools, while it's moved beside the window.
static std::optional<Point> list_home;


static bool is_captive_tool(const void* tool) {
    return tool_lists(tool, game::String(std::string(captive_variants::type_name)));
}

static void* character_window() {
    return window_named(game::String(window_name));
}

static Point& spot_of(void* window) {
    return game::field<Point>(window, "mmoPane::anchorPos");
}

// Both windows stand in the same spot (character_theme.win, model_window.win): the list moves to the window's right,
// as far from it as the window is from the screen's edge.
static void move_list_beside(void* window) {
    void* list = window_named(game::String(list_name));

    if (list == nullptr || list_home) {
        return;
    }

    Point& spot = spot_of(list);
    list_home = spot;
    spot.x = spot_of(window).x * 2 + game::field<Point>(window, "mmoPane::dims").x;
    reposition(list);
}

static void move_list_back() {
    void* list = window_named(game::String(list_name));

    if (list == nullptr || !list_home) {
        return;
    }

    spot_of(list) = *list_home;
    list_home.reset();
    reposition(list);
}

static const void* array_class(const char* array) {
    return (game::find(std::format("vtable for {}", array)) + static_cast<std::ptrdiff_t>(2 * sizeof(void*))).get();
}

// The window's pick if it's one of the MMO's characters, else the last one, else the first.
static character_types::Named pick(void* window) {
    std::vector<character_types::Named> characters = character_types::all();
    void* selected = window != nullptr ? selection_in(window) : nullptr;

    for (void* wanted : { selected, picked }) {
        auto found = std::ranges::find(characters, wanted, &character_types::Named::type);

        if (wanted != nullptr && found != characters.end()) {
            return *found;
        }
    }

    return characters.empty() ? character_types::Named{} : characters.front();
}

// As the quest giver tool opens it (mmoCursorBehaviourNPC::Activate).
static void open(void* window) {
    const void* characters = character_types::design_characters();

    if (characters == nullptr) {
        return;
    }

    std::array<const void*, 1> collections{ characters };
    std::array<std::int32_t, character_types::kinds.size()> kinds{};
    std::ranges::transform(character_types::kinds, kinds.begin(), &character_types::Kind::number);

    GameArray<const void*> collection_list{ array_class("vsArray<mmoCharacterTypeCollection*>"), collections.data(), 1, 1 };
    GameArray<std::int32_t> kind_list{ array_class("vsArray<CharacterType>"), kinds.data(),
        static_cast<std::int32_t>(kinds.size()), static_cast<std::int32_t>(kinds.size()) };

    set_title(window, game::String(window_title));
    set_types(window, &collection_list, &kind_list, game::String(edit_command));
    select(window, picked);
    show(window, true, false);
}


namespace captive_picker {

void install() {
    game::in("mmoCursorBehaviourGizmo::Activate").before([](void* tool) {
        if (is_captive_tool(tool)) {
            character_types::Named character = pick(character_window());
            picked = character.type;
            captive_variants::show_poses(character.name);
        }
    });

    game::in("mmoCursorBehaviourGizmo::Activate").after([](void* tool) {
        if (!is_captive_tool(tool)) {
            return;
        }

        captive_variants::show_all();
        void* window = character_window();

        if (window != nullptr && !refilling) {
            open(window);
            move_list_beside(window);
        }
    });

    game::in("mmoCursorBehaviourGizmo::Update").after([](void* tool, float) {
        void* window = !refilling && is_captive_tool(tool) ? character_window() : nullptr;
        void* selected = window != nullptr ? selection_in(window) : nullptr;

        if (selected == seen) {
            return;
        }

        seen = selected;

        if (selected == nullptr || pick(window).type == picked) {
            return;
        }

        refilling = true;
        deactivate(tool);
        activate(tool);
        refilling = false;
    });

    game::in("mmoCursorBehaviourGizmo::Deactivate").before([](void* tool) {
        void* window = !refilling && is_captive_tool(tool) ? character_window() : nullptr;

        if (window != nullptr) {
            show(window, false, false);
        }
    });

    // Once the tool has put its list away.
    game::in("mmoCursorBehaviourGizmo::Deactivate").after([](void* tool) {
        if (!refilling && is_captive_tool(tool)) {
            move_list_back();
        }
    });
}

}
