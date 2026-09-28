#include "before_stub.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "near_memory.h"

extern const uint8_t before_template[];
extern const uint8_t before_template_callback[];
extern const uint8_t before_template_original[];
extern const uint8_t before_template_end[];

/*
    Copied once per hook. On entry the arguments are in rcx, rdx, r8, r9 (or xmm0-xmm3) and from [rsp + 0x28] on.
    The stub saves the four registers, copies eight stack arguments down into its own call area so the callback
    finds them where it expects, calls the callback, puts the registers back and jumps to the function: the
    function then runs as if called directly. 0xa8 bytes keep the stack 16-byte aligned for the callback.
    The two slots at the end are reached rip-relative, so the copy works anywhere.
*/
__asm__(
    ".text\n"
    ".p2align 4\n"
    "before_template:\n"
    "    subq $0xa8, %rsp\n"
    "    movq %rcx, 0x60(%rsp)\n"
    "    movq %rdx, 0x68(%rsp)\n"
    "    movq %r8, 0x70(%rsp)\n"
    "    movq %r9, 0x78(%rsp)\n"
    "    movq %xmm0, 0x80(%rsp)\n"
    "    movq %xmm1, 0x88(%rsp)\n"
    "    movq %xmm2, 0x90(%rsp)\n"
    "    movq %xmm3, 0x98(%rsp)\n"
    "    movq 0xd0(%rsp), %rax\n"
    "    movq %rax, 0x20(%rsp)\n"
    "    movq 0xd8(%rsp), %rax\n"
    "    movq %rax, 0x28(%rsp)\n"
    "    movq 0xe0(%rsp), %rax\n"
    "    movq %rax, 0x30(%rsp)\n"
    "    movq 0xe8(%rsp), %rax\n"
    "    movq %rax, 0x38(%rsp)\n"
    "    movq 0xf0(%rsp), %rax\n"
    "    movq %rax, 0x40(%rsp)\n"
    "    movq 0xf8(%rsp), %rax\n"
    "    movq %rax, 0x48(%rsp)\n"
    "    movq 0x100(%rsp), %rax\n"
    "    movq %rax, 0x50(%rsp)\n"
    "    movq 0x108(%rsp), %rax\n"
    "    movq %rax, 0x58(%rsp)\n"
    "    call *before_template_callback(%rip)\n"
    "    movq 0x60(%rsp), %rcx\n"
    "    movq 0x68(%rsp), %rdx\n"
    "    movq 0x70(%rsp), %r8\n"
    "    movq 0x78(%rsp), %r9\n"
    "    movq 0x80(%rsp), %xmm0\n"
    "    movq 0x88(%rsp), %xmm1\n"
    "    movq 0x90(%rsp), %xmm2\n"
    "    movq 0x98(%rsp), %xmm3\n"
    "    addq $0xa8, %rsp\n"
    "    jmp *before_template_original(%rip)\n"
    "    .p2align 3\n"
    "before_template_callback:\n"
    "    .quad 0\n"
    "before_template_original:\n"
    "    .quad 0\n"
    "before_template_end:\n"
);


void* before_stub_make(void* callback, void*** original) {
    size_t size = (size_t)(before_template_end - before_template);
    uint8_t* stub = near_memory_allocate(callback, size);

    if (stub == NULL) {
        return NULL;
    }

    memcpy(stub, before_template, size);
    *(void**)(stub + (before_template_callback - before_template)) = callback;
    *original = (void**)(stub + (before_template_original - before_template));

    return stub;
}
