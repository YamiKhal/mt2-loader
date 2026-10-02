#include "captive_looks.h"
#include "captive_picker.h"
#include "captive_variants.h"

#include <mt2loader.hpp>

void plugin::init() {
    captive_variants::install();
    captive_looks::install();
    captive_picker::install();

    plugin::log("Captives can be placed in dungeons");
}
