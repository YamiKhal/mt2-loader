#pragma once

// The game's main thread, where it plays out the world and its windows. Its task workers also plan players' next
// steps, load and save; work that changes what the mod traced or keeps belongs on the main thread.
namespace main_thread {

void install();
bool is_current();

}
