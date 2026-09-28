#include "near_memory.h"

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#define REGION_SIZE (64 * 1024)
#define MAX_REGIONS 64
#define ALIGNMENT 16
// Well inside the 2 GB a 32-bit offset reaches, so every instruction in the game's image reaches the memory.
#define MAX_DISTANCE (1024ull * 1024 * 1024)

typedef struct Region {
    uint8_t* start;
    size_t size;
    size_t used;
} Region;

static Region regions[MAX_REGIONS];
static int region_count;
static SRWLOCK lock = SRWLOCK_INIT;


static bool is_near(const uint8_t* address, const uint8_t* origin) {
    uintptr_t distance = address > origin ? (uintptr_t)(address - origin) : (uintptr_t)(origin - address);

    return distance < MAX_DISTANCE;
}

static uint8_t* reserve_near(const uint8_t* origin, size_t size) {
    SYSTEM_INFO system;
    GetSystemInfo(&system);

    uintptr_t granularity = system.dwAllocationGranularity;
    uintptr_t center = (uintptr_t)origin & ~(granularity - 1);
    uintptr_t lowest = center > MAX_DISTANCE ? center - MAX_DISTANCE : granularity;

    // Below the game first, where nothing else tends to be, then above it.
    for (uintptr_t candidate = center - granularity; candidate >= lowest; candidate -= granularity) {
        void* made = VirtualAlloc((void*)candidate, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);

        if (made != NULL) {
            return made;
        }
    }

    for (uintptr_t candidate = center + granularity; candidate < center + MAX_DISTANCE; candidate += granularity) {
        MEMORY_BASIC_INFORMATION information;

        if (VirtualQuery((void*)candidate, &information, sizeof information) == 0 || information.State != MEM_FREE) {
            continue;
        }

        void* made = VirtualAlloc((void*)candidate, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);

        if (made != NULL) {
            return made;
        }
    }

    return NULL;
}

static void* take_from(Region* region, size_t size) {
    size_t start = (region->used + ALIGNMENT - 1) & ~(size_t)(ALIGNMENT - 1);

    if (start + size > region->size) {
        return NULL;
    }

    region->used = start + size;

    return region->start + start;
}


void* near_memory_allocate(const void* origin, size_t size) {
    void* taken = NULL;

    if (size == 0) {
        return NULL;
    }

    AcquireSRWLockExclusive(&lock);

    for (int index = 0; index < region_count && taken == NULL; index++) {
        if (is_near(regions[index].start, origin)) {
            taken = take_from(&regions[index], size);
        }
    }

    if (taken == NULL && region_count < MAX_REGIONS) {
        size_t region_size = size > REGION_SIZE ? (size + REGION_SIZE - 1) & ~(size_t)(REGION_SIZE - 1) : REGION_SIZE;
        uint8_t* start = reserve_near(origin, region_size);

        if (start != NULL) {
            regions[region_count] = (Region){ start, region_size, 0 };
            taken = take_from(&regions[region_count], size);
            region_count++;
        }
    }

    ReleaseSRWLockExclusive(&lock);

    return taken;
}
