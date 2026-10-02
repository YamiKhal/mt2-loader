#include "ui.h"

#include <mt2loader.hpp>

static game::Function<void*(void* view, const game::String& id)> find_pane_by_id{ "mmoView::FindPane" };
static game::Function<void*(const game::String& id)> find_window{ "mmoWindowRegistry::Find" };
static game::Function<void(void* window, bool show, bool immediately)> show_window{ "mmoWindow::Show(bool, bool)" };
static game::Function<void(void* window, const game::LocalizedText& title)> set_title{ "mmoWindow::SetTitle" };
static game::Function<void(void* checkbox, bool checked)> set_checkbox_value{ "mmoCheckbox::SetValue" };
static game::Function<void(void* dropdown, const game::Array<game::String>& choices)> set_dropdown_values{ "mmoComboBox::SetValues(vsArray<std::string >&)" };
static game::Function<void(void* dropdown, const game::String& choice, bool)> set_dropdown_selection{ "mmoComboBox::SetSelection" };
// A reference to its own text, not a copy.
static game::Function<const game::String&(const void* dropdown)> dropdown_selection{ "mmoComboBox::GetSelection" };

static game::Function<void(void* pane, const game::LocalizedText& text)> set_pane_text{ "mmoTextPane::SetText" };
static game::Function<void(void* pane, const game::LocalizedText& text, const game::Color& color)> set_pane_colored_text{ "mmoTextPane::SetTextAndColor" };
static game::Function<void(void* tooltip, const game::LocalizedText& title, const game::LocalizedText& text, const game::String& hotkey)> set_tooltip_text{ "mmoTooltip::SetText" };
static game::Function<void(void* button, const game::String& material, bool rebuild)> set_icon_material{ "mmoButtonPane::SetIconMaterial" };
static game::Function<void(void* button, bool toggled)> set_button_toggled{ "mmoButtonPane::SetToggled" };
static game::Function<const game::String&(const void* edit)> text_of_edit{ "mmoTextEditPane::GetText" };
static game::Function<void(void* edit, const game::String& text)> set_text_of_edit{ "mmoTextEditPane::SetText" };
static game::Function<void(void* edit, bool editable)> set_edit_editable{ "mmoTextEditPane::SetEditable" };
static game::Function<game::String(const game::LocalizedText& text)> expand_text{ "vsLocString::AsString" };
static game::Function<void(void* grid, int columns)> set_grid_columns{ "mmoGridTextView::SetColumnCount" };
static game::Function<void(void* grid, int rows)> set_grid_rows{ "mmoGridTextView::SetRowCount" };
static game::Function<void(void* grid, int column, int row, const game::LocalizedText& text)> set_grid_text{ "mmoGridTextView::SetText" };
static game::Function<void(void* grid, int column, int row, const game::LocalizedText& text, const game::Color& color)> set_grid_colored_text{ "mmoGridTextView::SetTextAndColor" };
static game::Function<void(void* pane, const game::LocalizedText& text)> set_property_value{ "mmoPropertyPane::SetValue(vsLocString const&)" };
static game::Function<void(void* grid, int row, int id)> set_grid_row_id{ "mmoGridTextView::SetRowIdentifier" };
static game::Function<void(void* grid, int id)> select_grid_row_id{ "mmoGridTextView::SetSelectedRowIdentifier" };


namespace ui {

void* find_pane(void* view, std::string_view id) {
    return view != nullptr ? find_pane_by_id(view, std::string(id)) : nullptr;
}

void* open_window(std::string_view id) {
    void* window = find_window(game::String(id));

    if (window != nullptr) {
        show_window(window, true, false);
    }

    return window;
}

void close_window(void* window) {
    show_window(window, false, false);
}

void set_window_title(void* window, std::string_view title) {
    set_title(window, game::LocalizedText(title));
}

void set_visible(void* pane, bool visible) {
    game::field<bool>(pane, "mmoPane::visible") = visible;
}

void set_text(void* text_pane, std::string_view text) {
    set_pane_text(text_pane, game::LocalizedText(text));
}

void set_text(void* text_pane, std::string_view text, const game::Color& color) {
    set_pane_colored_text(text_pane, game::LocalizedText(text), color);
}

void set_value(void* property_pane, std::string_view text) {
    set_property_value(property_pane, game::LocalizedText(text));
}

void set_icon(void* button, std::string_view material) {
    set_icon_material(button, std::string(material), true);
}

// A button made with a tooltip in its window file has one to change.
void set_tooltip(void* button, std::string_view title, std::string_view details) {
    void* tooltip = game::field<void*>(button, "mmoButtonPane::tooltipPane");

    if (tooltip != nullptr) {
        set_tooltip_text(tooltip, game::LocalizedText(title), game::LocalizedText(details), game::String());
    }
}

void set_toggled(void* button, bool toggled) {
    set_button_toggled(button, toggled);
}

std::string edit_text(void* edit) {
    return std::string(text_of_edit(edit).view());
}

void set_edit_text(void* edit, std::string_view text) {
    set_text_of_edit(edit, game::String(text));
}

void set_editable(void* edit, bool editable) {
    set_edit_editable(edit, editable);
}

void set_checked(void* checkbox, bool checked) {
    set_checkbox_value(checkbox, checked);
}

// The dropdown copies its choices.
void set_choices(void* dropdown, const std::vector<std::string>& choices) {
    std::vector<game::String> texts(choices.begin(), choices.end());

    set_dropdown_values(dropdown, game::Array<game::String>("vsArray<std::string>", texts));
}

void select_choice(void* dropdown, std::string_view choice) {
    set_dropdown_selection(dropdown, game::String(choice), false);
}

std::string selected_choice(const void* dropdown) {
    return std::string(dropdown_selection(dropdown).view());
}

void set_grid_size(void* grid, int columns, int rows) {
    set_grid_columns(grid, columns);
    set_grid_rows(grid, rows);
}

void set_cell(void* grid, int column, int row, std::string_view text) {
    set_grid_text(grid, column, row, game::LocalizedText(text));
}

void set_cell(void* grid, int column, int row, std::string_view text, const game::Color& color) {
    set_grid_colored_text(grid, column, row, game::LocalizedText(text), color);
}

void set_row_id(void* grid, int row, int id) {
    set_grid_row_id(grid, row, id);
}

void select_row_id(void* grid, int id) {
    select_grid_row_id(grid, id);
}

std::string translated(std::string_view text) {
    return std::string(expand_text(game::LocalizedText(text)).view());
}

}
