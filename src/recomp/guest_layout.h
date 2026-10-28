/* Guest address space of the recompiled lf2.exe.
 * MUST match tools/emu/lf2emu.py: the Unicorn oracle and the port allocate
 * identically, which is what makes a whole-memory diff between them valid. */
#ifndef LF2_GUEST_LAYOUT_H
#define LF2_GUEST_LAYOUT_H

#define GUEST_IMAGE_BASE 0x00400000u
#define GUEST_TIB_BASE   0x00760000u
#define GUEST_STACK_TOP  0x00A00000u
#define GUEST_STACK_SIZE 0x00200000u
#define GUEST_HEAP_BASE  0x00A00000u
#define GUEST_HEAP_SIZE  0x08000000u
#define GUEST_END        (GUEST_HEAP_BASE + GUEST_HEAP_SIZE)
#define GUEST_STUB_BASE  0xF0000000u   /* import/COM stubs: addresses only */
#define GUEST_MAGIC_RET  0xF9000000u

#endif
