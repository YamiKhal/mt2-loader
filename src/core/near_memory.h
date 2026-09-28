#ifndef CORE_NEAR_MEMORY_H
#define CORE_NEAR_MEMORY_H

#include <stddef.h>

// Memory within reach of a 32-bit offset from origin (readable, writable, executable), 16-byte aligned, never freed.
// NULL if none could be found.
void* near_memory_allocate(const void* origin, size_t size);

#endif
