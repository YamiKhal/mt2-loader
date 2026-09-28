#include "line_header.h"

#include "line_names.h"
#include "line_titles.h"
#include "quest_giver.h"
#include "questlines.h"
#include "ui.h"

#include <map>
#include <optional>
#include <string>

enum class Names {
    line,
    chapter,
    nothing,
};

// What the header shows: a name to edit, with the default it falls back to, or a title to read.
struct Header {
    Names names = Names::nothing;
    std::string name;
    std::string default_name;
    const Questline* line = nullptr;
    std::size_t chapter = 0;
};

struct Shown {
    void* npc = nullptr;
    std::string text;
};

// What each header last showed, so a refresh doesn't undo what the designer is typing.
static std::map<void*, Shown> shown;


static Header header_for(void* npc) {
    const Questline* line = npc != nullptr ? questlines::line_of_giver(npc) : nullptr;

    if (line == nullptr) {
        return Header{};
    }

    if (line->chapters.size() <= 1) {
        if (line->start == npc) {
            return Header{ Names::line, line_names::line_name(npc), line_titles::default_line_title(*line), line };
        }

        return Header{ Names::nothing, line_titles::line_title(*line), "" };
    }

    if (line->start == npc) {
        return Header{ Names::chapter, line_names::first_chapter_name(npc), line_titles::default_chapter_title(0), line, 0 };
    }

    if (std::optional<std::size_t> chapter = questlines::chapter_started_by(npc)) {
        return Header{ Names::chapter, line_names::chapter_name(npc), line_titles::default_chapter_title(*chapter), line, *chapter };
    }

    void* first_quest = quest_giver::quest(npc, 0);
    std::size_t chapter = first_quest != nullptr ? questlines::chapter_of_quest(first_quest) : 0;

    return Header{ Names::nothing, line_titles::chapter_title(*line, chapter), "" };
}

namespace line_header {

void refresh(void* window, void* npc) {
    void* edit = ui::find_pane(window, "quests_expanded_name");

    if (edit == nullptr) {
        return;
    }

    Header header = header_for(npc);
    std::string text = header.name.empty() ? header.default_name : header.name;
    Shown& last = shown[edit];

    ui::set_editable(edit, header.names != Names::nothing);

    if (last.npc != npc || last.text != text) {
        ui::set_edit_text(edit, text);
        last = Shown{ npc, text };
    }
}

void rename(void* window, void* npc) {
    void* edit = ui::find_pane(window, "quests_expanded_name");
    Header header = header_for(npc);

    if (edit == nullptr || header.names == Names::nothing) {
        return;
    }

    std::string name = ui::edit_text(edit);

    if (header.names == Names::line) {
        line_titles::rename_line(*header.line, name);
    } else {
        line_titles::rename_chapter(*header.line, header.chapter, name);
    }

    shown.erase(edit);
    refresh(window, npc);
}

}
