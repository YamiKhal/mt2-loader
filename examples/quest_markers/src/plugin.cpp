// Each quest giver's marker, picked in the game: the quest giver's info window gets a Marker tab (info_npc.win)
// with shapes, a color picker, the color's hex code, sliders for its size and height, and copy and paste. What each quest giver shows is kept in
// the saved game. The shapes are data: questmarkers/<shape>.vmb for a model, or materials/questmarker_<shape>.mat
// for a picture, plus a button in the tab. The game's own marker takes a picked color too. The mod manager sets
// the marker of quest givers with none picked.
#include <mt2loader.hpp>

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <set>
#include <sstream>

struct Color {
    float red;
    float green;
    float blue;
    float alpha;
};

struct Marker {
    std::string shape;
    Color color;
    // Times the game's size, and how far above the game's place, in the marker's own units (the game's is 2 tall).
    float scale = 1.0f;
    float height = 0.0f;
};

// vsMatrix4x4: the marker's right, up and forward axes (with its size in them) and where it is.
struct Matrix {
    float right[4];
    float up[4];
    float forward[4];
    float position[4];
};

// The game's own marker, which the other shapes replace, and its color. In any other color it's the same model
// drawn with this mod's material.
constexpr std::string_view game_shape = "game";
constexpr std::string_view game_color = "#FDFE0C";
constexpr std::string_view game_marker_file = "QuestGiver.vmb";
constexpr std::string_view marker_material = "questmarker";
constexpr int quest_giver = 0;
constexpr int online = 2;

static Marker default_marker;

// The sliders' ranges, as in info_npc.win.
constexpr float min_scale = 0.5f;
constexpr float max_scale = 3.0f;
constexpr float min_height = -1.0f;
constexpr float max_height = 3.0f;
static std::set<std::string, std::less<>> known_shapes{ "game", "question", "star", "diamond", "bubble" };

// Where a quest giver keeps its marker and its shard, and where a model keeps its fragments (by level of detail):
// read from the game's own code when the plugin starts.
static std::ptrdiff_t marker_offset = 0;
static std::ptrdiff_t shard_offset = 0;
static std::ptrdiff_t lods_offset = 0;
static std::ptrdiff_t lod_count_offset = 0;
static std::ptrdiff_t fragments_offset = 0;
static std::ptrdiff_t fragment_count_offset = 0;
static std::ptrdiff_t slider_value_offset = 0;

static game::Function<bool(const game::String& path)> file_exists{ "vsFile::Exists" };
static game::Function<void*(const game::String& path)> load_model{ "vsModel::Load(std::string const&)" };
static game::Function<int(void* rez, void* model, float fade_in, float fade_out)> add_model{ "mmoModelRezManager::AddModel" };
static game::Function<int(const game::String& material, float width, float height)> make_picture{ "(anonymous namespace)::_MakeTexturedBadgeModel" };
static game::Function<void*(void* rez, int model, void* scene, int group)> make_instance{ "mmoModelRezManager::MakeInstance(int, vsScene*, int)" };
static game::Function<void*(void* manager, int marker, void* shard)> make_game_marker{ "mmoCharacterModelManager::MakeMarkerInstance" };
static game::Function<void*(void* shard)> scene_of{ "mmoShard::GetScene" };
static game::Function<void(void* instance, bool visible)> set_visible{ "vsModelInstance::SetVisible" };
static game::Function<void(void* npc)> place_marker{ "mmoNPC::_PlaceMarker()" };
static game::Function<void(void* instance)> destroy_model_instance{ "vsModelInstance::~vsModelInstance()" };
static game::Function<void(void* memory)> game_delete{ "_ZdlPv" };
static game::Function<void(void* fragment, const game::String& material)> set_material{ "vsFragment::SetMaterial(std::string const&)" };

static game::Function<void*(void* view, const game::String& id)> find_pane{ "mmoView::FindPane" };
static game::Function<void(void* tabs, const game::String& page, bool enabled)> enable_page{ "mmoMultiView::EnablePage" };
static game::Function<void(void* button, bool toggled)> set_toggled{ "mmoButtonPane::SetToggled" };
static game::Function<const Color&(void* picker)> picked_color{ "mmoColorPicker::GetColor" };
static game::Function<void(void* picker, const Color& color)> set_picked_color{ "mmoColorPicker::SetColor" };
static game::Function<const game::String&(const void* field)> text_in{ "mmoTextEditPane::GetText" };
static game::Function<void(void* field, const game::String& text)> set_text_in{ "mmoTextEditPane::SetText" };
static game::Function<float(const void* slider, int value)> slider_external{ "mmoSlider::_ConvertToExternal" };
static game::Function<void(void* slider, float value)> set_slider{ "mmoSlider::SetValue_Immediate" };

static std::map<void*, void*> npc_in_window;
static thread_local void* placing = nullptr;
static bool refreshing_tab = false;


static std::optional<Color> color_from_hex(std::string_view text) {
    if (text.starts_with('#')) {
        text.remove_prefix(1);
    }

    unsigned int value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);

    if (text.size() != 6 || error != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }

    auto channel = [&](int shift) {
        return static_cast<float>((value >> shift) & 0xff) / 255.0f;
    };

    return Color{ channel(16), channel(8), channel(0), 1.0f };
}

static std::string hex_of(Color color) {
    auto byte = [](float channel) {
        return static_cast<int>(std::clamp(channel, 0.0f, 1.0f) * 255.0f + 0.5f);
    };

    return std::format("#{:02X}{:02X}{:02X}", byte(color.red), byte(color.green), byte(color.blue));
}

static bool valid_shape_name(std::string_view shape) {
    return !shape.empty() && shape.size() <= 40 && std::ranges::all_of(shape, [](char letter) {
        return (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') || letter == '_';
    });
}

static std::string model_file(std::string_view shape) {
    return std::format("questmarkers/{}.vmb", shape);
}

static std::string picture_material(std::string_view shape) {
    return std::format("questmarker_{}", shape);
}

// A shape is the game's own, or a model or a picture some mod has for it.
static bool shape_exists(std::string_view shape) {
    if (shape == game_shape) {
        return true;
    }

    return valid_shape_name(shape)
        && (file_exists(model_file(shape)) || file_exists(std::format("materials/{}.mat", picture_material(shape))));
}

static std::optional<float> number_from_text(const std::string& word) {
    float value = 0.0f;
    auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);

    if (error != std::errc() || end != word.data() + word.size()) {
        return std::nullopt;
    }

    return value;
}

// What copy writes, "star #FF8800 1.5 0.5" (shape, color, size, height), and what paste reads onto the marker it
// has. Any of them can be left out; the first number is the size, the second the height.
static std::optional<Marker> marker_from_text(std::string_view text, Marker current) {
    bool read_any = false;
    int numbers = 0;
    std::istringstream words{ std::string(text) };
    std::string word;

    while (words >> word) {
        std::optional<float> number = number_from_text(word);

        if (std::optional<Color> color = color_from_hex(word)) {
            current.color = *color;
        } else if (shape_exists(word)) {
            current.shape = word;
        } else if (number && numbers == 0) {
            current.scale = std::clamp(*number, min_scale, max_scale);
            numbers++;
        } else if (number && numbers == 1) {
            current.height = std::clamp(*number, min_height, max_height);
            numbers++;
        } else {
            return std::nullopt;
        }

        read_any = true;
    }

    if (!read_any) {
        return std::nullopt;
    }

    return current;
}


static bool is_quest_giver(void* npc) {
    return npc != nullptr && game::field<int>(npc, "mmoNPC::type") == quest_giver;
}

// The quest giver's own marker from the saved game, or the default the player set in the mod manager.
static Marker marker_of(void* npc) {
    game::Saved saved = game::saved(npc);
    Marker marker = default_marker;

    if (std::optional<std::string> shape = saved.find<std::string>("shape")) {
        marker.shape = *shape;
    }

    if (std::optional<Color> color = color_from_hex(saved.get("color", ""))) {
        marker.color = *color;
    }

    marker.scale = saved.get("scale", marker.scale);
    marker.height = saved.get("height", marker.height);

    return marker;
}

static std::string text_of(const Marker& marker) {
    return std::format("{} {} {:.2f} {:.2f}", marker.shape, hex_of(marker.color), marker.scale, marker.height);
}

// The game's own marker, exactly as the game shows it.
static bool is_games_own(const Marker& marker) {
    return marker.shape == game_shape && hex_of(marker.color) == game_color;
}

static void keep_marker(void* npc, const Marker& marker) {
    known_shapes.insert(marker.shape);

    game::Saved saved = game::saved(npc);
    saved.set("shape", marker.shape);
    saved.set("color", hex_of(marker.color));
    saved.set("scale", marker.scale);
    saved.set("height", marker.height);
}

// The game's marker model, loaded again for this mod and drawn with its material, so it takes a picked color.
static void* game_marker_to_color() {
    void* model = load_model(game_marker_file);
    int lod_count = game::field<int>(model, lod_count_offset);

    for (int lod = 0; lod < lod_count; lod++) {
        void* level = game::field<void**>(model, lods_offset)[lod];
        int fragment_count = game::field<int>(level, fragment_count_offset);

        for (int fragment = 0; fragment < fragment_count; fragment++) {
            set_material(game::field<void**>(level, fragments_offset)[fragment], marker_material);
        }
    }

    return model;
}

// The game's model for a shape, loaded the first time it's needed, the way the game loads its own marker.
static int model_for(const std::string& shape) {
    static std::map<std::string, int, std::less<>> models;
    auto found = models.find(shape);

    if (found != models.end()) {
        return found->second;
    }

    void* rez = game::singleton("mmoMetaRezManager");
    int model = 0;

    if (shape == game_shape) {
        model = add_model(rez, game_marker_to_color(), 1.0f, -1.0f);
    } else if (file_exists(model_file(shape))) {
        model = add_model(rez, load_model(model_file(shape)), 1.0f, -1.0f);
    } else {
        model = make_picture(picture_material(shape), 1.0f, 1.0f);
    }

    models[shape] = model;

    return model;
}

// As the quest giver's own destructor frees its marker.
static void destroy_instance(void* instance) {
    destroy_model_instance(instance);
    game_delete(instance);
}

// Puts the quest giver's marker model in place of the one it shows, where the game made that one.
static void show_marker(void* npc) {
    if (!is_quest_giver(npc)) {
        return;
    }

    void*& instance = game::field<void*>(npc, marker_offset);
    void* shard = game::field<void*>(npc, shard_offset);

    if (instance == nullptr || shard == nullptr) {
        return;
    }

    Marker marker = marker_of(npc);

    if (!shape_exists(marker.shape)) {
        plugin::log("A quest giver's marker is \"{}\", which no mod has now: it shows the game's own", marker.shape);
        marker.shape = game_shape;
    }

    void* made = nullptr;

    if (is_games_own(marker)) {
        made = make_game_marker(game::singleton("mmoCharacterModelManager"), 0, shard);
    } else {
        made = make_instance(game::singleton("mmoMetaRezManager"), model_for(marker.shape), scene_of(shard), 0);
    }

    if (made == nullptr) {
        return;
    }

    destroy_instance(instance);
    instance = made;
    set_visible(instance, true);
    place_marker(npc);
}

// The color the quest giver's marker is drawn in: its own, unless it shows the game's own marker as it is, or the
// game tints it (red when offline, yellow in maintenance).
static std::optional<Color> color_to_draw(void* npc) {
    if (!is_quest_giver(npc) || game::field<int>(npc, "mmoNPC::onlineState") != online) {
        return std::nullopt;
    }

    Marker marker = marker_of(npc);

    if (is_games_own(marker)) {
        return std::nullopt;
    }

    return marker.color;
}

// The tint the game gives the quest giver (the region's light, mmoRegion::GetPropTint) over a marker's color, as the
// game's tint_v.glsl puts it over its own marker's: mixed in by its alpha. So a picked color is as dim as the game's
// own marker in a dark region.
static Color in_the_light(Color color, const Color& tint) {
    auto mixed = [&](float own, float light) {
        return own + (light - own) * tint.alpha;
    };

    return Color{ mixed(color.red, tint.red), mixed(color.green, tint.green), mixed(color.blue, tint.blue), 1.0f };
}

// The game's place for the marker, raised by its height and grown by its size from the bottom up.
static Matrix sized_and_raised(const Matrix& matrix, const Marker& marker) {
    Matrix placed = matrix;

    for (int axis = 0; axis < 3; axis++) {
        placed.position[axis] += matrix.up[axis] * marker.height;
        placed.right[axis] *= marker.scale;
        placed.up[axis] *= marker.scale;
        placed.forward[axis] *= marker.scale;
    }

    return placed;
}

static void find_offsets() {
    // cmp qword ptr [rcx + marker], 0: the first thing mmoNPC::_PlaceMarker() does.
    game::Address place = game::find("mmoNPC::_PlaceMarker()");
    place.expect("48 83 B9 ?? ?? ?? ?? 00");
    marker_offset = (place + 3).read<std::int32_t>();

    // mov r8, [rbx + shard]; xor edx, edx; mov rcx, rax; call: mmoNPC::Generate asking for the quest giver's marker.
    game::Address ask = game::find("mmoNPC::Generate").scan("4C 8B 83 ?? ?? ?? ?? 31 D2 48 89 C1 E8");
    shard_offset = (ask + 3).read<std::int32_t>();

    // cmp edx, [rcx + lod count], and later mov rax, [rbx + lods]; movsxd rdx, edx; mov rax, [rax + rdx*8];
    // mov eax, [rax + fragment count]: how many fragments a level of detail has.
    game::Address count = game::find("vsModel::GetLodFragmentCount");
    lod_count_offset = (count.scan("3B 91 ?? ?? ?? ??") + 2).read<std::int32_t>();
    game::Address level = count.scan("48 8B 83 ?? ?? ?? ?? 48 63 D2 48 8B 04 D0 8B 40 ??");
    lods_offset = (level + 3).read<std::int32_t>();
    fragment_count_offset = (level + 16).read<std::int8_t>();

    // mov ebx, [r13 + fragment count]; test ebx, ebx; jle; mov rdx, [r13 + fragments]: looking through a level's fragments.
    game::Address fragments = game::find("vsModel::AddLodFragment(int, vsFragment*) [clone .part.0]")
        .scan("41 8B 5D ?? 85 DB 0F 8E ?? ?? ?? ?? 49 8B 55 ??");
    fragments_offset = (fragments + 15).read<std::int8_t>();

    // mov [rbx + value], eax: where a slider keeps its value, as a step count that _ConvertToExternal turns into a number.
    game::Address slider_store = game::find("mmoSlider::SetValue_Immediate").scan("89 83 ?? ?? ?? ??");
    slider_value_offset = (slider_store + 2).read<std::int32_t>();
}

static void replace_markers() {
    game::in("mmoNPC::Generate").after([](void* npc, const void*, bool) {
        if (!is_games_own(marker_of(npc))) {
            show_marker(npc);
        }
    });

    // Once a saved game has loaded, the quest giver's own marker is known.
    game::in("mmoNPC::PostResolve").after([](void* npc, void*) {
        if (!is_games_own(marker_of(npc))) {
            show_marker(npc);
        }
    });

    game::in("mmoNPC::_PlaceMarker() [clone .part.0]").hook<void(void* npc)>([](auto original, void* npc) {
        placing = npc;
        original(npc);
        placing = nullptr;
    });

    game::in("mmoNPC::_PlaceMarker() [clone .part.0]").call("vsModelInstance::SetMatrix")
        .hook<void(void* instance, const Matrix* matrix, const Color* tint)>([](auto set_matrix, void* instance, const Matrix* matrix, const Color* tint) {
            if (placing == nullptr || !is_quest_giver(placing)) {
                set_matrix(instance, matrix, tint);

                return;
            }

            Marker marker = marker_of(placing);
            Matrix placed = sized_and_raised(*matrix, marker);
            std::optional<Color> own = color_to_draw(placing);
            Color lit = own ? in_the_light(*own, *tint) : *tint;

            set_matrix(instance, &placed, &lit);
        });
}


static void* pane(void* window, std::string_view id) {
    return find_pane(window, std::string(id));
}

static float slider_value(void* slider) {
    return slider_external(slider, game::field<int>(slider, slider_value_offset));
}

// Shows the quest giver's marker in the tab: its shape's button pressed, its color in the picker and as hex, its
// size and height on the sliders. While the player drags the picker or a slider, that's left alone.
static void refresh_tab(void* window, bool picking = false, bool sliding = false) {
    void* npc = npc_in_window[window];

    if (!is_quest_giver(npc)) {
        return;
    }

    Marker marker = marker_of(npc);
    refreshing_tab = true;

    for (const std::string& shape : known_shapes) {
        if (void* button = pane(window, "questmarker_shape_" + shape)) {
            set_toggled(button, shape == marker.shape);
        }
    }

    if (void* picker = pane(window, "questmarker_color"); picker != nullptr && !picking) {
        set_picked_color(picker, marker.color);
    }

    if (void* field = pane(window, "questmarker_hex")) {
        set_text_in(field, hex_of(marker.color));
    }

    if (void* slider = pane(window, "questmarker_scale"); slider != nullptr && !sliding) {
        set_slider(slider, marker.scale);
    }

    if (void* slider = pane(window, "questmarker_height"); slider != nullptr && !sliding) {
        set_slider(slider, marker.height);
    }

    refreshing_tab = false;
}

static void copy_to_clipboard(const std::string& text) {
    if (!OpenClipboard(nullptr)) {
        return;
    }

    EmptyClipboard();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);

    if (memory != nullptr) {
        std::memcpy(GlobalLock(memory), text.c_str(), text.size() + 1);
        GlobalUnlock(memory);
        SetClipboardData(CF_TEXT, memory);
    }

    CloseClipboard();
}

static std::string clipboard_text() {
    std::string text;

    if (!OpenClipboard(nullptr)) {
        return text;
    }

    if (HANDLE memory = GetClipboardData(CF_TEXT)) {
        if (const char* locked = static_cast<const char*>(GlobalLock(memory))) {
            text = locked;
            GlobalUnlock(memory);
        }
    }

    CloseClipboard();

    return text;
}

// A command from the tab's buttons, picker and hex field: "QuestMarker shape star", "QuestMarker color", ...
static void handle_command(void* window, const game::Record& command) {
    void* npc = npc_in_window[window];

    if (!is_quest_giver(npc)) {
        return;
    }

    std::string action = command.text(0);
    Marker marker = marker_of(npc);
    Marker changed = marker;

    if (action == "shape" && shape_exists(command.text(1))) {
        changed.shape = command.text(1);
    } else if (action == "color") {
        changed.color = picked_color(pane(window, "questmarker_color"));
    } else if (action == "scale") {
        changed.scale = std::clamp(slider_value(pane(window, "questmarker_scale")), min_scale, max_scale);
    } else if (action == "height") {
        changed.height = std::clamp(slider_value(pane(window, "questmarker_height")), min_height, max_height);
    } else if (action == "hex") {
        changed.color = color_from_hex(text_in(pane(window, "questmarker_hex")).view()).value_or(marker.color);
    } else if (action == "copy") {
        copy_to_clipboard(text_of(marker));
    } else if (action == "paste") {
        changed = marker_from_text(clipboard_text(), marker).value_or(marker);
    } else if (action == "reset") {
        game::saved(npc).erase("shape");
        game::saved(npc).erase("color");
        game::saved(npc).erase("scale");
        game::saved(npc).erase("height");
        changed = marker_of(npc);
    }

    if (action != "copy" && action != "reset") {
        keep_marker(npc, changed);
    }

    if (changed.shape != marker.shape || is_games_own(changed) != is_games_own(marker) || action == "reset") {
        show_marker(npc);
    }

    refresh_tab(window, action == "color", action == "scale" || action == "height");
}

static void add_the_tab() {
    // A quest giver destroyed while its window is open, and the last game's windows.
    game::in("_ZN6mmoNPCD1Ev").before([](void* npc) {
        for (auto& [window, shown] : npc_in_window) {
            if (shown == npc) {
                shown = nullptr;
            }
        }
    });

    game::in("mmoModeInGame::DoInit").before([](void*) {
        npc_in_window.clear();
    });

    game::in("mmoNPCInfoWindow::SetNPC").after([](void* window, void* npc) {
        npc_in_window[window] = npc;
        refresh_tab(window);
    });

    // Where the window shows the Quests tab (for quest givers only), it shows this one too.
    game::in("mmoNPCInfoWindow::InitContents").call("mmoMultiView::EnablePage").every()
        .after([](void* tabs, const game::String& page, bool enabled) {
            if (page == "{npcinfo_tab_quests}") {
                enable_page(tabs, "{questmarkers_tab}", enabled);
            }
        });

    game::in("mmoNPCInfoWindow::UICommand").after([](bool handled, void* window, game::Record command) {
        if (command.label() != "QuestMarker") {
            return handled;
        }

        if (!refreshing_tab) {
            handle_command(window, command);
        }

        return true;
    });
}

void plugin::init() {
    default_marker.shape = plugin::setting<std::string>("default_shape");
    default_marker.color = color_from_hex(plugin::setting<std::string>("default_color")).value_or(Color{ 1.0f, 1.0f, 1.0f, 1.0f });
    default_marker.scale = std::clamp(static_cast<float>(plugin::setting<double>("default_scale")), min_scale, max_scale);
    default_marker.height = std::clamp(static_cast<float>(plugin::setting<double>("default_height")), min_height, max_height);

    find_offsets();
    replace_markers();
    add_the_tab();

    plugin::log("Quest givers get a Marker tab (default marker: {}; marker at +{:#x}, shard at +{:#x}; model levels at +{:#x} +{:#x}, fragments at +{:#x} +{:#x}; slider value at +{:#x})",
        text_of(default_marker), marker_offset, shard_offset, lods_offset, lod_count_offset, fragments_offset, fragment_count_offset, slider_value_offset);
}
