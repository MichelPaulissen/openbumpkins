/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <string.h>
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

static uint32_t stack(const uint32_t *input) { return input[3] + 2052u; }
static uint32_t arg(const uint32_t *input, unsigned n) {
  return BB_U32(stack(input) + 4u * (n + 1u));
}
static uint64_t bits(uint32_t p) {
  uint64_t value; memcpy(&value, (const void *)(uintptr_t)p, 8); return value;
}
static void store(uint32_t p, uint64_t value) {
  memcpy((void *)(uintptr_t)p, &value, 8);
}
static uint32_t call(uint32_t *input, uintptr_t target, uint32_t args[16]) {
  struct bb_call_state state = {input[7], input[6], input[5], input[8]};
  bb_qmix_call(&state, target, args, stack(input) - 256u, input[0], input[1],
               input[2], input[4], 0);
  input[7] = state.eax; input[6] = state.ecx;
  input[5] = state.edx; input[8] = state.flags;
  return input[7];
}
/* Temporary x87 ABI isolation: logical ST order, physical register tags. */
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
static void push(struct x87_state *state, const void *value) {
  unsigned next = (top(state) - 1u) & 7u;
  state->environment[1] = (state->environment[1] & ~0x3800u) | (next << 11);
  memmove(state->value[1], state->value[0], 70); memcpy(state->value[0], value, 10);
  uint64_t mantissa; uint16_t exponent;
  memcpy(&mantissa, value, 8); memcpy(&exponent, (const uint8_t *)value + 8, 2);
  exponent &= 0x7fff;
  unsigned tag = !exponent && !mantissa ? 1 :
                 !exponent || exponent == 0x7fff || !(mantissa >> 63) ? 2 : 0;
  state->environment[2] = (state->environment[2] & ~(3u << (next * 2u))) |
                         (tag << (next * 2u));
}
static void pop(struct x87_state *state) {
  unsigned old = top(state), next = (old + 1u) & 7u;
  state->environment[2] |= 3u << (2u * old);
  state->environment[1] = (state->environment[1] & ~0x3800u) | (next << 11);
  uint8_t first[10]; memcpy(first, state->value[0], 10);
  memmove(state->value[0], state->value[1], 70); memcpy(state->value[7], first, 10);
}
/* 0x100159f1..0x10015a2d: load doubles, including private NaN packing. */
void bb_qmix_math_load_double_c(uint32_t *input) {
  struct x87_state state; save(&state);
  uint64_t raw = bits(stack(input) + 4u), mantissa = raw & UINT64_C(0xfffffffffffff);
  uint16_t sign = (uint16_t)((raw >> 48) & 0x8000u), encoded = (raw >> 52) & 0x7ffu;
  int exponent;
  if (encoded == 0x7ffu) {
    uint32_t high = (uint32_t)(raw >> 32), low = (uint32_t)raw;
    mantissa = ((uint64_t)((high << 11) | (low >> 21)) << 32) | low;
    exponent = 0x7fff;
  } else if (!encoded && !mantissa) exponent = 0;
  else {
    exponent = encoded ? (int)encoded - 1023 : -1022;
    if (encoded) mantissa |= UINT64_C(1) << 52;
    else while (!(mantissa & (UINT64_C(1) << 52))) { mantissa <<= 1; --exponent; }
    mantissa <<= 11; exponent += 16383;
  }
  uint8_t value[10]; uint16_t header = sign | (uint16_t)exponent;
  memcpy(value, &mantissa, 8); memcpy(value + 8, &header, 2);
  push(&state, value); restore(&state);
}
static unsigned is_nan(uint64_t value) {
  return (value & UINT64_C(0x7fffffffffffffff)) > UINT64_C(0x7ff0000000000000);
}
static uint32_t power_special(uint32_t *input, uint64_t x, uint64_t y, uint32_t out) {
  uint64_t magnitude = x & UINT64_C(0x7fffffffffffffff);
  const uint64_t infinity = UINT64_C(0x7ff0000000000000);
  if ((y & UINT64_C(0x7fffffffffffffff)) == infinity) {
    unsigned greater = magnitude > UINT64_C(0x3ff0000000000000) || is_nan(x);
    unsigned less = magnitude < UINT64_C(0x3ff0000000000000);
    if (!greater && !less) { store(out, bits(0x1001fe00u)); return 1; }
    store(out, greater == !(y >> 63) ? bits(0x1001fdf8u) : 0); return 0;
  }
  if (magnitude == infinity) {
    unsigned odd = 0;
    if (x >> 63) {
      uint32_t args[16] = {(uint32_t)y, (uint32_t)(y >> 32)};
      odd = call(input, 0x10016dc0u, args) == 1;
    }
    uint64_t absolute_y = y & UINT64_C(0x7fffffffffffffff), result;
    if ((!(y >> 63) && absolute_y) || is_nan(y))
      result = bits(0x1001fdf8u) | (odd ? UINT64_C(1) << 63 : 0);
    else if (absolute_y) result = odd ? bits(0x1001fe18u) : 0;
    else result = UINT64_C(0x3ff0000000000000);
    store(out, result);
  }
  return 0;
}
/* 0x10016b70..0x10016db8: power result for infinite operands. Ordinary
 * finite inputs intentionally leave the output untouched. */
void bb_qmix_math_power_special_c(uint32_t *input) {
  struct x87_state state; save(&state);
  uint64_t x = bits(stack(input) + 4u), y = bits(stack(input) + 12u);
  input[7] = power_special(input, x, y, arg(input, 4)); restore(&state);
}
/* 0x100157a5..0x100157d7: two live extended operands become one result. */
void bb_qmix_math_power_register_c(uint32_t *input) {
  struct x87_state state; save(&state);
  long double x = 0, y = 0;
  memcpy(&x, state.value[1], 10); memcpy(&y, state.value[0], 10);
  double dx = (double)x, dy = (double)y; uint64_t bx, by;
  memcpy(&bx, &dx, 8); memcpy(&by, &dy, 8);
  uint32_t out = stack(input) - 120u;
  uint32_t error = power_special(input, bx, by, out);
  uint64_t raw = bits(out); double result; memcpy(&result, &raw, 8);
  long double value = result; pop(&state); pop(&state); push(&state, &value);
  if (error) *(volatile uint8_t *)(uintptr_t)(input[2] - 0x90u) = 1;
  input[7] = error; restore(&state);
}
/* 0x10017bd0..0x10017ccf: mutable _exception records, FPIEEE reporting,
 * caller control restoration, optional matherr and errno routing. */
void bb_qmix_math_record_error_c(uint32_t *input) {
  uint32_t operation = arg(input, 0), record = arg(input, 1), control = arg(input, 2);
  uint32_t cw = *(volatile uint16_t *)(uintptr_t)control, flags = 0;
  switch (BB_U32(record)) {
  case 1: case 5: flags = 8; break;
  case 2: flags = 4; break;
  case 3: flags = 0x11; break;
  case 4: flags = 0x12; break;
  case 7: BB_U32(record) = 1; break;
  case 8: flags = 0x10; break;
  }
  uint32_t args[16] = {flags, record + 24u, cw};
  if (flags && !call(input, 0x10013bd0u, args)) {
    uint32_t p = stack(input) - 88u, saved_control = p - 4u;
    BB_U32(saved_control) = cw;
    if (operation == 0x10 || operation == 0x16 || operation == 0x1d) {
      BB_U32(p + 56u) = (BB_U32(p + 56u) & ~0x1cu) | 3u;
      store(p + 40u, bits(record + 16u));
    } else BB_U32(p + 56u) &= ~1u;
    args[0] = p; args[1] = saved_control; args[2] = flags;
    args[3] = operation; args[4] = record + 8u; args[5] = record + 24u;
    call(input, 0x10013860u, args);
    cw = BB_U32(saved_control);
  }
  args[0] = cw; args[1] = 0xffff; call(input, 0x10014020u, args);
  unsigned handled = 0;
  if (BB_U32(record) != 8 && !BB_U32(0x10020250u)) {
    args[0] = record; handled = call(input, 0x10015dc0u, args);
  }
  if (!handled) { args[0] = BB_U32(record); call(input, 0x10013f40u, args); }
  else input[7] = handled;
}

#define ARG(n) uint32_t a##n __attribute__((unused))
#define ENTRY(name, signature)                                                 \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;ret"); \
  }
ENTRY(bb_qmix_math_load_double, (ARG(0), ARG(1)))
ENTRY(bb_qmix_math_power_special, (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4)))
ENTRY(bb_qmix_math_power_register, (void))
ENTRY(bb_qmix_math_record_error, (ARG(0), ARG(1), ARG(2)))
#undef ENTRY
#undef ARG
