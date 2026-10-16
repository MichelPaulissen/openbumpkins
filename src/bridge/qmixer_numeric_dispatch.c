/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <math.h>
#include <string.h>
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
#define U8(p) BB_U8(p)
#define U16(p) BB_U16(p)

/* Classification and final result/error routing in C. The descriptor tables
 * remain data; not-yet-reimplemented calculation leaves use the bridge. */
struct x87_state { uint32_t environment[7]; uint8_t value[8][10]; };
_Static_assert(sizeof(struct x87_state) == 108, "i386 x87 save image");
#define FP_CLOBBERS "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)", "memory"
static void save(struct x87_state *state) {
  __asm__ volatile("fnsave %0;fwait" : "=m"(*state) : : FP_CLOBBERS);
  uint16_t control = (uint16_t)state->environment[0];
  __asm__ volatile("fldcw %0" : : "m"(control) : "memory");
}
static void restore(const struct x87_state *state) {
  __asm__ volatile("frstor %0" : : "m"(*state) : FP_CLOBBERS);
}
static unsigned top(const struct x87_state *state) {
  return (state->environment[1] >> 11) & 7u;
}
static long double number(const void *p) {
  long double value = 0; memcpy(&value, p, 10); return value;
}
static double scalar(uint32_t p) {
  double value; memcpy(&value, (const void *)(uintptr_t)p, 8); return value;
}
static void store(uint32_t p, long double value) {
  double result = (double)value; memcpy((void *)(uintptr_t)p, &result, 8);
}
static void put(struct x87_state *state, long double value) {
  memcpy(state->value[0], &value, 10);
  uint64_t mantissa; uint16_t exponent;
  memcpy(&mantissa, &value, 8); memcpy(&exponent, (const uint8_t *)&value + 8, 2);
  exponent &= 0x7fff;
  unsigned tag = !exponent && !mantissa ? 1 :
                 !exponent || exponent == 0x7fff || !(mantissa >> 63) ? 2 : 0;
  unsigned shift = 2u * top(state);
  state->environment[2] = (state->environment[2] & ~(3u << shift)) | (tag << shift);
}
static void pop(struct x87_state *state) {
  unsigned old = top(state), next = (old + 1u) & 7u;
  state->environment[2] |= 3u << (2u * old);
  state->environment[1] = (state->environment[1] & ~0x3800u) | (next << 11);
  uint8_t first[10]; memcpy(first, state->value[0], 10);
  memmove(state->value[0], state->value[1], 70); memcpy(state->value[7], first, 10);
}
static void push(struct x87_state *state, long double value) {
  unsigned next = (top(state) - 1u) & 7u;
  state->environment[1] = (state->environment[1] & ~0x3800u) | (next << 11);
  memmove(state->value[1], state->value[0], 70); put(state, value);
}
static uint32_t call(uint32_t *input, uintptr_t target, uint32_t args[16]) {
  struct bb_call_state state = {input[7], input[6], input[5], input[8]};
  bb_qmix_call(&state, target, args, input[3] + 1796u, input[0], input[1],
               input[2], input[4], 0);
  input[7] = state.eax; input[6] = state.ecx;
  input[5] = state.edx; input[8] = state.flags;
  return input[7];
}
static uint16_t classify(const struct x87_state *state, unsigned index) {
  unsigned physical = (top(state) + index) & 7u;
  uint64_t mantissa; uint16_t encoded;
  memcpy(&mantissa, state->value[index], 8); memcpy(&encoded, state->value[index] + 8, 2);
  if (((state->environment[2] >> (physical * 2u)) & 3u) == 3u)
    return 0x4100 | ((encoded & 0x8000u) ? 0x0200 : 0);
  unsigned exponent = encoded & 0x7fff, integer = mantissa >> 63;
  uint16_t result;
  if (!exponent && !mantissa) result = 0x4000;
  else if (!exponent) result = 0x4400;
  else if (!integer) result = 0;
  else if (exponent == 0x7fff) result = mantissa == UINT64_C(0x8000000000000000) ? 0x0500 : 0x0100;
  else result = 0x0400;
  return result | ((encoded & 0x8000u) ? 0x0200 : 0);
}
static unsigned class_offset(uint16_t classification) {
  unsigned nibble = ((classification >> 14) & 1u) |
                    ((classification >> 7) & 2u) |
                    ((classification >> 7) & 4u) |
                    ((classification >> 7) & 8u);
  return U8(0x100204edu + nibble);
}
/* Calculation leaves also use EBP/EBX/ESI/EDI as inputs. The general CRT
 * boundary only supplies caller-saved registers, so retain this complete
 * register ABI until these remaining calculation bodies have been ported. */
static void __attribute__((naked, noinline, noipa)) leaf_call(
    uint32_t *input __attribute__((unused)), uintptr_t target __attribute__((unused))) {
  __asm__ volatile(
      "pushl %ebp;movl %esp,%ebp;pushl %ebx;pushl %esi;pushl %edi;"
      "pushl 8(%ebp);pushl 12(%ebp);movl 8(%ebp),%eax;"
      "pushl 32(%eax);popfl;movl (%eax),%edi;movl 4(%eax),%esi;"
      "movl 16(%eax),%ebx;movl 20(%eax),%edx;movl 24(%eax),%ecx;"
      "movl 8(%eax),%ebp;movl 28(%eax),%eax;call *(%esp);"
      "pushfl;pushal;movl 40(%esp),%eax;"
      "movl (%esp),%edx;movl %edx,(%eax);"
      "movl 4(%esp),%edx;movl %edx,4(%eax);"
      "movl 8(%esp),%edx;movl %edx,8(%eax);"
      "movl 16(%esp),%edx;movl %edx,16(%eax);"
      "movl 20(%esp),%edx;movl %edx,20(%eax);"
      "movl 24(%esp),%edx;movl %edx,24(%eax);"
      "movl 28(%esp),%edx;movl %edx,28(%eax);"
      "movl 32(%esp),%edx;movl %edx,32(%eax);"
      "leal 44(%esp),%esp;popl %edi;popl %esi;popl %ebx;popl %ebp;ret");
}
static void calculation(uint32_t *input, uintptr_t target) {
  if (bb_qmix_calculation_route_c(input, target)) return;
  switch (target) {
#define BB_QMIX_CALC(address, body, count, frame, floating)                    \
  case address:                                                              \
    if (!(count) && (frame) == 2048 && !(floating)) { body(input); return; }   \
    break;
    BB_QMIX_METHODS(BB_QMIX_CALC)
#undef BB_QMIX_CALC
  default: break;
  }
  leaf_call(input, BB_CODE(target));
}
static void dispatch(uint32_t *input, unsigned binary) {
  struct x87_state state; save(&state);
  uint32_t descriptor = input[5], frame = input[2];
  uint16_t control = U8(descriptor + 14u) == 5 ?
      (U16(frame - 0xa4u) | 0x200u) & ~0x100u : 0x133fu;
  control = (control & 0xff00u) | 0x3fu;
  U16(frame - 0xa2u) = control; BB_U32(frame - 0x94u) = descriptor;
  U8(frame - 0x90u) = 0;
  uint16_t first = classify(&state, 0), second = binary ? classify(&state, 1) : 0;
  unsigned offset = class_offset(first) + (binary ? class_offset(second) * 4u : 0);
  uint16_t status = (state.environment[1] & ~0x4700u) | (binary ? second : first);
  U16(frame - 0xa0u) = status; state.environment[1] = status;
  state.environment[0] = (state.environment[0] & 0xffff0000u) | control;
  input[6] = ((first & 0x200u) >> 7) | (binary ? (second & 0x200u) << 1 : 0);
  input[7] = offset; input[4] = descriptor + 16u + offset;
  uint32_t target = BB_U32(input[4]);
  restore(&state); calculation(input, target);
}
/* 0x100169a0..0x10016a07 and 0x10016a07..0x10016a93: descriptor routing. */
void bb_qmix_numeric_dispatch_unary_c(uint32_t *input) { dispatch(input, 0); }
void bb_qmix_numeric_dispatch_binary_c(uint32_t *input) { dispatch(input, 1); }
static void finalize(uint32_t *input) {
  struct x87_state state; save(&state);
  uint32_t frame = input[2]; uint16_t control = U16(frame - 0xa4u);
  if (BB_U32(0x1001f47cu)) goto done;
  long double value = number(state.value[0]); store(0x10020d20u, value);
  int32_t error = (int8_t)U8(frame - 0x90u);
  if (error == -1 || error == -2) {
    unsigned exponent = U16(0x10020d26u) & 0x7ff0u;
    if (!exponent) {
      error = 4; value = scalbnl(value, (int)scalar(0x1001ac18u));
      if (fabsl(value) < scalar(0x1001ac08u)) value *= scalar(0x1001ac28u);
    } else if (exponent == 0x7ff0u) {
      error = 3; value = scalbnl(value, (int)scalar(0x1001ac10u));
      if (fabsl(value) > scalar(0x1001ac00u)) value *= scalar(0x1001ac20u);
    } else error = 0;
  }
  uint16_t status;
  __asm__ volatile("fnstsw %0" : "=m"(status));
  state.environment[1] |= status & 0x3fu;
  if (!error && !(control & 0x20u) && (state.environment[1] & 0x20u)) error = 8;
  if (error) {
    uint32_t descriptor = BB_U32(frame - 0x94u), record = frame - 0x8eu;
    BB_U32(record) = (uint32_t)error; BB_U32(record + 4u) = descriptor + 1u;
    if (!U8(0x10020d28u)) {
      memcpy((void *)(uintptr_t)(record + 8u), (const void *)(uintptr_t)(frame + 8u), 8);
      if (U8(descriptor + 13u) != 1)
        memcpy((void *)(uintptr_t)(record + 16u), (const void *)(uintptr_t)(frame + 16u), 8);
    }
    store(record + 24u, value); pop(&state);
    /* C/library error helpers require an empty arithmetic stack. Retain the
     * caller's remaining live registers separately, while forwarding its
     * control and exception flags through the reporting boundary. */
    struct x87_state host = state;
    host.environment[1] &= ~0x3800u;
    host.environment[2] = (host.environment[2] & 0xffff0000u) | 0xffffu;
    restore(&host);
    uint32_t args[16] = {(uint32_t)(int32_t)(int8_t)U8(descriptor + 14u), record, frame - 0xa4u};
    call(input, 0x10017bd0u, args); save(&host);
    state.environment[1] = (state.environment[1] & ~0x3fu) | (host.environment[1] & 0x3fu);
    push(&state, scalar(record + 24u));
  }
done:
  state.environment[0] = (state.environment[0] & 0xffff0000u) | control;
  restore(&state);
}
/* 0x1001588e..0x10015895 and 0x10015895..0x100159c5: final reporting. */
void bb_qmix_numeric_result_finalize_memory_c(uint32_t *input) {
  U8(0x10020d28u) = 0; finalize(input);
}
void bb_qmix_numeric_result_finalize_c(uint32_t *input) { finalize(input); }

#define ENTRY(name)                                                            \
  uint32_t __attribute__((naked)) name##_entry(void) {                         \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;ret"); \
  }
ENTRY(bb_qmix_numeric_dispatch_unary)
ENTRY(bb_qmix_numeric_dispatch_binary)
ENTRY(bb_qmix_numeric_result_finalize_memory)
ENTRY(bb_qmix_numeric_result_finalize)
#undef ENTRY
