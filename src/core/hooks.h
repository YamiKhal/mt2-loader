#ifndef CORE_HOOKS_H
#define CORE_HOOKS_H

#include <stdbool.h>
#include <stddef.h>

#define HOOK_PROBLEM_CAPACITY 200

bool hooks_add(const char* owner, void* target, void* detour, void** original, char* problem, size_t problem_size);
// Hooks one call instruction (E8 or E9) instead of a whole function: only that call goes to detour.
bool hooks_add_call(const char* owner, void* call, void* detour, void** original, char* problem, size_t problem_size);
bool hooks_remove(const char* owner, void* detour, char* problem, size_t problem_size);
int hooks_count_on(void* target);
// Where a hooked call instruction went before it was hooked, so it can still be found by what it calls.
bool hooks_call_destination(const void* call, void** destination);

#endif
