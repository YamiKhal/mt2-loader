#ifndef CORE_CLASS_SIZES_H
#define CORE_CLASS_SIZES_H

#include <stddef.h>
#include <windows.h>

/*
    How many bytes the game allocates for an object of a class ("mmoActor"), read from its own code each time the
    game starts: "mov ecx, size; call operator new; mov rcx, rax; call Class::Class". The size most places agree on,
    or 0 when the game never makes one that way.
*/
size_t class_size_of(HMODULE game, const char* class_name);

#endif
