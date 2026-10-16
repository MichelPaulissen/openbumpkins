/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <string.h>
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* Temporary x87 ABI state, in the documented 32-bit FNSAVE/FRSTOR format.
 * Registers are in logical ST order; two-bit tags are in physical order.
 * Arithmetic, selector dispatch and stack updates are C, without original
 * instruction bodies or instruction decoding. */
struct x87_state { uint32_t environment[7]; uint8_t value[8][10]; };
_Static_assert(sizeof(struct x87_state) == 108, "i386 x87 save image");
static uint32_t stack(const uint32_t *input) { return input[3] + 2052u; }
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
static unsigned empty(const struct x87_state *state, unsigned index) {
  return ((state->environment[2] >> (2u * ((top(state) + index) & 7u))) & 3u) == 3;
}
static long double number(const uint8_t bytes[10]) {
  long double value = 0; memcpy(&value, bytes, 10); return value;
}
static void put(struct x87_state *state, unsigned index, long double value) {
  memcpy(state->value[index], &value, 10);
  uint64_t mantissa; uint16_t exponent;
  memcpy(&mantissa, state->value[index], 8);
  memcpy(&exponent, state->value[index] + 8, 2); exponent &= 0x7fff;
  unsigned tag = !exponent && !mantissa ? 1 :
                 !exponent || exponent == 0x7fff || !(mantissa >> 63) ? 2 : 0;
  unsigned shift = 2u * ((top(state) + index) & 7u);
  state->environment[2] = (state->environment[2] & ~(3u << shift)) | (tag << shift);
}
static long double indefinite(void) {
  static const uint8_t value[10] = {0,0,0,0,0,0,0,0xc0,0xff,0xff};
  return number(value);
}
static void pop(struct x87_state *state) {
  unsigned old = top(state), next = (old + 1u) & 7u;
  state->environment[2] |= 3u << (2u * old);
  state->environment[1] = (state->environment[1] & ~0x3800u) | (next << 11);
  uint8_t first[10]; memcpy(first, state->value[0], 10);
  memmove(state->value[0], state->value[1], 70);
  memcpy(state->value[7], first, 10);
}
static void registers(struct x87_state *state, unsigned selector) {
  unsigned index = selector >> 3, operation = selector & 7u;
  /* The original two unsupported encodings per register execute INT 6. Keep
   * that trap at the temporary ABI rather than interpreting any executable. */
  if (operation == 1 || operation == 3) { __asm__ volatile("int $6"); return; }
  unsigned destination = operation < 4 ? 0 : index;
  long double result;
  if (empty(state, 0) || empty(state, index)) result = indefinite();
  else {
    long double first = number(state->value[0]), other = number(state->value[index]);
    result = operation == 0 || operation >= 6 ? first / other : other / first;
  }
  put(state, destination, result);
  if (operation == 5 || operation == 7) pop(state);
}
/* 0x10016f57..0x100173f6: all 64 register selectors, including pop forms. */
void bb_qmix_divide_register_dispatch_c(uint32_t *input) {
  struct x87_state state; save(&state);
  unsigned selector = input[7] & 63u; registers(&state, selector);
  input[7] = selector; restore(&state);
}
static void pair(uint32_t *input, unsigned reverse) {
  (void)input;
  struct x87_state state; save(&state);
  registers(&state, 8u | (reverse ? 7u : 5u)); restore(&state);
}
void bb_qmix_divide_pair_c(uint32_t *input) { pair(input, 0); }
void bb_qmix_divide_pair_reverse_c(uint32_t *input) { pair(input, 1); }
void bb_qmix_divide_pair_preserved_c(uint32_t *input) { pair(input, 0); }
void bb_qmix_divide_pair_reverse_preserved_c(uint32_t *input) { pair(input, 1); }
static void memory(uint32_t *input, unsigned kind, unsigned reverse) {
  struct x87_state state; save(&state);
  uint32_t p = stack(input) + 4u;
  long double operand, result;
  if (kind == 0) { float value; memcpy(&value, (const void *)(uintptr_t)p, 4); operand = value; }
  else if (kind == 1) { double value; memcpy(&value, (const void *)(uintptr_t)p, 8); operand = value; }
  else if (kind == 2) { int16_t value; memcpy(&value, (const void *)(uintptr_t)p, 2); operand = value; }
  else { int32_t value; memcpy(&value, (const void *)(uintptr_t)p, 4); operand = value; }
  if (empty(&state, 0)) result = indefinite();
  else {
    long double value = number(state.value[0]);
    result = reverse ? operand / value : value / operand;
  }
  put(&state, 0, result); restore(&state);
}
#define MEMORY(name, kind, reverse) \
  void name##_c(uint32_t *input) { memory(input, kind, reverse); }
MEMORY(bb_qmix_divide_memory_float, 0, 0)
MEMORY(bb_qmix_divide_memory_double, 1, 0)
MEMORY(bb_qmix_divide_memory_word, 2, 0)
MEMORY(bb_qmix_divide_memory_integer, 3, 0)
MEMORY(bb_qmix_divide_memory_float_reverse, 0, 1)
MEMORY(bb_qmix_divide_memory_double_reverse, 1, 1)
MEMORY(bb_qmix_divide_memory_word_reverse, 2, 1)
MEMORY(bb_qmix_divide_memory_integer_reverse, 3, 1)
#undef MEMORY

#define ARG(n) uint32_t a##n __attribute__((unused))
#define ENTRY(name, signature, cleanup)                                        \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;" cleanup); \
  }
ENTRY(bb_qmix_divide_register_dispatch, (void), "ret")
ENTRY(bb_qmix_divide_pair, (void), "ret")
ENTRY(bb_qmix_divide_pair_reverse, (void), "ret")
ENTRY(bb_qmix_divide_pair_preserved, (void), "ret")
ENTRY(bb_qmix_divide_pair_reverse_preserved, (void), "ret")
ENTRY(bb_qmix_divide_memory_float, (ARG(0)), "ret $4")
ENTRY(bb_qmix_divide_memory_double, (ARG(0), ARG(1)), "ret $8")
ENTRY(bb_qmix_divide_memory_word, (ARG(0)), "ret $4")
ENTRY(bb_qmix_divide_memory_integer, (ARG(0)), "ret $4")
ENTRY(bb_qmix_divide_memory_float_reverse, (ARG(0)), "ret $4")
ENTRY(bb_qmix_divide_memory_double_reverse, (ARG(0), ARG(1)), "ret $8")
ENTRY(bb_qmix_divide_memory_word_reverse, (ARG(0)), "ret $4")
ENTRY(bb_qmix_divide_memory_integer_reverse, (ARG(0)), "ret $4")
#undef ENTRY
#undef ARG
