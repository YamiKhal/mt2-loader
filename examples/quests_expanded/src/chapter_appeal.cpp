#include "chapter_appeal.h"

namespace chapter_appeal {

float of(PlayerType type, const Quality& quality) {
    switch (type) {
    case PlayerType::explorer:
        return 2.0f;
    case PlayerType::achiever:
        return 1.5f;
    case PlayerType::socialiser:
        return quality.givers > 1 ? 1.5f : 1.0f;
    case PlayerType::killer:
        return quality.elite ? 2.0f : 1.0f;
    default:
        return 0.5f;
    }
}

}
