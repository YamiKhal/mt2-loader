#include "quests_report.h"

#include "chapter_buzz.h"
#include "chapter_quality.h"
#include "layout.h"
#include "line_stats.h"
#include "line_titles.h"
#include "quest.h"
#include "quest_giver.h"
#include "questlines.h"
#include "ui.h"

#include <mt2loader.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>

constexpr float seconds_between_refreshes = 5.0f;
// A table's header row: clicking it selects nothing.
constexpr int header_row_id = -1;

// The game's own colors: its values (orange), and the good and bad of its reports.
constexpr game::Color value_color{ 0.756863f, 0.466667f, 0.086275f, 1.0f };
constexpr game::Color good_color{ 0.45f, 0.8f, 0.35f, 1.0f };
constexpr game::Color bad_color{ 0.9f, 0.3f, 0.25f, 1.0f };
constexpr game::Color plain_color{};
constexpr game::Color header_color{ 0.65f, 0.65f, 0.65f, 1.0f };

// The report window once it has been shown; windows stay loaded for the whole session.
static void* report = nullptr;
// The line shown, by its first quest giver, as tracing again makes new lines.
static const void* selected_start = nullptr;
// Each listed row's line, by its first quest giver: a row's id is its place here.
static std::vector<const void*> listed_starts;
// The chapter whose quests are listed, in the line shown.
static std::size_t selected_chapter = 0;
// What the name edits last showed, so a refresh doesn't undo what the designer is typing.
static std::string shown_line_name;
static std::string shown_chapter_name;
static float since_refresh = 0.0f;


static std::string percent(std::optional<int> rate) {
    return rate ? std::format("{}%", *rate) : "-";
}

static std::string rating_of(const Quality& quality) {
    if (quality.broken) {
        return "{quests_expanded_rating_broken}";
    }

    switch (quality.rating) {
    case Rating::great:
        return "{quests_expanded_rating_great}";
    case Rating::fine:
        return "{quests_expanded_rating_fine}";
    default:
        return "{quests_expanded_rating_poor}";
    }
}

static game::Color rating_color(const Quality& quality) {
    if (quality.broken || quality.rating == Rating::poor) {
        return bad_color;
    }

    return quality.rating == Rating::great ? good_color : plain_color;
}

static std::string kind_of(const Questline& line) {
    std::string kind = line.settings.main ? "{quests_expanded_kind_main}" : "{quests_expanded_kind_side}";

    return line.settings.repeatable ? kind + " {quests_expanded_kind_repeatable}" : kind;
}

// Main lines first, then the ones most played.
static std::vector<std::size_t> line_order(const std::vector<Questline>& lines, const std::vector<LineStats>& stats) {
    std::vector<std::size_t> order(lines.size());

    for (std::size_t index = 0; index < order.size(); index++) {
        order[index] = index;
    }

    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (lines[a].settings.main != lines[b].settings.main) {
            return lines[a].settings.main;
        }

        return stats[a].started > stats[b].started;
    });

    return order;
}

static const Questline* selected_line() {
    const std::vector<Questline>& lines = questlines::all();
    auto found = std::find_if(lines.begin(), lines.end(), [](const Questline& line) { return line.start == selected_start; });

    return found != lines.end() ? &*found : nullptr;
}

static void set_value(void* window, const char* id, std::string_view text) {
    if (void* pane = ui::find_pane(window, id)) {
        ui::set_value(pane, text);
    }
}

static void fill_lines(void* window) {
    void* grid = ui::find_pane(window, "quests_expanded_lines");

    if (grid == nullptr) {
        return;
    }

    const std::vector<Questline>& lines = questlines::all();
    listed_starts.clear();

    if (lines.empty()) {
        ui::set_grid_size(grid, 1, 1);
        ui::set_cell(grid, 0, 0, "{quests_expanded_no_lines}");
        selected_start = nullptr;

        return;
    }

    std::vector<LineStats> stats;

    for (const Questline& line : lines) {
        stats.push_back(line_stats::of(line));
    }

    std::vector<std::size_t> order = line_order(lines, stats);
    ui::set_grid_size(grid, 1, static_cast<int>(lines.size()));

    for (std::size_t row = 0; row < order.size(); row++) {
        const Questline& line = lines[order[row]];
        int grid_row = static_cast<int>(row);

        ui::set_cell(grid, 0, grid_row, line_titles::line_title(line), line.settings.main ? value_color : plain_color);
        ui::set_row_id(grid, grid_row, grid_row);
        listed_starts.push_back(line.start);
    }

    if (selected_line() == nullptr) {
        selected_start = lines[order.front()].start;
    }

    auto selected = std::find(listed_starts.begin(), listed_starts.end(), selected_start);
    ui::select_row_id(grid, static_cast<int>(selected - listed_starts.begin()));
}

// Rounded, over every chapter of the line.
static int average_score(const Questline& line) {
    if (line.chapters.empty()) {
        return 0;
    }

    int total = 0;

    for (std::size_t chapter = 0; chapter < line.chapters.size(); chapter++) {
        total += chapter_quality::of(line, chapter).score;
    }

    int count = static_cast<int>(line.chapters.size());

    return (total + count / 2) / count;
}

static void fill_line(void* window) {
    const Questline* line = selected_line();
    void* name = ui::find_pane(window, "quests_expanded_line_name");

    if (name != nullptr) {
        std::string title = line != nullptr ? line_titles::line_title(*line) : "";
        ui::set_editable(name, line != nullptr);

        if (title != shown_line_name) {
            ui::set_edit_text(name, title);
            shown_line_name = title;
        }
    }

    if (line == nullptr) {
        for (const char* id : { "quests_expanded_kind", "quests_expanded_chapter_count", "quests_expanded_quest_count", "quests_expanded_average_score",
                 "quests_expanded_in_progress", "quests_expanded_started", "quests_expanded_finished", "quests_expanded_finish_rate" }) {
            set_value(window, id, "-");
        }

        return;
    }

    LineStats stats = line_stats::of(*line);

    set_value(window, "quests_expanded_kind", kind_of(*line));
    set_value(window, "quests_expanded_chapter_count", std::to_string(line->chapters.size()));
    set_value(window, "quests_expanded_quest_count", std::to_string(line->quests.size()));
    set_value(window, "quests_expanded_average_score", std::to_string(average_score(*line)));
    set_value(window, "quests_expanded_in_progress", std::to_string(stats.on_it));
    set_value(window, "quests_expanded_started", std::to_string(stats.started));
    set_value(window, "quests_expanded_finished", std::to_string(stats.finished));
    set_value(window, "quests_expanded_finish_rate", percent(line_stats::finish_rate(stats.started, stats.finished)));
}

static void fill_headers(void* grid, const std::vector<const char*>& headers) {
    for (std::size_t column = 0; column < headers.size(); column++) {
        ui::set_cell(grid, static_cast<int>(column), 0, headers[column], header_color);
    }

    ui::set_row_id(grid, 0, header_row_id);
}

// A row per chapter under the headers, numbered ("Chapter 2"): its name shows below once it's picked.
static void fill_chapters(void* window) {
    void* grid = ui::find_pane(window, "quests_expanded_chapters");
    const Questline* line = selected_line();
    std::size_t chapters = line != nullptr ? line->chapters.size() : 0;

    if (grid == nullptr) {
        return;
    }

    ui::set_grid_size(grid, 4, static_cast<int>(chapters) + 1);
    fill_headers(grid, { "{quests_expanded_column_chapter}", "{quests_expanded_column_rating}", "{quests_expanded_column_score}",
        "{quests_expanded_column_quests}" });

    for (std::size_t chapter = 0; chapter < chapters; chapter++) {
        Quality quality = chapter_quality::of(*line, chapter);
        std::size_t quests = chapter_quality::end_quest(*line, chapter) - chapter_quality::first_quest(*line, chapter);
        int row = static_cast<int>(chapter) + 1;

        ui::set_cell(grid, 0, row, line_titles::default_chapter_title(chapter));
        ui::set_cell(grid, 1, row, rating_of(quality), rating_color(quality));
        ui::set_cell(grid, 2, row, std::to_string(quality.score));
        ui::set_cell(grid, 3, row, std::to_string(quests));
        ui::set_row_id(grid, row, static_cast<int>(chapter));
    }

    ui::select_row_id(grid, chapters > 0 ? static_cast<int>(selected_chapter) : header_row_id);
}

// The "!" beside the chosen chapter's name: what players say about it.
static void fill_buzz(void* window) {
    void* button = ui::find_pane(window, "quests_expanded_buzz");
    const Questline* line = selected_line();

    if (button == nullptr) {
        return;
    }

    ui::set_visible(button, line != nullptr);

    if (line != nullptr) {
        ui::set_tooltip(button, chapter_buzz::of(chapter_quality::of(*line, selected_chapter)));
    }
}

static void fill_chapter_name(void* window) {
    void* edit = ui::find_pane(window, "quests_expanded_chapter_name");
    const Questline* line = selected_line();

    if (edit == nullptr) {
        return;
    }

    std::string name = line != nullptr ? line_titles::chapter_title(*line, selected_chapter) : "";
    ui::set_editable(edit, line != nullptr);

    if (name != shown_chapter_name) {
        ui::set_edit_text(edit, name);
        shown_chapter_name = name;
    }
}

static void fill_quest_headers(void* grid) {
    fill_headers(grid, { "{quests_expanded_column_number}", "{quests_expanded_column_quest}", "{quests_expanded_column_giver}",
        "{quests_expanded_column_done}", "{quests_expanded_column_rate}" });
}

// The selected chapter's quests, under a header row. Their numbers count through the whole line.
static void fill_quests(void* window) {
    void* grid = ui::find_pane(window, "quests_expanded_quests");
    const Questline* line = selected_line();

    if (grid == nullptr) {
        return;
    }

    if (line == nullptr) {
        ui::set_grid_size(grid, 5, 1);
        fill_quest_headers(grid);

        return;
    }

    LineStats stats = line_stats::of(*line);
    std::size_t first = chapter_quality::first_quest(*line, selected_chapter);
    std::size_t end = std::min(chapter_quality::end_quest(*line, selected_chapter), stats.quests.size());
    ui::set_grid_size(grid, 5, static_cast<int>(end - first) + 1);
    fill_quest_headers(grid);

    for (std::size_t index = first; index < end; index++) {
        const QuestStats& quest_stats = stats.quests[index];
        int row = static_cast<int>(index - first) + 1;

        ui::set_cell(grid, 0, row, std::to_string(index + 1));
        ui::set_cell(grid, 1, row, quest::name(quest_stats.quest));
        ui::set_cell(grid, 2, row, quest_giver::name(quest::giver(quest_stats.quest)));
        ui::set_cell(grid, 3, row, std::format("{}/{}", quest_stats.completed, quest_stats.accepted));
        ui::set_cell(grid, 4, row, percent(line_stats::finish_rate(quest_stats.accepted, quest_stats.completed)));
    }
}

static void fill_selected(void* window) {
    const Questline* line = selected_line();

    if (line == nullptr || selected_chapter >= line->chapters.size()) {
        selected_chapter = 0;
    }

    fill_line(window);
    fill_chapters(window);
    fill_chapter_name(window);
    fill_buzz(window);
    fill_quests(window);
}

static void refresh(void* window) {
    questlines::update();
    fill_lines(window);
    fill_selected(window);
    since_refresh = 0.0f;
}

static void select_line(void* window, int row) {
    if (row < 0 || static_cast<std::size_t>(row) >= listed_starts.size()) {
        return;
    }

    selected_start = listed_starts[static_cast<std::size_t>(row)];
    selected_chapter = 0;
    fill_selected(window);
}

// The header row keeps the chapter picked before.
static void select_chapter(void* window, int chapter) {
    if (chapter != header_row_id) {
        selected_chapter = static_cast<std::size_t>(chapter);
    }

    fill_selected(window);
}

static void rename_line(void* window) {
    void* edit = ui::find_pane(window, "quests_expanded_line_name");
    const Questline* line = selected_line();

    if (edit != nullptr && line != nullptr) {
        line_titles::rename_line(*line, ui::edit_text(edit));
    }

    shown_line_name.clear();
    refresh(window);
}

static void rename_chapter(void* window) {
    void* edit = ui::find_pane(window, "quests_expanded_chapter_name");
    const Questline* line = selected_line();

    if (edit != nullptr && line != nullptr && selected_chapter < line->chapters.size()) {
        line_titles::rename_chapter(*line, selected_chapter, ui::edit_text(edit));
    }

    shown_chapter_name.clear();
    fill_selected(window);
}


namespace quests_report {

void install() {
    // Not mmoWindow::OnShow: Show calls that one's work inline for a plain window, so a hook on it never runs.
    game::in("mmoWindow::Show(bool, bool) [clone .part.0]").after([](void* window, bool show, bool) {
        if (!show) {
            return;
        }

        if (report == nullptr && ui::find_pane(window, "quests_expanded_lines") != nullptr) {
            report = window;
        }

        if (window == report) {
            refresh(window);
        }
    });

    game::in("mmoWindow::UpdateUI").after([](void* window, float seconds) {
        if (window != report || !game::field<bool>(window, static_cast<std::size_t>(layout.pane_visible))) {
            return;
        }

        since_refresh += seconds;

        if (since_refresh >= seconds_between_refreshes) {
            refresh(window);
        }
    });

    // "QuestsExpanded select_line 3", "QuestsExpanded select_chapter 1" (the grids add the clicked row's id),
    // "QuestsExpanded rename_line" or "QuestsExpanded rename_chapter" (a name edited).
    game::in("mmoWindow::UICommand").after([](bool handled, void* window, game::Record command) {
        if (window != report || command.label() != "QuestsExpanded") {
            return handled;
        }

        std::optional<int> number = command.number(1);

        if (command.text(0) == "select_line" && number) {
            select_line(window, *number);
        } else if (command.text(0) == "select_chapter" && number) {
            select_chapter(window, *number);
        } else if (command.text(0) == "rename_line") {
            rename_line(window);
        } else if (command.text(0) == "rename_chapter") {
            rename_chapter(window);
        }

        return true;
    });
}

}
