#include "steering.h"

#include "branch_choices.h"
#include "main_thread.h"
#include "objectives.h"
#include "parties.h"
#include "places.h"
#include "player.h"
#include "quest_owners.h"
#include "tour_progress.h"

#include <mt2loader.hpp>

#include <mutex>
#include <unordered_map>
#include <vector>

// mmoQuest::Action, as DefaultQuestActionForDestination picks it.
constexpr int go_to_action = 0;
constexpr int kill_monsters_action = 1;
constexpr int kill_it_action = 6;
constexpr int talk_action = 7;
constexpr int picked_hunt_kills = 8;
// mmoAction::Type of a quest's "Do quest" advertisement.
constexpr int do_quest_advertisement = 8;

struct Steered {
    unsigned version = 0;
    const void* quest = nullptr;
    void* target = nullptr;
};

static game::Function<int(void* destination)> default_action{ "mmoQuest::DefaultQuestActionForDestination" };
static game::Function<void(void* action)> finish_action{ "mmoToonAction::Finish()" };
static game::Function<bool(void* party)> move_party_to_quest{ "mmoParty::SetUpMoveToQuest" };

// What each player's copy of a quest is steered to, until a tour, a branch or a player's progress changes, or the
// copy is of another quest.
static std::unordered_map<const void*, Steered> steered;
static unsigned version = 1;
// The game may ask from its worker threads too.
static std::mutex steered_lock;
// The quest of the player's quest step being started, for its walk.
static void* starting_quest = nullptr;


// A tour step moves only its own players on: their copies are worked out again, the rest stay.
static void forget(const void* instance) {
    std::lock_guard forgetting(steered_lock);
    steered.erase(instance);
}

static void* tour_quest_of(const void* instance) {
    void* quest = player::quest_of(instance);

    return objectives::of(quest) == Objective::tour ? quest : nullptr;
}

static void* steered_quest_of(const void* instance) {
    void* quest = player::quest_of(instance);

    return objectives::of(quest) != Objective::standard ? quest : nullptr;
}

// Where the player goes next: a tour's next target, or the quest giver of the branch they take.
static void* target_for(void* toon, const void* quest) {
    return objectives::of(quest) == Objective::tour ? tour_progress::current_target(toon, quest) : branch_choices::target(toon, quest);
}

// The player's target on this tour or branch, or nullptr for a quest that's neither (or a copy no player holds).
static void* target_of(const void* instance) {
    const void* held = player::quest_of(instance);

    {
        std::lock_guard reading(steered_lock);
        auto found = steered.find(instance);

        if (found != steered.end() && found->second.version == version && found->second.quest == held) {
            return found->second.target;
        }
    }

    void* quest = steered_quest_of(instance);
    void* toon = quest != nullptr ? quest_owners::player_of(instance) : nullptr;
    void* target = toon != nullptr ? target_for(toon, quest) : nullptr;

    // A branch not chosen yet, asked from a worker thread (which doesn't choose), is asked again next time.
    if (target == nullptr && toon != nullptr && !main_thread::is_current()) {
        return nullptr;
    }

    std::lock_guard writing(steered_lock);
    steered[instance] = Steered{ version, held, target };

    return target;
}

static int kills_for(const void* quest, int action) {
    int kills = game::field<int>(quest, "mmoQuest::monstersToKill");

    if (action == kill_monsters_action) {
        return kills > 0 ? kills : picked_hunt_kills;
    }

    return action == kill_it_action ? 1 : 0;
}

// As IsReadyToTurnIn judges a quest, for the target alone: kills, being there, or the building's service used.
static bool target_done(const void* instance, const void* quest, void* target) {
    int action = default_action(target);

    if (action == kill_monsters_action || action == kill_it_action) {
        return game::field<int>(instance, "mmoQuestInstance::monstersDefeated") >= kills_for(quest, action);
    }

    if (action == go_to_action || action == talk_action) {
        return game::field<bool>(instance, "mmoQuestInstance::visited");
    }

    return game::field<bool>(instance, "mmoQuestInstance::actionPerformed");
}

static void start_afresh(void* instance) {
    game::field<bool>(instance, "mmoQuestInstance::visited") = false;
    game::field<bool>(instance, "mmoQuestInstance::actionPerformed") = false;
    game::field<int>(instance, "mmoQuestInstance::monstersDefeated") = 0;
}

// A party goes round a tour together: a target one member does is done for every member on the same tour.
static void share_with_party(void* toon, const void* quest, void* target) {
    for (void* member : parties::others_with(toon)) {
        for (void* record : player::records(member)) {
            void* instance = player::instance_of(record);

            if (player::quest_of(instance) != quest || tour_progress::targets_left(member, quest) <= 1) {
                continue;
            }

            if (tour_progress::current_target(member, quest) == target) {
                start_afresh(instance);
            }

            tour_progress::mark_done(member, quest, target);
            forget(instance);
        }
    }
}

// A target done with more to go: it's kept as done, and the copy starts afresh for the next.
static bool advance(void* toon, void* instance) {
    void* quest = tour_quest_of(instance);
    void* target = quest != nullptr ? target_of(instance) : nullptr;

    if (target == nullptr || !target_done(instance, quest, target) || tour_progress::targets_left(toon, quest) <= 1) {
        return false;
    }

    tour_progress::mark_done(toon, quest, target);
    start_afresh(instance);
    forget(instance);
    share_with_party(toon, quest, target);

    return true;
}

// How long a player reckons a quest takes to walk: to their next target, then round the rest of a tour. It runs on
// the game's worker threads, so it reads only the saved game and the game (a branch's choice as kept).
static float travel_time(float time, const void* plan, const void* advertisement) {
    if (game::field<int>(advertisement, "mmoAdvertisement::action") != do_quest_advertisement) {
        return time;
    }

    void* quest = game::field<void*>(advertisement, "mmoQuestAdvertisement::quest");
    void* toon = game::field<void*>(plan, "mmoToonPlanTask::toon");
    Objective objective = objectives::of(quest);

    if (toon == nullptr || objective == Objective::standard) {
        return time;
    }

    bool tour = objective == Objective::tour;
    void* target = tour ? tour_progress::current_target(toon, quest) : branch_choices::chosen(toon, quest);
    float speed = game::field<float>(toon, "mmoMapEntity::speed");
    std::vector<void*> targets = objectives::targets(quest);

    if (target == nullptr || speed <= 0.0f || targets.empty()) {
        return time;
    }

    // The game's own time is to the quest's own target, where the advertisement is.
    float to_next = target == targets.front() ? time : places::distance(places::of(toon), places::of(target)) / speed;

    return tour ? to_next + tour_progress::route_length(toon, quest) / speed : to_next;
}


namespace steering {

void changed() {
    std::lock_guard writing(steered_lock);
    version++;
    steered.clear();
}

void install() {
    game::in("mmoModeInGame::DoInit").before([](void*) {
        changed();
        starting_quest = nullptr;
    });

    game::in("mmoQuestInstance::GetQuestDestination()").after([](void* destination, void* instance) {
        void* target = target_of(instance);

        return target != nullptr ? target : destination;
    });

    game::in("mmoQuestInstance::GetAction").after([](int action, const void* instance) {
        void* target = target_of(instance);

        return target != nullptr ? default_action(target) : action;
    });

    game::in("mmoQuestInstance::MonstersRemaining").after([](int remaining, const void* instance) {
        void* target = target_of(instance);

        if (target == nullptr) {
            return remaining;
        }

        return kills_for(player::quest_of(instance), default_action(target)) - game::field<int>(instance, "mmoQuestInstance::monstersDefeated");
    });

    // A tour is ready once the last target left is done; a branch as the game judges a talk, on arriving.
    game::in("mmoQuestInstance::IsReadyToTurnIn").after([](bool ready, const void* instance) {
        void* quest = tour_quest_of(instance);
        void* target = quest != nullptr ? target_of(instance) : nullptr;

        if (target == nullptr) {
            return ready;
        }

        void* toon = quest_owners::player_of(instance);

        return toon != nullptr && tour_progress::targets_left(toon, quest) == 1 && target_done(instance, quest, target);
    });

    game::in("mmoToonDoQuestAction::Tick").hook<void(void* action, float seconds)>([](auto tick, void* action, float seconds) {
        void* toon = game::field<void*>(action, "mmoToonDoQuestAction::toon");

        if (toon == nullptr) {
            tick(action, seconds);

            return;
        }

        for (void* record : player::records_in_place(toon)) {
            if (advance(toon, player::instance_of(record))) {
                finish_action(action);

                return;
            }
        }

        tick(action, seconds);
    });

    // The game walks the player to the quest's own target; a tour's player goes to their next one, a branch's to the
    // quest giver they take.
    game::in("mmoToonDoQuestAction::DoStart").before([](void* action, void*) {
        starting_quest = game::field<game::WeakPointer>(action, "mmoToonDoQuestAction::quest").get();
    });

    game::in("mmoToonDoQuestAction::DoStart").call("mmoMapEntity::SetUpMoveToQuestDestination")
        .hook<void(void* toon, void* destination)>([](auto move, void* toon, void* destination) {
            bool steered_quest = objectives::of(starting_quest) != Objective::standard;

            for (void* record : steered_quest ? player::records(toon) : std::vector<void*>{}) {
                void* instance = player::instance_of(record);

                if (player::quest_of(instance) != starting_quest) {
                    continue;
                }

                if (void* target = target_of(instance)) {
                    destination = target;
                }

                break;
            }

            move(toon, destination);
        });

    // A party does its quests together, without the players' own quest steps: on reaching a tour's target, its
    // members move on to the next, and the party walks there.
    game::in("mmoParty::_ReachedQuestDestination() [clone .part.0]").after([](void* party) {
        bool moved_on = false;

        for (void* toon : parties::members(party)) {
            for (void* record : player::records(toon)) {
                moved_on = advance(toon, player::instance_of(record)) || moved_on;
            }
        }

        if (moved_on) {
            move_party_to_quest(party);
        }
    });

    game::in("mmoToonPlanTask::_EstimateTravelTimeTo").after([](float time, const void* plan, const void* advertisement) {
        return travel_time(time, plan, advertisement);
    });

    game::in("mmoToon::TurnInQuest").after([](void* toon, void* instance) {
        void* quest = player::quest_of(instance);

        if (objectives::of(quest) == Objective::tour) {
            tour_progress::forget(toon, quest);
            changed();
        }
    });
}

}
