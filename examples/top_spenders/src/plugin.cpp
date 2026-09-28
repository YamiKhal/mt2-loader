// A Spenders tab in the Leaderboards window: the players who have spent the most real money on the game, drawn by
// the game's own leaderboard code. Design > Features has a checkbox for it. The tab and the checkbox are in the mod's
// data files (report_leaderboards.win, design_features.win), what counts is set in the mod manager (config.json);
// this plugin makes them work.
#include <mt2loader.hpp>

#include <chrono>

// What the tab's `type "spenders"` reads as: a leaderboard type the game doesn't have.
constexpr int top_spenders = 100;

// The player's choice in Design > Features, kept between sessions.
static plugin::Remembered<bool> show_top_spenders{"show_top_spenders", true};

// From config.json, as the player set them in the mod manager.
static std::string counted;
static int minimum_spend = 1;

static game::Function<void(void* board, void* toon, int value)> register_value{"mmoLeaderboard::RegisterValue"};
static game::Function<void(void* tabs, const game::String& page, bool enabled)> enable_page{"mmoMultiTabView::EnablePage"};
static game::Function<void*(void* view, const game::String& id)> find_pane{"mmoView::FindPane"};
static game::Function<void(void* checkbox, bool enabled)> set_enabled{"mmoCheckbox::SetEnabled"};
static game::Function<void(void* checkbox, bool value)> set_value{"mmoCheckbox::SetValue"};
static game::Function<bool(void* features, int flag)> is_flag_enabled{"mmoGameFeatures::IsEnabled(mmoGameFeatures::Flag)"};
// It returns a vsLocString, which the game builds where its first parameter points.
static game::Function<void*(void* text, int value)> format_money{"vsFormatMoney"};

static void* spenders_board = nullptr;
static std::chrono::steady_clock::time_point spenders_counted_at;
static bool drawing_spenders = false;


// Whether the Leaderboards technology is researched in the game being played.
static bool leaderboards_researched() {
    static const int leaderboards = game::enumeration("mmoGameFeatures::Flag").value("Leaderboards");
    void* features = game::singleton("mmoGameFeatures");

    return features != nullptr && is_flag_enabled(features, leaderboards);
}

// What a subscriber has spent, as the "counted" setting says: subscription fees, other purchases, or both (the
// player info window's "Lifetime spend").
static int spent_by(void* subscriber) {
    int subscriptions = game::field<int>(subscriber, "mmoSubscriber::spend_subscriptions");
    int purchases = game::field<int>(subscriber, "mmoSubscriber::spend_other");

    if (counted == "subscriptions") {
        return subscriptions;
    }

    if (counted == "purchases") {
        return purchases;
    }

    return subscriptions + purchases;
}

// A leaderboard of the game's own kind, filled with what each subscriber has spent. Counted again at most once a
// second while it's shown.
static void* top_spenders_board() {
    auto now = std::chrono::steady_clock::now();

    if (spenders_board != nullptr && now - spenders_counted_at < std::chrono::seconds(1)) {
        return spenders_board;
    }

    game::destroy(spenders_board);
    spenders_board = game::create("mmoLeaderboard");
    spenders_counted_at = now;

    void* subscribers = game::singleton("mmoSubscriberManager");

    if (subscribers == nullptr) {
        return spenders_board;
    }

    for (void* subscriber : game::field<game::Objects>(subscribers, "mmoSubscriberManager::subscriber")) {
        void* toon = game::field<void*>(subscriber, "mmoSubscriber::main");
        int spent = spent_by(subscriber);

        if (toon != nullptr && spent >= minimum_spend) {
            register_value(spenders_board, toon, spent);
        }
    }

    return spenders_board;
}

static void add_the_tab() {
    game::enumeration("mmoLeaderboard::Type").add("spenders", top_spenders);

    game::in("mmoLeaderboardSet::GetLeaderboard").after([](void* board, void*, int type) {
        return type == top_spenders ? top_spenders_board() : board;
    });

    // The tab's numbers are money: formatted as the game formats money, in the currency the player picked.
    game::in("mmoLeaderboardView::UpdateUI").hook<void(void* view, float seconds)>([](auto original, void* view, float seconds) {
        drawing_spenders = game::field<int>(view, "mmoLeaderboardView::type") == top_spenders;
        original(view, seconds);
        drawing_spenders = false;
    });

    game::in("mmoLeaderboardView::UpdateUI").call("vsFormatNumber").hook<void*(void* text, int value)>([](auto format_number, void* text, int value) {
        return drawing_spenders ? format_money(text, value) : format_number(text, value);
    });

    // Where the window decides whether to show the Duels tab, it decides about this one too.
    game::in("mmoLeaderboardsWindow::OnShow").call("mmoMultiTabView::EnablePage").after([](void* tabs, const game::String&, bool) {
        enable_page(tabs, "{top_spenders_tab}", show_top_spenders);
    });
}

static void add_the_checkbox() {
    game::in("mmoFeaturesView::UpdateContents").after([](void* view) {
        void* checkbox = find_pane(view, "top_spenders_enabled");

        if (checkbox == nullptr) {
            return;
        }

        bool researched = leaderboards_researched();

        set_enabled(checkbox, researched);
        set_value(checkbox, researched && show_top_spenders);
    });

    game::in("mmoFeaturesView::UICommand").after([](bool handled, void* view, void*) {
        void* checkbox = find_pane(view, "top_spenders_enabled");

        if (checkbox != nullptr && leaderboards_researched()) {
            show_top_spenders = game::field<bool>(checkbox, "mmoCheckbox::value");
        }

        return handled;
    });
}

void plugin::init() {
    counted = plugin::setting<std::string>("counted");
    minimum_spend = plugin::setting<int>("minimum_spend");

    add_the_tab();
    add_the_checkbox();

    plugin::log("The Leaderboards window can show the biggest spenders (counting: {}, from {})", counted, minimum_spend);
}
