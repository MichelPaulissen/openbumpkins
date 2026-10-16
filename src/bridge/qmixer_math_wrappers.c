/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <string.h>
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
#define U8(p) BB_U8(p)
#define U16(p) BB_U16(p)

/* Public memory/register math interfaces and the common descriptor wrappers
 * in C. Original descriptor storage and i386 stack/register glue remain. */
struct x87_state { uint32_t environment[7]; uint8_t value[8][10]; };
_Static_assert(sizeof(struct x87_state) == 108, "i386 x87 save image");
#define FP_CLOBBERS "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)", "memory"
static void save(struct x87_state *state) {
  __asm__ volatile("fnsave %0;fwait" : "=m"(*state) : : FP_CLOBBERS);
  uint16_t control = (uint16_t)state->environment[0];
  __asm__ volatile("fldcw %0" : : "m"(control) : "memory");
}
static void restore(struct x87_state *state) {
  uint16_t status; __asm__ volatile("fnstsw %0" : "=m"(status));
  state->environment[1] |= status & 0x3fu;
  __asm__ volatile("frstor %0" : : "m"(*state) : FP_CLOBBERS);
}
static void store(uint32_t p, const void *encoded) {
  long double value = 0; memcpy(&value, encoded, 10);
  double result = (double)value; memcpy((void *)(uintptr_t)p, &result, 8);
}
static void invoke(uint32_t *input, uint32_t frame, uintptr_t target,
                   uint32_t args[16], unsigned count) {
  struct bb_call_state state = {input[7], input[6], input[5], input[8]};
  bb_qmix_call(&state, target, args, frame - 0x2a4u - count * 4u,
               input[0], input[1], frame, input[4], 0);
  input[7] = state.eax; input[6] = state.ecx;
  input[5] = state.edx; input[8] = state.flags;
}
static void wrapper(uint32_t *input, unsigned binary, unsigned memory) {
  uint32_t stack = input[3] + 2052u, frame = stack - 4u;
  uint32_t args[16] = {0};
  BB_U32(frame) = input[2]; BB_U32(frame - 0x2a4u) = input[4];
  if (memory) {
    memcpy(args, (const void *)(uintptr_t)(stack + 4u), 8);
    invoke(input, frame, 0x100159f1u, args, 2);
    if (binary) {
      memcpy(args, (const void *)(uintptr_t)(stack + 12u), 8);
      invoke(input, frame, 0x100159f1u, args, 2);
    }
  }
  struct x87_state state; save(&state);
  U16(frame - 0xa4u) = (uint16_t)state.environment[0];
  if (!memory && !BB_U32(0x10020250u)) {
    store(frame - 0x86u, state.value[binary ? 1 : 0]);
    if (binary) store(frame - 0x7eu, state.value[0]);
  }
  restore(&state);
  invoke(input, frame, binary ? 0x10016a07u : 0x100169a0u, args, 0);
  U8(0x10020d28u) = !memory;
  invoke(input, frame, 0x10015895u, args, 0);
}
/* Common private wrappers retain EDX as the incoming descriptor argument. */
void bb_qmix_math_wrapper_power_register_c(uint32_t *input) { wrapper(input, 1, 0); }
void bb_qmix_math_wrapper_unary_register_c(uint32_t *input) { wrapper(input, 0, 0); }
void bb_qmix_math_wrapper_power_memory_c(uint32_t *input) { wrapper(input, 1, 1); }
void bb_qmix_math_wrapper_unary_memory_c(uint32_t *input) { wrapper(input, 0, 1); }
#define PUBLIC(name, descriptor, binary, memory)                               \
  void name##_c(uint32_t *input) {                                             \
    input[5] = descriptor; wrapper(input, binary, memory);                     \
  }
PUBLIC(bb_qmix_math_public_power_memory, 0x10020194u, 1, 1)
PUBLIC(bb_qmix_math_public_log_memory, 0x10020204u, 0, 1)
PUBLIC(bb_qmix_math_public_log10_memory, 0x100201e4u, 0, 1)
PUBLIC(bb_qmix_math_public_exp_memory, 0x10020224u, 0, 1)
PUBLIC(bb_qmix_math_public_power_register, 0x10020194u, 1, 0)
PUBLIC(bb_qmix_math_public_log_register, 0x10020204u, 0, 0)
PUBLIC(bb_qmix_math_public_log10_register, 0x100201e4u, 0, 0)
PUBLIC(bb_qmix_math_public_exp_register, 0x10020224u, 0, 0)
#undef PUBLIC

#define ARG(n) uint32_t a##n __attribute__((unused))
#define ENTRY(name, signature)                                                 \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;ret"); \
  }
ENTRY(bb_qmix_math_wrapper_power_register, (void))
ENTRY(bb_qmix_math_wrapper_unary_register, (void))
ENTRY(bb_qmix_math_wrapper_power_memory, (ARG(0), ARG(1), ARG(2), ARG(3)))
ENTRY(bb_qmix_math_wrapper_unary_memory, (ARG(0), ARG(1)))
ENTRY(bb_qmix_math_public_power_memory, (ARG(0), ARG(1), ARG(2), ARG(3)))
ENTRY(bb_qmix_math_public_log_memory, (ARG(0), ARG(1)))
ENTRY(bb_qmix_math_public_log10_memory, (ARG(0), ARG(1)))
ENTRY(bb_qmix_math_public_exp_memory, (ARG(0), ARG(1)))
ENTRY(bb_qmix_math_public_power_register, (void))
ENTRY(bb_qmix_math_public_log_register, (void))
ENTRY(bb_qmix_math_public_log10_register, (void))
ENTRY(bb_qmix_math_public_exp_register, (void))
#undef ENTRY
#undef ARG
