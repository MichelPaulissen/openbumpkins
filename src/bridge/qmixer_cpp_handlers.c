/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_cpp_dispatch.h"
#include "qmixer_internal.h"

/* C replaces MOV-FuncInfo/JMP stubs used by the original registration records.
 * Metadata identity is still a DLL data address; dispatch itself is shared C.
 */
static uintptr_t argument(const uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 2056u + n * 4u);
}
#define HANDLER(address, name, info)                                           \
  uint32_t name##_entry(void *record, void *registration, void *context,       \
                        void *dispatcher) {                                    \
    __asm__ volatile("cld" ::: "cc");                                          \
    return bb_qmix_cpp_internal_entry(record, registration, context,           \
                                      dispatcher, (void *)(uintptr_t)info, 0,  \
                                      (void *)0, 0);                           \
  }                                                                            \
  void name##_c(uint32_t *r) {                                                 \
    r[7] = name##_entry((void *)argument(r, 0), (void *)argument(r, 1),        \
                        (void *)argument(r, 2), (void *)argument(r, 3));       \
    r[8] &= ~0x400u;                                                           \
  }
BB_QMIX_CPP_HANDLERS(HANDLER)
#undef HANDLER
