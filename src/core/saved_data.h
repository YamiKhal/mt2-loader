#ifndef CORE_SAVED_DATA_H
#define CORE_SAVED_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SAVED_KEY_CAPACITY 64
#define SAVED_VALUE_CAPACITY (64 * 1024)

typedef void (*SavedDataLog)(const char* format, ...);

// Hooks the game's save and load code so plugins can keep their own values on game objects in saved games.
// Each object with values gets one extra field, "mt2loader", which the game skips when it loads without the loader.
bool saved_data_start(uint8_t* game_base, size_t game_size, SavedDataLog log, char* problem, size_t problem_size);
bool saved_data_started(void);

// key is "<mod id>/<the plugin's key>". A NULL value removes it.
bool saved_data_set(const void* object, const char* key, const char* value, char* problem, size_t problem_size);
// Copies the value if value_size is enough; *length is its length either way. False if the object has no such key.
bool saved_data_get(const void* object, const char* key, char* value, size_t value_size, size_t* length);
// The keys that start with prefix, without it, one per line. Returns the size needed, with the final '\0'.
size_t saved_data_keys(const void* object, const char* prefix, char* keys, size_t keys_size);
// A link to another game object, kept as the object itself: saved as the id the save gives it, and gone when it's
// destroyed. A NULL target removes the key. get_link is NULL for no link (or a text value).
bool saved_data_set_link(const void* object, const char* key, const void* target, char* problem, size_t problem_size);
const void* saved_data_get_link(const void* object, const char* key);
// The object at the root of every saved game (the game state), or NULL outside a game.
void* saved_data_root(void);

#endif
