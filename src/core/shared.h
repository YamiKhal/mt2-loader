#ifndef CORE_SHARED_H
#define CORE_SHARED_H

#include <stdbool.h>

#define SHARED_NAME_CAPACITY 64

typedef enum ShareResult {
    SHARE_ADDED,
    SHARE_NAME_INVALID,
    SHARE_NAME_TAKEN,
    SHARE_FULL,
} ShareResult;

ShareResult shared_add(const char* owner, const char* name, void* pointer, const char** taken_by);
void* shared_get(const char* name);

#endif
