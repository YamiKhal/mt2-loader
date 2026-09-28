#ifndef CORE_BEFORE_STUB_H
#define CORE_BEFORE_STUB_H

// A detour that calls callback with the hooked function's arguments, then jumps on to *original with the same
// arguments, so the function's result reaches the caller untouched, whatever it is. *original is where the hook
// stores what the stub jumps to. NULL if no memory could be had.
void* before_stub_make(void* callback, void*** original);

#endif
