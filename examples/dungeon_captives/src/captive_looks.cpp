#include "captive_looks.h"

#include "captive_variants.h"
#include "character_types.h"

#include <mt2loader.hpp>

#include <string>


// A character's actor in a pose its skeleton has, standing otherwise; nullptr for one without a costume.
static void* actor_of(std::string_view variant, void* scene) {
    void* actor = game::actors::make(character_types::named(captive_variants::character_of(variant)), scene);

    if (actor != nullptr && !game::actors::play(actor, captive_variants::pose_of(variant))) {
        game::actors::play(actor, captive_variants::poses.front());
    }

    return actor;
}


namespace captive_looks {

void install() {
    game::in("mmoGizmoLibrary::MakeGizmoActor")
        .hook<void*(void* library, const game::String& type, const game::String& variant, void* scene, bool shared)>(
            [](auto make, void* library, const game::String& type, const game::String& variant, void* scene, bool shared) {
                if (type.view() != captive_variants::type_name) {
                    return make(library, type, variant, scene, shared);
                }

                if (void* actor = actor_of(variant.view(), scene)) {
                    return actor;
                }

                return make(library, type, game::String(std::string(captive_variants::base_variant)), scene, shared);
            });
}

}
