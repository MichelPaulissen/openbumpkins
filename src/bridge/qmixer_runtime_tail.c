/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* Remaining table-referenced runtime entries. Exception descriptors own a CRT
 * buffer; deleting descriptors additionally release the outer mixer allocation.
 */
static uint32_t stack(const uint32_t *r) { return r[3] + 2052u; }
static void call(uint32_t *r, uintptr_t target, uint32_t argument) {
  uint32_t args[16] = {argument};
  struct bb_call_state state = {r[7], r[6], r[5], r[8]};
  long double call_float __attribute__((unused)) = 0;
  BB_QMIX_DISPATCH(&state, target, args, stack(r) - 256u, r[0], r[1], r[2],
                   r[4], 0);
  r[7] = state.eax;
  r[6] = state.ecx;
  r[5] = state.edx;
  r[8] = state.flags;
}
void bb_qmix_exception_object_destructor_c(uint32_t *r) {
  uint32_t self = r[6], buffer = BB_U32(self + 4u);
  BB_U32(self) = BB_QMIX_VTABLE(bb_qmix_vtable_exception_object);
  r[7] = buffer;
  if (buffer)
    call(r, 0x100117c0u, buffer);
}
void bb_qmix_exception_object_delete_c(uint32_t *r) {
  uint32_t self = r[6], flags = BB_U32(stack(r) + 4u);
  call(r, 0x10011680u, 0);
  if (flags & 1u)
    call(r, 0x10001070u, self);
  r[7] = self;
}
void bb_qmix_fp_unavailable_c(uint32_t *r) { call(r, 0x10012030u, 2); }

#ifndef BB_MODEL_RUNTIME
#define ENTRY(name, signature, cleanup)                                        \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "lea -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(          \
            name##_c) ";lea 4(%esp),%esp;popal;popfl;lea 2048(%esp),%esp;"     \
                      "ret $" #cleanup);                                       \
  }
ENTRY(bb_qmix_exception_object_destructor, (void), 0)
ENTRY(bb_qmix_exception_object_delete, (uint32_t flags __attribute__((unused))),
      4)
ENTRY(bb_qmix_fp_unavailable, (void), 0)
#undef ENTRY
#endif
