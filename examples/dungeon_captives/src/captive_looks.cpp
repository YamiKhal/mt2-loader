#include "captive_looks.h"

#include "captive_variants.h"
#include "character_types.h"
#include "layout.h"

#include <mt2loader.hpp>

#include <cstddef>
#include <string>

// An animation the actor's skeleton doesn't have.
constexpr int missing = -1;

static game::Function<void*(void* type)> costume_of{ "_ZN16mmoCharacterType10GetCostumeEv" };
static game::Function<void*(std::size_t size)> allocate{ "_Znwy" };
static game::Function<void(void* actor, const game::String& skeleton)> start_actor{ "_ZN8mmoActorC1ERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE" };
static game::Function<void(const void* costume, void* actor, int colors, bool, void* scene)> dress{ "mmoCostume::ApplyToActor" };
static game::Function<int(void* actor, const game::String& name)> animation_named{ "mmoActor::GetAnimation" };
static game::Function<void(void* actor, int animation)> play{ "mmoActor::PlayAnimation" };
static game::Function<void(void* actor, float seconds)> update{ "mmoActor::Update" };
static game::Function<void(void* actor)> apply{ "mmoActor::Apply" };
static game::Function<void(void* actor, bool visible)> set_visible{ "mmoActor::SetVisible" };
static game::Function<void(void* actor)> build_bounds{ "mmoActor::BuildBoundingBox" };


static void* character_for(std::string_view variant) {
    void* character = character_types::named(captive_variants::character_of(variant));

    return character != nullptr && costume_of(character) != nullptr ? character : nullptr;
}

static int animation_for(void* actor, std::string_view pose) {
    int animation = animation_named(actor, game::String(std::string(pose)));

    return animation != missing ? animation : animation_named(actor, game::String(std::string(captive_variants::poses.front())));
}

// As the game makes a character's actor (mmoCharacterModelManager::MakeActor), finished as it finishes a gizmo's
// (mmoGizmoLibrary::MakeGizmoActor).
static void* actor_of(void* character, std::string_view pose, void* scene) {
    void* costume = costume_of(character);
    void* actor = allocate(static_cast<std::size_t>(layout.actor_size));

    start_actor(actor, game::field<game::String>(costume, static_cast<std::size_t>(layout.costume_skeleton)));
    dress(costume, actor, game::field<int>(character, static_cast<std::size_t>(layout.type_colors)), false, scene);
    play(actor, animation_for(actor, pose));
    update(actor, 0.0f);
    apply(actor);
    set_visible(actor, true);
    build_bounds(actor);

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

                if (void* character = character_for(variant.view())) {
                    return actor_of(character, captive_variants::pose_of(variant.view()), scene);
                }

                return make(library, type, game::String(std::string(captive_variants::base_variant)), scene, shared);
            });
}

}
