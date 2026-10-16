/* SPDX-License-Identifier: GPL-3.0-only */
#include "allocation_unwind.h"
#include "crt_bootstrap.h"
#include "crt_cpp_dispatch.h"
#include "interop.h"
#include <stddef.h>
#ifdef BB_MODEL_RUNTIME
#include "crt_exception_runtime.h"
#endif
#ifdef _WIN32
#define ASM_NAME_INNER(name) "_" #name
#else
#define ASM_NAME_INNER(name) #name
#endif
#define ASM_NAME(name) ASM_NAME_INNER(name)

#ifdef BB_MODEL_RUNTIME
#define CLEANUP(address, name, offset)                                         \
  enum {name##_identity = address};                                           \
  void name##_c(uint32_t *r) {                                                \
    bb_crt_boot_delete_entry(BB_POINTER(r[2] - offset));                       \
  }
BB_ALLOCATION_CLEANUPS(CLEANUP)
#undef CLEANUP
#define HANDLER(identity, name, cleanup)                                        \
  uint32_t name##_entry(void *record, void *node, void *context,                \
                         void *dispatcher) {                                  \
    struct bb_model_scratch scope;                                            \
    bb_model_scratch_begin(&scope, 40);                                        \
    uint32_t info = scope.address;                                            \
    uint32_t words[10] = {0x19930520, 1, info + 32, 0, 0, 0, 0, 0,             \
                          UINT32_MAX, cleanup##_identity};                    \
    for (unsigned i = 0; i < 10; ++i) BB_U32(info + i * 4u) = words[i];        \
    uint32_t result = bb_crt_cpp_internal_entry(                               \
        record, node, context, dispatcher, (void *)(uintptr_t)info, 0, NULL, 0); \
    bb_model_scratch_end(&scope);                                             \
    return result;                                                            \
  }
BB_ALLOCATION_HANDLERS(HANDLER)
#undef HANDLER
#else
/* These three owners have one allocation cleanup and no catch clauses. Keep
 * the shared C dispatcher, but own their metadata rather than reading it from
 * the executable. Only the funclet's owner-frame boundary retains the old ABI. */
struct UnwindRow {
  int32_t parent;
  void (*cleanup)(void);
};
struct FunctionInfo {
  uint32_t magic, states;
  const struct UnwindRow *unwind;
  uint32_t tries;
  const void *try_map;
  uint32_t ip_count;
  const void *ip_map;
  uint32_t flags;
};
_Static_assert(sizeof(struct UnwindRow) == 8, "temporary i386 unwind ABI");
_Static_assert(sizeof(struct FunctionInfo) == 32, "temporary i386 FuncInfo ABI");
#define CLEANUP(address, name, offset)                                         \
  void name##_c(uint32_t *r) {                                                 \
    bb_crt_boot_delete_entry(BB_POINTER(r[2] - offset));                       \
  }                                                                            \
  void __attribute__((naked)) name##_entry(void) {                            \
    __asm__ volatile(                                                          \
        "lea -128(%esp),%esp;pushfl;pushal;cld;pushl %esp;call "              \
        ASM_NAME(name##_c)                                                     \
        ";addl $4,%esp;popal;popfl;lea 128(%esp),%esp;ret");                    \
  }
BB_ALLOCATION_CLEANUPS(CLEANUP)
#undef CLEANUP
#define HANDLER(identity, name, cleanup)                                        \
  uint32_t name##_entry(void *record, void *node, void *context,               \
                        void *dispatcher) {                                    \
    static const struct UnwindRow row = {-1, cleanup##_entry};                \
    static const struct FunctionInfo info = {0x19930520, 1, &row, 0, NULL,      \
                                             0, NULL, 0};                      \
    __asm__ volatile("cld" ::: "cc");                                          \
    return bb_crt_cpp_internal_entry(record, node, context, dispatcher,        \
                                      &info, 0, NULL, 0);                      \
  }
BB_ALLOCATION_HANDLERS(HANDLER)
#undef HANDLER

#endif
