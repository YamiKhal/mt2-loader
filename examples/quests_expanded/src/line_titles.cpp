#include "line_titles.h"

#include "line_names.h"
#include "quest_giver.h"
#include "ui.h"

#include <format>
#include <string>

// Braces would read as translation keys wherever the name is shown.
static void without_braces(std::string& name) {
    std::erase_if(name, [](char letter) { return letter == '{' || letter == '}'; });
}


namespace line_titles {

std::string default_line_title(const Questline& line) {
    return std::format("{} {}", ui::translated("{quests_expanded_quests_from}"), quest_giver::name(line.start));
}

std::string line_title(const Questline& line) {
    std::string name = line_names::line_name(line.start);

    return name.empty() ? default_line_title(line) : name;
}

void rename_line(const Questline& line, std::string name) {
    without_braces(name);

    if (name == default_line_title(line)) {
        name.clear();
    }

    line_names::set_line_name(line.start, name);
}

std::string default_chapter_title(const Questline& line, std::size_t chapter) {
    const Chapter& numbered = line.chapters[chapter];
    std::string letter = numbered.letter != '\0' ? std::string(1, numbered.letter) : "";

    return std::format("{} {}{}", ui::translated("{quests_expanded_chapter}"), numbered.number, letter);
}

std::string chapter_title(const Questline& line, std::size_t chapter) {
    std::string name = chapter == 0 ? line_names::first_chapter_name(line.start) : line_names::chapter_name(line.chapters[chapter].giver);

    return name.empty() ? default_chapter_title(line, chapter) : name;
}

void rename_chapter(const Questline& line, std::size_t chapter, std::string name) {
    without_braces(name);

    if (name == default_chapter_title(line, chapter)) {
        name.clear();
    }

    if (chapter == 0) {
        line_names::set_first_chapter_name(line.start, name);
    } else {
        line_names::set_chapter_name(line.chapters[chapter].giver, name);
    }
}

}
