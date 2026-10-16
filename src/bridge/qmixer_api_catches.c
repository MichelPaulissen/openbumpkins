/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#ifdef BB_MODEL_RUNTIME
#include "qmixer_lifetime.h"
#define CATCH_VALUE(r, src) ((src) ? bb_qmix_model_caught_error().value : 7u)
#define RESTORE_CHAIN(r) bb_qmix_model_exception_link((r)[6])
#else
#define CATCH_VALUE(r, src) ((src) ? BB_U32((r)[2] + (src)) : 7u)
#define RESTORE_CHAIN(r) __asm__ volatile("movl %0,%%fs:0" ::"r"((r)[6]) : "memory")
#endif
#ifdef _WIN32
#define BB_ASM_INNER(name) "_" #name
#else
#define BB_ASM_INNER(name) #name
#endif
#define BB_ASM(name) BB_ASM_INNER(name)
#define BB_NUMBER_INNER(value) #value
#define BB_NUMBER(value) BB_NUMBER_INNER(value)

/* Public API catches preserve native C error values or report allocation
 * failure. Logical owner locals remain available through the return action;
 * the working i386 build retains its final physical stack/FS epilogues. */
static void memory_error(uint32_t *r) {
  uint32_t args[16] = {4};
  struct bb_call_state state = {r[7], r[6], r[5], r[8]};
  long double call_float __attribute__((unused)) = 0;
  BB_QMIX_DISPATCH(&state, 0x10005040u, args, r[3] + 2052u - 256u, r[0], r[1], r[2],
               r[4], 0);
  r[6] = state.ecx;
  r[5] = state.edx;
}
#ifndef BB_MODEL_RUNTIME
#define CALL_WORKER(name)                                                      \
  "lea -2048(%esp),%esp;pushfl;pushal;cld;pushl %esp;call " BB_ASM(            \
      name##_c) ";addl $4,%esp;popal;popfl;lea 2048(%esp),%esp;"
#endif
#define CATCH_BODY(address, name, dst, src, continuation, report)               \
  void name##_c(uint32_t *r) {                                                 \
    if (report)                                                                \
      memory_error(r);                                                        \
    if (dst)                                                                   \
      BB_U32(r[2] + (dst)) = CATCH_VALUE(r, src);                               \
    r[7] = continuation;                                                       \
  }

BB_QMIX_API_CATCHES(CATCH_BODY)
#undef CATCH_BODY
#ifndef BB_MODEL_RUNTIME
#define CATCH_ENTRY(address, name, dst, src, continuation, report)             \
  uint32_t __attribute__((naked)) name##_entry(void) {                         \
    __asm__ volatile(CALL_WORKER(name) "ret");                                \
  }
BB_QMIX_API_CATCHES(CATCH_ENTRY)
#undef CATCH_ENTRY
#endif
#define RETURN_BODY(address, name, result, stage, cleanup, pops)                \
  void name##_c(uint32_t *r) {                                                 \
    if (stage)                                                                 \
      BB_U32(r[2] + (stage)) = UINT32_MAX;                                     \
    r[7] = BB_U32(r[2] + (result));                                            \
    r[6] = BB_U32(r[2] - 12u);                                                 \
    RESTORE_CHAIN(r);                                                         \
  }

BB_QMIX_API_RETURNS(RETURN_BODY)
#undef RETURN_BODY
#ifndef BB_MODEL_RUNTIME
#define RETURN_ENTRY(address, name, result, stage, cleanup, pops)              \
  uint32_t __attribute__((naked)) name##_entry(void) {                         \
    __asm__ volatile(CALL_WORKER(name) pops "leave;ret $" BB_NUMBER(cleanup)); \
  }
BB_QMIX_API_RETURNS(RETURN_ENTRY)
#undef RETURN_ENTRY
#undef CALL_WORKER
#endif
