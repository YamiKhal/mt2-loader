#include "chapter_rewards.h"
#include "dungeon_quests.h"
#include "dungeon_runs.h"
#include "hand_ins.h"
#include "hand_offs.h"
#include "level_gates.h"
#include "line_extensions.h"
#include "line_finish.h"
#include "line_thoughts.h"
#include "main_line_places.h"
#include "main_thread.h"
#include "main_preference.h"
#include "objective_undo.h"
#include "quest_card.h"
#include "card_selection.h"
#include "progress_text.h"
#include "quest_details.h"
#include "quest_menu.h"
#include "quest_owners.h"
#include "quest_types.h"
#include "quests_report.h"
#include "quests_tab.h"
#include "retracing.h"
#include "settings.h"
#include "steering.h"
#include "story.h"
#include "story_demand.h"
#include "story_releases.h"
#include "target_arrows.h"
#include "target_editing.h"
#include "target_levels.h"
#include "window_files.h"

#include <mt2loader.hpp>

void plugin::init() {
    read_settings();

    main_thread::install();
    retracing::install();
    hand_ins::install();
    hand_offs::install();
    level_gates::install();
    main_preference::install();
    main_line_places::install();
    line_finish::install();
    line_extensions::install();
    line_thoughts::install();
    chapter_rewards::install();
    story::install();
    story_demand::install();
    story_releases::install();
    quest_card::install();
    quests_tab::install();
    quest_menu::install();
    card_selection::install();
    quest_details::install();
    quest_types::install();
    target_editing::install();
    steering::install();
    quest_owners::install();
    target_levels::install();
    objective_undo::install();
    target_arrows::install();
    progress_text::install();
    dungeon_quests::install();
    dungeon_runs::install();
    window_files::install();
    quests_report::install();

    plugin::log("Quest givers hand players on to each other, and the main questline leads them (main questline preference {}%, "
        "repeat cooldown {} days)", settings.main_preference, settings.repeat_cooldown_days);
}
