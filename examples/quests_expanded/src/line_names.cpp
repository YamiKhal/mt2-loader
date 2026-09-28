#include "line_names.h"

#include <mt2loader.hpp>

static void keep_text(void* npc, const std::string& key, std::string_view text) {
    if (text.empty()) {
        game::saved(npc).erase(key);
    } else {
        game::saved(npc).set(key, text);
    }
}


namespace line_names {

std::string line_name(const void* start) {
    return game::saved(start).get("name", "");
}

void set_line_name(void* start, std::string_view name) {
    keep_text(start, "name", name);
}

bool starts_chapter(const void* npc) {
    return game::saved(npc).get("chapter", false);
}

void set_starts_chapter(void* npc, bool starts) {
    if (starts) {
        game::saved(npc).set("chapter", true);
    } else {
        game::saved(npc).erase("chapter");
    }
}

std::string chapter_name(const void* npc) {
    return game::saved(npc).get("chapter_name", "");
}

void set_chapter_name(void* npc, std::string_view name) {
    keep_text(npc, "chapter_name", name);
}

std::string first_chapter_name(const void* start) {
    return game::saved(start).get("first_chapter_name", "");
}

void set_first_chapter_name(void* start, std::string_view name) {
    keep_text(start, "first_chapter_name", name);
}

}
