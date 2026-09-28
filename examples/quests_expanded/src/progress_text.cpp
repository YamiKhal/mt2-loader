#include "progress_text.h"

#include "objectives.h"
#include "player.h"
#include "quest_owners.h"
#include "tour_progress.h"

#include <mt2loader.hpp>

#include <cstddef>

struct TourRow {
    int done = 0;
    int total = 0;
};

constexpr const char* progress_window = "mmoToonInfoWindow::UpdateContents";

// The tour on the row being filled in: its progress is asked for (MonstersRemaining), then its numbers made.
static const void* row_instance = nullptr;


static TourRow tour_row(const void* instance) {
    void* quest = player::quest_of(instance);
    void* toon = quest != nullptr && objectives::of(quest) == Objective::tour ? quest_owners::player_of(instance) : nullptr;

    if (toon == nullptr) {
        return TourRow{};
    }

    int total = static_cast<int>(objectives::targets(quest).size());
    int left = static_cast<int>(tour_progress::targets_left(toon, quest));

    return TourRow{ total - left, total };
}


namespace progress_text {

void install() {
    // The first of its two asks, which every row not ready to turn in makes.
    game::in(progress_window).call("mmoQuestInstance::MonstersRemaining").nth(1).hook<int(const void* instance)>([](auto remaining, const void* instance) {
        row_instance = instance;

        return remaining(instance);
    });

    // "{quest_progress}" is "{value} / {total}": total comes first, then value, which ends the row.
    game::in(progress_window).call("vsLocArg::vsLocArg(std::string const&, int)").every()
        .hook<void(void* argument, const game::String& name, int number)>([](auto make, void* argument, const game::String& name, int number) {
            if (row_instance == nullptr) {
                make(argument, name, number);

                return;
            }

            TourRow row = tour_row(row_instance);
            bool value = name.view() == "value";

            if (value) {
                row_instance = nullptr;
            }

            if (row.total > 0) {
                number = value ? row.done : row.total;
            }

            make(argument, name, number);
        });
}

}
