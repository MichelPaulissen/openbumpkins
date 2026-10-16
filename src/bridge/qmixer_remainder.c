/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <math.h>
#include <string.h>
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* Isolate ordinary C arithmetic from the live, temporary i386 x87 ABI.
 * Save images contain logical ST0..ST7 and physical two-bit register tags. */
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
static long double number(const void *bytes) {
  long double value = 0; memcpy(&value, bytes, 10); return value;
}
static void put(struct x87_state *state, unsigned index, long double value) {
  memcpy(state->value[index], &value, 10);
  uint64_t mantissa; uint16_t exponent;
  memcpy(&mantissa, &value, 8); memcpy(&exponent, (const uint8_t *)&value + 8, 2);
  exponent &= 0x7fff;
  unsigned tag = !exponent && !mantissa ? 1 :
                 !exponent || exponent == 0x7fff || !(mantissa >> 63) ? 2 : 0;
  unsigned shift = 2u * ((top(state) + index) & 7u);
  state->environment[2] = (state->environment[2] & ~(3u << shift)) | (tag << shift);
}
static void push(struct x87_state *state, long double value) {
  memmove(state->value[1], state->value[0], 70);
  unsigned next = (top(state) - 1u) & 7u;
  state->environment[1] = (state->environment[1] & ~0x3800u) | (next << 11);
  put(state, 0, value);
}
static long double remainder_value(struct x87_state *state, long double dividend,
                                   long double divisor, unsigned nearest) {
  int quotient = 0;
  long double result = remquol(dividend, divisor, &quotient);
  unsigned bits = (unsigned)(quotient < 0 ? -quotient : quotient) & 7u;
  if (!nearest) {
    long double truncated = fmodl(dividend, divisor);
    /* remquo supplies the nearest quotient's low bits. Truncation differs by
     * at most one, including negative quotients and exact halfway cases. */
    if (isfinite(result) && result != truncated) bits = (bits - 1u) & 7u;
    result = truncated;
  }
  state->environment[1] = (state->environment[1] & ~0x4700u) |
      ((bits & 1u) << 9) | ((bits & 2u) << 13) | ((bits & 4u) << 6);
  /* C completes the reduction even for a huge exponent difference. Clear C2
   * so original callers' reduction loops finish without further iterations. */
  return result;
}
static void arguments(uint32_t *input, unsigned nearest) {
  struct x87_state state; save(&state);
  uint32_t p = input[3] + 2056u;
  long double divisor = number((const void *)(uintptr_t)p);
  long double dividend = number((const void *)(uintptr_t)(p + 24u));
  long double result = remainder_value(&state, dividend, divisor, nearest);
  /* Internal subnormal adapters can scale both operands by 2^64 and keep an
   * unscaled divisor in the second twelve-byte slot. */
  if (input[5] & 1u) result = scalbnl(result, -64);
  if (input[5] & 2u) divisor = number((const void *)(uintptr_t)(p + 12u));
  push(&state, divisor); push(&state, result); restore(&state);
}
static void registers(uint32_t *input, unsigned nearest) {
  (void)input;
  struct x87_state state; save(&state);
  long double result = remainder_value(&state, number(state.value[0]),
                                       number(state.value[1]), nearest);
  put(&state, 0, result); restore(&state);
}
/* 0x10017646..0x1001784c and 0x100178fe..0x10017b04: expanded arguments. */
void bb_qmix_remainder_arguments_c(uint32_t *input) { arguments(input, 0); }
void bb_qmix_remainder_nearest_arguments_c(uint32_t *input) { arguments(input, 1); }
/* 0x1001784c..0x100178fe and 0x10017b04..0x10017bb9: register adapters. */
void bb_qmix_remainder_register_c(uint32_t *input) { registers(input, 0); }
void bb_qmix_remainder_nearest_register_c(uint32_t *input) { registers(input, 1); }
/* 0x10017bb9..0x10017bc5: the two public reduction aliases. */
void bb_qmix_remainder_alias_c(uint32_t *input) { registers(input, 0); }
void bb_qmix_remainder_nearest_alias_c(uint32_t *input) { registers(input, 1); }

#define ARG(n) uint32_t a##n __attribute__((unused))
#define ENTRY(name, signature)                                                 \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;ret"); \
  }
ENTRY(bb_qmix_remainder_arguments, (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8)))
ENTRY(bb_qmix_remainder_nearest_arguments, (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8)))
ENTRY(bb_qmix_remainder_register, (void))
ENTRY(bb_qmix_remainder_nearest_register, (void))
ENTRY(bb_qmix_remainder_alias, (void))
ENTRY(bb_qmix_remainder_nearest_alias, (void))
#undef ENTRY
#undef ARG
