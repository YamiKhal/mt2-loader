#include "captive_looks.h"
#include "captive_picker.h"
#include "captive_variants.h"
#include "layout.h"

#include <mt2loader.hpp>

void plugin::init() {
    read_layout();

    captive_variants::install();
    captive_looks::install();
    captive_picker::install();

    plugin::log("Captives can be placed in dungeons (gizmo variants at +{:#x}, actor {:#x} bytes, costume skeleton at +{:#x}, "
        "type colors at +{:#x})",
        layout.definition_variants, layout.actor_size, layout.costume_skeleton, layout.type_colors);
}
