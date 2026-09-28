#include "places.h"

#include "layout.h"

#include <mt2loader.hpp>

#include <cmath>

namespace places {

Place of(const void* object) {
    return game::field<Place>(object, static_cast<std::size_t>(layout.position));
}

float distance(const Place& from, const Place& to) {
    return std::hypot(to.x - from.x, to.z - from.z);
}

}
