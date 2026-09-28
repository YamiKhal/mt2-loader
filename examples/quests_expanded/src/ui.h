#pragma once

#include <mt2loader.hpp>

#include <string>
#include <string_view>
#include <vector>

// The game's panes, as the mod's windows use them. Texts may hold translation keys: "{quests_expanded_from} Alda".
namespace ui {

void* find_pane(void* view, std::string_view id);
// A window from a window file, shown and raised; nullptr if no file has it.
void* open_window(std::string_view id);
void close_window(void* window);
void set_window_title(void* window, std::string_view title);
void set_visible(void* pane, bool visible);
void set_text(void* text_pane, std::string_view text);
void set_text(void* text_pane, std::string_view text, const game::Color& color);
// A label and value pair (mmoPropertyPane): its value.
void set_value(void* property_pane, std::string_view text);

void set_icon(void* button, std::string_view material);
void set_tooltip(void* button, std::string_view title, std::string_view details = {});
void set_toggled(void* button, bool toggled);

// A text edit (mmoTextEditPane) shows its text as it is, without translating keys.
std::string edit_text(void* edit);
void set_edit_text(void* edit, std::string_view text);
void set_editable(void* edit, bool editable);

void set_checked(void* checkbox, bool checked);

// A dropdown (mmoComboBox): its choices, as the player reads them, and the one picked.
void set_choices(void* dropdown, const std::vector<std::string>& choices);
void select_choice(void* dropdown, std::string_view choice);
std::string selected_choice(const void* dropdown);

// A text grid (mmoGridTextView): its cells by column and row; a clicked row adds its id to the grid's command.
void set_grid_size(void* grid, int columns, int rows);
void set_cell(void* grid, int column, int row, std::string_view text);
void set_cell(void* grid, int column, int row, std::string_view text, const game::Color& color);
void set_row_id(void* grid, int row, int id);
void select_row_id(void* grid, int id);

// The text with its translation keys filled in, as the player reads it.
std::string translated(std::string_view text);

}
