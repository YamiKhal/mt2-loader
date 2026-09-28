#pragma once

// Two thoughts players have about a chapter they finish, beside the game's own (mmoToonThought::Type): a great one and
// a poor one. They show in the Thoughts report and on players like the game's, and are kept in saved games by their
// word; a game loaded without the mod reads them as no thought.
namespace line_thoughts {

void install();

int great();
int poor();

}
