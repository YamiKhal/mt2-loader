#include "hooks.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include <MinHook.h>

#include "code.h"
#include "memory.h"
#include "near_memory.h"

#define MAX_HOOKED 1024
#define MAX_HOOKS_PER_TARGET 32
#define THUNK_SIZE 8
#define CALL_SIZE 5
#define OPCODE_CALL 0xe8
#define OPCODE_JMP 0xe9
// MinHook moves at most this many bytes from a function's start into its trampoline.
#define MOVED_BY_FUNCTION_HOOK 16
#define STUB_SIZE 16
#define STUB_SLOT_OFFSET 8

typedef struct HookLink {
    const char* owner;
    void* detour;
    void** original;
} HookLink;

// A hooked function, or a hooked call instruction. Either way a slot holds the newest hook, and each hook's
// original leads to the one before it, the oldest to the game's own code.
typedef struct HookedTarget {
    void* target;
    void* game_code;
    bool is_call;
    // For a call: jmp [slot], within reach of the call's 32-bit offset, with the slot after it.
    uint8_t* stub;
    int link_count;
    HookLink links[MAX_HOOKS_PER_TARGET];
} HookedTarget;

// MinHook sends each hooked function to its own thunk, and the thunk jumps to whatever its slot holds.
// So a function is hooked once, and adding or removing a plugin's hook only rewrites pointers.
void* hook_slots[MAX_HOOKED];
extern char hook_thunks[];

__asm__(
    ".text\n"
    ".p2align 4\n"
    "hook_thunks:\n"
    ".set thunk_index, 0\n"
    ".rept 1024\n"
    "    jmp *hook_slots + 8 * thunk_index(%rip)\n"
    "    .p2align 3\n"
    "    .set thunk_index, thunk_index + 1\n"
    ".endr\n"
);

// jmp [rip + 2], two filler bytes, then the 8-byte slot, aligned so that rewriting it is atomic.
static const uint8_t STUB_CODE[STUB_SLOT_OFFSET] = { 0xff, 0x25, 0x02, 0x00, 0x00, 0x00, 0xcc, 0xcc };

static HookedTarget targets[MAX_HOOKED];
static int target_count;
static bool minhook_ready;
static SRWLOCK lock = SRWLOCK_INIT;


static HookedTarget* hooked(void* target) {
    for (int index = 0; index < target_count; index++) {
        if (targets[index].target == target) {
            return &targets[index];
        }
    }

    return NULL;
}

static HookedTarget* target_with_detour(void* detour, int* link_index) {
    for (int index = 0; index < target_count; index++) {
        for (int link = 0; link < targets[index].link_count; link++) {
            if (targets[index].links[link].detour == detour) {
                *link_index = link;

                return &targets[index];
            }
        }
    }

    return NULL;
}

static void relink(HookedTarget* target) {
    void* next = target->game_code;

    for (int link = 0; link < target->link_count; link++) {
        *target->links[link].original = next;
        next = target->links[link].detour;
    }

    if (target->is_call) {
        *(void* volatile*)(target->stub + STUB_SLOT_OFFSET) = next;
    } else {
        hook_slots[target - targets] = next;
    }
}

static bool start_minhook(char* problem, size_t problem_size) {
    if (minhook_ready) {
        return true;
    }

    MH_STATUS status = MH_Initialize();

    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        snprintf(problem, problem_size, "the hooking library couldn't start (%s)", MH_StatusToString(status));

        return false;
    }

    minhook_ready = true;

    return true;
}

static HookedTarget* hook_new_function(void* target, char* problem, size_t problem_size) {
    if (!start_minhook(problem, problem_size)) {
        return NULL;
    }

    HookedTarget* hooked_target = &targets[target_count];
    void* thunk = hook_thunks + target_count * THUNK_SIZE;
    void* trampoline = NULL;
    MH_STATUS status = MH_CreateHook(target, thunk, &trampoline);

    if (status != MH_OK) {
        snprintf(problem, problem_size, "it can't be hooked (%s)", MH_StatusToString(status));

        return NULL;
    }

    *hooked_target = (HookedTarget){ .target = target, .game_code = trampoline };
    hook_slots[target_count] = trampoline;

    status = MH_EnableHook(target);

    if (status != MH_OK) {
        MH_RemoveHook(target);
        snprintf(problem, problem_size, "the hook couldn't be switched on (%s)", MH_StatusToString(status));

        return NULL;
    }

    target_count++;

    return hooked_target;
}

// A function hook moved the first bytes of its function elsewhere, so a call among them isn't run from here.
static bool moved_by_function_hook(const uint8_t* call) {
    for (int index = 0; index < target_count; index++) {
        const uint8_t* start = targets[index].target;

        if (!targets[index].is_call && call >= start && call < start + MOVED_BY_FUNCTION_HOOK) {
            return true;
        }
    }

    return false;
}

static HookedTarget* hook_new_call(uint8_t* call, char* problem, size_t problem_size) {
    Instruction instruction;
    bool is_call_or_jump = code_decode(call, &instruction) && instruction.length == CALL_SIZE && instruction.reference != NULL
        && (call[0] == OPCODE_CALL || call[0] == OPCODE_JMP);

    if (!is_call_or_jump) {
        snprintf(problem, problem_size, "it isn't a call to a function (only calls written as E8 or E9 can be hooked)");

        return NULL;
    }

    if (moved_by_function_hook(call)) {
        snprintf(problem, problem_size, "it's in the first bytes of a function that is hooked as a whole");

        return NULL;
    }

    uint8_t* stub = near_memory_allocate(call, STUB_SIZE);

    if (stub == NULL) {
        snprintf(problem, problem_size, "no memory near the game's code is free for it");

        return NULL;
    }

    memcpy(stub, STUB_CODE, sizeof STUB_CODE);
    *(void**)(stub + STUB_SLOT_OFFSET) = instruction.reference;

    int32_t offset = (int32_t)(stub - (call + CALL_SIZE));

    if (!memory_write(call + 1, &offset, sizeof offset)) {
        snprintf(problem, problem_size, "its code couldn't be changed");

        return NULL;
    }

    HookedTarget* hooked_target = &targets[target_count++];
    *hooked_target = (HookedTarget){ .target = call, .game_code = instruction.reference, .is_call = true, .stub = stub };

    return hooked_target;
}

static bool add(const char* owner, void* target, bool is_call, void* detour, void** original, char* problem, size_t problem_size) {
    if (target == NULL || detour == NULL || original == NULL) {
        snprintf(problem, problem_size, "the target, the replacement function and original must all be given");

        return false;
    }

    AcquireSRWLockExclusive(&lock);

    int ignored = 0;
    HookedTarget* hooked_target = NULL;

    if (target_with_detour(detour, &ignored) != NULL) {
        snprintf(problem, problem_size, "this replacement function already hooks something; use one per hook");
    } else if ((hooked_target = hooked(target)) == NULL && target_count == MAX_HOOKED) {
        snprintf(problem, problem_size, "more than %d functions and calls are hooked", MAX_HOOKED);
    } else if (hooked_target == NULL) {
        hooked_target = is_call ? hook_new_call(target, problem, problem_size) : hook_new_function(target, problem, problem_size);
    }

    bool added = hooked_target != NULL && hooked_target->link_count < MAX_HOOKS_PER_TARGET;

    if (hooked_target != NULL && !added) {
        snprintf(problem, problem_size, "more than %d hooks on one function or call", MAX_HOOKS_PER_TARGET);
    }

    if (added) {
        hooked_target->links[hooked_target->link_count++] = (HookLink){ owner, detour, original };
        relink(hooked_target);
    }

    ReleaseSRWLockExclusive(&lock);

    return added;
}


bool hooks_add(const char* owner, void* target, void* detour, void** original, char* problem, size_t problem_size) {
    return add(owner, target, false, detour, original, problem, problem_size);
}

bool hooks_add_call(const char* owner, void* call, void* detour, void** original, char* problem, size_t problem_size) {
    return add(owner, call, true, detour, original, problem, problem_size);
}

bool hooks_remove(const char* owner, void* detour, char* problem, size_t problem_size) {
    AcquireSRWLockExclusive(&lock);

    int link = 0;
    HookedTarget* target = target_with_detour(detour, &link);
    bool removed = target != NULL && strcmp(target->links[link].owner, owner) == 0;

    if (target == NULL) {
        snprintf(problem, problem_size, "that function isn't a hook");
    } else if (!removed) {
        snprintf(problem, problem_size, "that hook belongs to %s", target->links[link].owner);
    }

    if (removed) {
        memmove(&target->links[link], &target->links[link + 1], (size_t)(target->link_count - link - 1) * sizeof target->links[0]);
        target->link_count--;
        relink(target);
    }

    ReleaseSRWLockExclusive(&lock);

    return removed;
}

bool hooks_call_destination(const void* call, void** destination) {
    AcquireSRWLockShared(&lock);

    HookedTarget* hooked_target = hooked((void*)call);
    bool found = hooked_target != NULL && hooked_target->is_call;

    if (found) {
        *destination = hooked_target->game_code;
    }

    ReleaseSRWLockShared(&lock);

    return found;
}

int hooks_count_on(void* target) {
    AcquireSRWLockShared(&lock);

    HookedTarget* hooked_target = hooked(target);
    int count = hooked_target != NULL ? hooked_target->link_count : 0;

    ReleaseSRWLockShared(&lock);

    return count;
}
