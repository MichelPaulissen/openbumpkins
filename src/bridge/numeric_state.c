/* SPDX-License-Identifier: GPL-3.0-only */
#include "numeric_state.h"
#include "address.h"
#include "model_runtime.h"
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static _Thread_local uint16_t control = 0x037f;
uint16_t bb_numeric_word(void) { return control; }
void bb_numeric_set_word(uint16_t value) {
  value = (value & 0x1f3fu) | 0x0040u;
  if ((value ^ control) & 0x0c00u) {
    static const int rounding[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD,
                                   FE_TOWARDZERO};
    if (fesetround(rounding[(value >> 10) & 3]))
      bb_model_raise(bb_model_current_identity());
  }
  control = value;
}
uint16_t bb_numeric_status(void) {
  int flags = fetestexcept(FE_ALL_EXCEPT);
  return (flags & FE_INVALID ? 1u : 0u) | (flags & FE_DIVBYZERO ? 4u : 0u) |
         (flags & FE_OVERFLOW ? 8u : 0u) | (flags & FE_UNDERFLOW ? 16u : 0u) |
         (flags & FE_INEXACT ? 32u : 0u);
}
static struct bb_numeric_value *operand(unsigned index) {
  struct bb_numeric_operands *operands = bb_model_operands();
  if (index >= operands->count) {
    fputs("bumpkins: missing C numeric operand\n", stderr);
    abort();
  }
  return &operands->values[operands->count - 1 - index];
}
void bb_numeric_push(long double value) {
  struct bb_numeric_operands *operands = bb_model_operands();
  if (operands->count == 8) {
    fputs("bumpkins: too many C numeric operands\n", stderr);
    abort();
  }
  operands->values[operands->count++] = (struct bb_numeric_value){value, 0};
}
void bb_numeric_push_bits(uint64_t bits) {
  double value;
  memcpy(&value, &bits, sizeof value);
  bb_numeric_push(value);
  operand(0)->signaling =
      (bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000) &&
      (bits & UINT64_C(0x000fffffffffffff)) &&
      !(bits & UINT64_C(0x0008000000000000));
}
long double bb_numeric_get(unsigned index) { return operand(index)->value; }
unsigned bb_numeric_signaling(unsigned index) {
  return operand(index)->signaling;
}
void bb_numeric_set(unsigned index, long double value) {
  *operand(index) = (struct bb_numeric_value){value, 0};
}
long double bb_numeric_pop(void) {
  long double value = bb_numeric_get(0);
  --bb_model_operands()->count;
  return value;
}
void bb_numeric_exchange(void) {
  struct bb_numeric_value value = *operand(0);
  *operand(0) = *operand(1);
  *operand(1) = value;
}
long double bb_numeric_read_extended(uintptr_t p) {
  uint64_t fraction = BB_U64(p);
  uint16_t word = BB_U16(p + 8), exponent = word & 0x7fff;
  long double value;
  if (exponent == 0x7fff)
    value = (fraction & UINT64_C(0x7fffffffffffffff)) ? NAN : INFINITY;
  else if (exponent && !(fraction & UINT64_C(0x8000000000000000)))
    value = NAN;
  else
    value = ldexpl((long double)fraction, (exponent ? exponent : 1) - 16446);
  return word & 0x8000 ? -value : value;
}
void bb_numeric_store_extended(uintptr_t p, unsigned index) {
  long double value = bb_numeric_get(index), magnitude = fabsl(value);
  uint64_t fraction = 0;
  uint16_t word = signbit(value) ? 0x8000 : 0;
  if (isnan(value)) {
    word |= 0x7fff;
    fraction = bb_numeric_signaling(index) ? UINT64_C(0xa000000000000000)
                                           : UINT64_C(0xc000000000000000);
  } else if (isinf(value)) {
    word |= 0x7fff;
    fraction = UINT64_C(0x8000000000000000);
  } else if (magnitude) {
    int exponent;
    long double part = frexpl(magnitude, &exponent);
    int biased = exponent + 16382;
    fraction = (uint64_t)ldexpl(part, biased > 0 ? 64 : 63 + biased);
    if (biased > 0)
      word |= biased;
  }
  BB_U64(p) = fraction;
  BB_U16(p + 8) = word;
}
