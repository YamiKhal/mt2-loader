#pragma once

// The Story demand: players want new main questline chapters and are let down by poor ones. A saved game's Custom
// Rules can disable it ("Disable story demand"), which keeps the rest of the mod.
namespace story {

void install();
bool enabled();

}
