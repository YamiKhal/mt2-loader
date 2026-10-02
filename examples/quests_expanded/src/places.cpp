#include "places.h"

#include <mt2loader.hpp>

#include <cmath>

namespace places {

Place of(const void* object) {
    return game::field<Place>(object, "mmoProp::transform.m_translation");
}

float distance(const Place& from, const Place& to) {
    return std::hypot(to.x - from.x, to.z - from.z);
}

}
