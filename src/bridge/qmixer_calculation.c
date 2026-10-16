/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <errno.h>
#include <math.h>
#include <string.h>
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
#define U8(p) BB_U8(p)

/* Host C math and explicit stack/result routing replace the calculation
 * leaves. Extended register snapshots are temporary i386 ABI glue. */
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
static unsigned top(const struct x87_state *state) {
  return (state->environment[1] >> 11) & 7u;
}
static long double number(const void *p) {
  long double value = 0; memcpy(&value, p, 10); return value;
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
static void replace(struct x87_state *state, unsigned count, long double value) {
  while (count--) pop(state);
  push(state, value);
}
static void error(uint32_t *input, int value) {
  U8(input[2] - 0x90u) = (uint8_t)value;
}
static unsigned parity(long double value) {
  if (!isfinite(value)) return 2;
  if (nearbyintl(value) != value) return 0;
  long double half = value * 0.5L;
  return nearbyintl(half) == half ? 2 : 1;
}
static void domain(uint32_t *input, struct x87_state *state, unsigned count) {
  replace(state, count, number((const void *)0x100204d0u));
  if ((int8_t)U8(input[2] - 0x90u) <= 0) error(input, 1);
}
static void test_sign(uint32_t *input) {
  unsigned value = input[6] & 0xffu, folded = value ^ (value >> 4);
  input[8] = (input[8] & ~0x8d5u) | (!value ? 0x40u : 0) |
             (value & 0x80u) | (((0x9669u >> (folded & 15u)) & 1u) << 2);
}
/* 0x100155d0..0x100155e1: normal finite power, including negative bases.
 * The exponent is ST0, the base ST1. -2 selects common range finalization. */
void bb_qmix_calculation_power_c(uint32_t *input) {
  struct x87_state state; save(&state);
  long double exponent = number(state.value[0]), base = number(state.value[1]);
  error(input, -2);
  if (input[6] & 0xff00u) {
    unsigned kind = parity(exponent); input[7] = kind;
    if (!kind) { domain(input, &state, 2); goto done; }
    input[6] = (input[6] & ~0xff00u) | (kind == 1 ? 0xff00u : 0);
    base = fabsl(base);
  }
  int saved_errno = errno;
  long double scaled_exponent = exponent * log2l(base);
  long double result = powl(base, exponent);
  if (scaled_exponent < -32767.5L) { error(input, 4); result = 0.L; }
  else {
    if (scaled_exponent > 32767.5L) result = number((const void *)0x10020150u);
    if (input[6] & 0xff00u) result = -result;
  }
  errno = saved_errno;
  replace(&state, 2, result);
done:
  restore(&state);
}
/* 0x100155e1..0x10015648: exponential and the shared old range algorithm.
 * C libm computes the result directly; the existing finalizer reports range. */
void bb_qmix_calculation_exp_c(uint32_t *input) {
  struct x87_state state; save(&state); error(input, -2);
  input[6] &= ~0xff00u;
  int saved_errno = errno;
  long double value = number(state.value[0]), result = expl(value);
  long double scaled_exponent = value / logl(2.L);
  if (scaled_exponent < -32767.5L) { error(input, 4); result = 0.L; }
  else if (scaled_exponent > 32767.5L) result = number((const void *)0x10020150u);
  errno = saved_errno;
  put(&state, result); restore(&state);
}
void bb_qmix_calculation_power_singularity_c(uint32_t *input) {
  struct x87_state state; save(&state); error(input, 2);
  replace(&state, 2, number((const void *)0x10020150u)); restore(&state);
}
/* 0x1001565a..0x10015674: checked natural logarithm. */
void bb_qmix_calculation_log_checked_c(uint32_t *input) {
  struct x87_state state; save(&state);
  long double value = number(state.value[0]);
  if (!(value > 0)) domain(input, &state, 1);
  else { int saved_errno = errno; put(&state, logl(value)); errno = saved_errno; }
  restore(&state);
}
void bb_qmix_calculation_log_zero_c(uint32_t *input) {
  struct x87_state state; save(&state); error(input, 2);
  put(&state, number((const void *)0x1002015au)); restore(&state);
}
void bb_qmix_calculation_log_infinity_c(uint32_t *input) {
  struct x87_state state; save(&state); test_sign(input);
  if (input[6] & 0xffu) domain(input, &state, 1);
  restore(&state);
}
/* 0x1001568f..0x10015698: shared logarithm calculation. Tiny old entry
 * prefixes supply log10(2) or ln(2) in ST0, with the argument in ST1. */
void bb_qmix_calculation_log_scaled_c(uint32_t *input) {
  struct x87_state state; save(&state); test_sign(input);
  if (input[6] & 0xffu) domain(input, &state, 2);
  else {
    int saved_errno = errno;
    long double value = log2l(number(state.value[1])) * number(state.value[0]);
    errno = saved_errno; replace(&state, 2, value);
  }
  restore(&state);
}
void bb_qmix_calculation_power_one_c(uint32_t *input) {
  (void)input;
  struct x87_state state; save(&state); replace(&state, 2, 1.L); restore(&state);
}
void bb_qmix_calculation_power_zero_c(uint32_t *input) {
  struct x87_state state; save(&state);
  unsigned kind = parity(number(state.value[0])); input[7] = kind;
  test_sign(input);
  long double result = (input[6] & 0xffu) ? number((const void *)0x10020150u) : 0.L;
  if (input[6] & 0xffu) error(input, 2);
  if (kind == 1 && (input[6] & 0xff00u)) result = -result;
  replace(&state, 2, result); restore(&state);
}
void bb_qmix_calculation_power_underflow_c(uint32_t *input) {
  (void)input;
  struct x87_state state; save(&state); replace(&state, 2, 0.L); restore(&state);
}
void bb_qmix_calculation_domain_c(uint32_t *input) {
  struct x87_state state; save(&state); domain(input, &state, 1);
  test_sign(input); restore(&state);
}
void bb_qmix_calculation_domain_record_c(uint32_t *input) {
  bb_qmix_calculation_domain_c(input);
}
void bb_qmix_calculation_power_overflow_c(uint32_t *input) {
  struct x87_state state; save(&state); error(input, 3);
  replace(&state, 2, number((const void *)0x10020150u)); restore(&state);
}
void bb_qmix_calculation_exp_infinity_c(uint32_t *input) {
  struct x87_state state; save(&state); test_sign(input);
  put(&state, (input[6] & 0xffu) ? 0.L : number((const void *)0x10020150u));
  restore(&state);
}
void bb_qmix_calculation_parity_c(uint32_t *input) {
  struct x87_state state; save(&state);
  input[7] = parity(number(state.value[0])); restore(&state);
}
static unsigned quiet(const struct x87_state *state, unsigned index) {
  return !!(state->value[index][7] & 0x40u);
}
static void capture(uint32_t *input, const struct x87_state *state, unsigned index) {
  memcpy((void *)(uintptr_t)(input[2] - 0x9eu), state->value[index], 10);
}
void bb_qmix_calculation_nan_unary_c(uint32_t *input) {
  struct x87_state state; save(&state); capture(input, &state, 0);
  error(input, quiet(&state, 0) ? 7 : 1);
  if (!quiet(&state, 0)) put(&state, number(state.value[0]) + 1.L);
  restore(&state);
}
void bb_qmix_calculation_nan_binary_c(uint32_t *input) {
  struct x87_state state; save(&state); capture(input, &state, 0);
  error(input, quiet(&state, 0) ? 7 : 1);
  long double result = number(state.value[0]) + number(state.value[1]);
  replace(&state, 2, result); restore(&state);
}
void bb_qmix_calculation_nan_pair_c(uint32_t *input) {
  struct x87_state state; save(&state); capture(input, &state, 0);
  unsigned both = quiet(&state, 0) && quiet(&state, 1);
  if (quiet(&state, 0)) capture(input, &state, 1);
  error(input, both ? 7 : 1);
  long double result = number(state.value[0]) + number(state.value[1]);
  replace(&state, 2, result); restore(&state);
}
void bb_qmix_calculation_mark_domain_c(uint32_t *input) {
  error(input, 1); test_sign(input);
}
void bb_qmix_calculation_negate_c(uint32_t *input) {
  struct x87_state state; save(&state); test_sign(input);
  if (input[6] & 0xffu) state.value[0][9] ^= 0x80u;
  restore(&state);
}

/* Tiny descriptor targets share original instruction ranges with other
 * entries. Route their complete behavior in C before any executable jump. */
unsigned bb_qmix_calculation_route_c(uint32_t *input, uintptr_t target) {
  switch (target) {
  case 0x10015689u: case 0x1001568du: {
    struct x87_state state; save(&state); test_sign(input);
    if (input[6] & 0xffu) domain(input, &state, 1);
    else {
      int saved_errno = errno;
      long double value = number(state.value[0]);
      put(&state, target == 0x10015689u ? log10l(value) : logl(value));
      errno = saved_errno;
    }
    restore(&state); return 1;
  }
  case 0x1001563eu: case 0x10016aabu:
    bb_qmix_calculation_power_one_c(input); return 1;
  case 0x10016aadu: {
    struct x87_state state; save(&state); put(&state, 1.L); restore(&state); return 1;
  }
  case 0x10016addu: {
    struct x87_state state; save(&state); capture(input, &state, 1);
    error(input, quiet(&state, 1) ? 7 : 1);
    long double value = number(state.value[0]) + number(state.value[1]);
    replace(&state, 2, value); restore(&state); return 1;
  }
  default: return 0;
  }
}

#define ENTRY(name)                                                            \
  uint32_t __attribute__((naked)) name##_entry(void) {                         \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;ret"); \
  }
ENTRY(bb_qmix_calculation_power)
ENTRY(bb_qmix_calculation_exp)
ENTRY(bb_qmix_calculation_power_singularity)
ENTRY(bb_qmix_calculation_log_checked)
ENTRY(bb_qmix_calculation_log_zero)
ENTRY(bb_qmix_calculation_log_infinity)
ENTRY(bb_qmix_calculation_log_scaled)
ENTRY(bb_qmix_calculation_power_one)
ENTRY(bb_qmix_calculation_power_zero)
ENTRY(bb_qmix_calculation_power_underflow)
ENTRY(bb_qmix_calculation_domain)
ENTRY(bb_qmix_calculation_domain_record)
ENTRY(bb_qmix_calculation_power_overflow)
ENTRY(bb_qmix_calculation_exp_infinity)
ENTRY(bb_qmix_calculation_parity)
ENTRY(bb_qmix_calculation_nan_unary)
ENTRY(bb_qmix_calculation_nan_binary)
ENTRY(bb_qmix_calculation_nan_pair)
ENTRY(bb_qmix_calculation_mark_domain)
ENTRY(bb_qmix_calculation_negate)
#undef ENTRY
