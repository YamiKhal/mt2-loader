#include "shared.h"

#include <string.h>
#include <windows.h>

#define MAX_SHARED 256

typedef struct SharedEntry {
    char name[SHARED_NAME_CAPACITY];
    const char* owner;
    void* pointer;
} SharedEntry;

static SharedEntry entries[MAX_SHARED];
static int entry_count;
static SRWLOCK lock = SRWLOCK_INIT;


static SharedEntry* entry_named(const char* name) {
    for (int index = 0; index < entry_count; index++) {
        if (strcmp(entries[index].name, name) == 0) {
            return &entries[index];
        }
    }

    return NULL;
}

ShareResult shared_add(const char* owner, const char* name, void* pointer, const char** taken_by) {
    if (name == NULL || name[0] == '\0' || strlen(name) >= SHARED_NAME_CAPACITY) {
        return SHARE_NAME_INVALID;
    }

    AcquireSRWLockExclusive(&lock);

    SharedEntry* existing = entry_named(name);
    ShareResult result = SHARE_ADDED;

    if (existing != NULL) {
        *taken_by = existing->owner;
        result = SHARE_NAME_TAKEN;
    } else if (entry_count == MAX_SHARED) {
        result = SHARE_FULL;
    } else {
        SharedEntry* entry = &entries[entry_count++];
        strcpy(entry->name, name);
        entry->owner = owner;
        entry->pointer = pointer;
    }

    ReleaseSRWLockExclusive(&lock);

    return result;
}

void* shared_get(const char* name) {
    if (name == NULL) {
        return NULL;
    }

    AcquireSRWLockShared(&lock);

    SharedEntry* entry = entry_named(name);
    void* pointer = entry != NULL ? entry->pointer : NULL;

    ReleaseSRWLockShared(&lock);

    return pointer;
}
