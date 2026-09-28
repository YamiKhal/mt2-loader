#include "main_thread.h"

#include <mt2loader.hpp>

#include <atomic>
#include <functional>
#include <thread>

// The main thread's id, hashed, once the game has played a frame; 0 until then.
static std::atomic<std::size_t> main_id{ 0 };


static std::size_t current_id() {
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
}


namespace main_thread {

void install() {
    game::in("mmoModeInGame::Update(float)").before([](void*, float) {
        if (main_id.load(std::memory_order_relaxed) == 0) {
            main_id.store(current_id(), std::memory_order_relaxed);
        }
    });
}

bool is_current() {
    return main_id.load(std::memory_order_relaxed) == current_id();
}

}
