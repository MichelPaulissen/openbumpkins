/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_math_base.h"
#include "abi.h"
#include "interop.h"
#include <stddef.h>
#ifdef BB_MODEL_RUNTIME
#include "numeric_state.h"
#include <fenv.h>
#include <math.h>
#include <string.h>
#endif
static uint64_t bits(uint32_t low, uint32_t high) {
  return low | ((uint64_t)high << 32);
}
static uint16_t read_word(void) {
#ifdef BB_MODEL_RUNTIME
  return bb_numeric_word();
#else
  uint16_t value;
  __asm__ volatile("fstcw %0" : "=m"(value));
  return value;
#endif
}
static void write_word(uint16_t value) {
#ifdef BB_MODEL_RUNTIME
  bb_numeric_set_word(value);
#else
  __asm__ volatile("fldcw %0" ::"m"(value));
#endif
}
#ifdef BB_MODEL_RUNTIME
uint64_t bb_crt_math_integer_entry(void) {
  long double value = bb_numeric_pop();
  if (!isfinite(value) || value < -0x1p63L || value >= 0x1p63L) {
    feraiseexcept(FE_INVALID);
    return UINT64_C(0x8000000000000000);
  }
  return (uint64_t)(int64_t)value;
}
#else
uint64_t bb_crt_math_integer_entry(void) {
  uint16_t old = read_word(), truncate = old | 0xc00;
  uint64_t result;
  __asm__ volatile("fwait");
  write_word(truncate);
  __asm__ volatile("fistpq %0" : "=m"(result)::"st");
  write_word(old);
  return result;
}
#endif
uint64_t BB_STDCALL bb_crt_math_divide_entry(uint32_t a, uint32_t b, uint32_t c,
                                             uint32_t d) {
  return bits(a, b) / bits(c, d);
}
uint64_t BB_STDCALL bb_crt_math_remainder_entry(uint32_t a, uint32_t b,
                                                uint32_t c, uint32_t d) {
  return bits(a, b) % bits(c, d);
}
uint32_t bb_crt_math_decode_entry(uint32_t value) {
  static const uint32_t masks[] = {0x10, 0x80000, 8, 4, 2, 1};
  uint32_t result = 0;
  for (unsigned i = 0; i < 6; ++i)
    if (value & (1u << i))
      result |= masks[i];
  result |= (value & 0xc00) >> 2;
  if (!(value & 0x300))
    result |= 0x20000;
  else if ((value & 0x300) == 0x200)
    result |= 0x10000;
  if (value & 0x1000)
    result |= 0x40000;
  return result;
}
uint32_t bb_crt_math_encode_entry(uint32_t value) {
  static const uint32_t masks[] = {0x10, 0x80000, 8, 4, 2, 1};
  uint32_t result = 0;
  for (unsigned i = 0; i < 6; ++i)
    if (value & masks[i])
      result |= 1u << i;
  result |= (value & 0x300) << 2;
  if (!(value & 0x30000))
    result |= 0x300;
  else if ((value & 0x30000) == 0x10000)
    result |= 0x200;
  if (value & 0x40000)
    result |= 0x1000;
  /* The private helper writes only AX at return after testing precision. */
  else if ((value & 0x30000) >= 0x20000)
    result |= value & 0x30000;
  return result;
}
uint32_t bb_crt_math_control_entry(uint32_t value, uint32_t mask) {
  uint32_t old = bb_crt_math_decode_entry(read_word());
  uint32_t merged = (old & ~mask) | (value & mask);
  write_word(bb_crt_math_encode_entry(merged));
  return merged;
}
uint32_t bb_crt_math_control_safe_entry(uint32_t value, uint32_t mask) {
  return bb_crt_math_control_entry(value, mask & ~0x80000u);
}
int32_t bb_crt_math_status_entry(void) {
#ifdef BB_MODEL_RUNTIME
  return bb_numeric_status();
#else
  uint16_t value;
  __asm__ volatile("fstsw %0" : "=m"(value));
  return (int16_t)value;
#endif
}
int32_t bb_crt_math_clear_entry(void) {
#ifdef BB_MODEL_RUNTIME
  uint16_t value = bb_numeric_status();
  feclearexcept(FE_ALL_EXCEPT);
#else
  uint16_t value;
  __asm__ volatile("fnstsw %0;fnclex" : "=m"(value));
#endif
  return (int16_t)value;
}
int32_t bb_crt_math_word_entry(uint32_t value, uint32_t mask) {
  uint16_t old = read_word();
  write_word((old & ~mask) | (value & mask));
  return (int16_t)old;
}
static uint64_t rounded_bits(uint64_t raw) {
  uint64_t out;
#ifdef BB_MODEL_RUNTIME
  double value;
  memcpy(&value, &raw, sizeof value);
  /* Explicit operations also work with compilers assuming default rounding
   * when optimizing rint. They retain the requested integer-rounding intent. */
  switch (bb_numeric_word() & 0x0c00u) {
  case 0x0400:
    value = floor(value);
    break;
  case 0x0800:
    value = ceil(value);
    break;
  case 0x0c00:
    value = trunc(value);
    break;
  default:
    value = rint(value);
    break;
  }
  if (value == 0)
    return raw & UINT64_C(0x8000000000000000);
  memcpy(&out, &value, sizeof out);
#else
  __asm__ volatile("fldl %1;frndint;fstpl %0" : "=m"(out) : "m"(raw) : "st");
#endif
  return out;
}
static double load_bits(uint64_t raw) {
  double out;
#ifdef BB_MODEL_RUNTIME
  memcpy(&out, &raw, sizeof out);
#else
  __asm__ volatile("fldl %1" : "=t"(out) : "m"(raw));
#endif
  return out;
}
double bb_crt_math_round_entry(uint32_t low, uint32_t high) {
  return load_bits(rounded_bits(bits(low, high)));
}
int32_t bb_crt_math_special_entry(uint32_t low, uint32_t high) {
  if (high == 0x7ff00000 && !low)
    return 1;
  if (high == 0xfff00000 && !low)
    return 2;
  if ((high & 0x7ff80000) == 0x7ff80000)
    return 3;
  if ((high & 0x7ff80000) == 0x7ff00000 && ((high & 0x7ffff) || low))
    return 4;
  return 0;
}
static uint16_t compare_zero(uint64_t raw, uintptr_t zero) {
#ifdef BB_MODEL_RUNTIME
  double value = load_bits(raw), other = BB_DOUBLE(zero);
  return isnan(value) || isnan(other) ? 0x4500u
         : value < other              ? 0x0100u
         : value == other             ? 0x4000u
                                      : 0;
#else
  uint16_t status;
  __asm__ volatile("fldl %1;fcompl %2;fnstsw %%ax"
                   : "=a"(status)
                   : "m"(raw), "m"(*(const double *)zero)
                   : "st");
  return status;
#endif
}
int32_t bb_crt_math_classify_entry(uint32_t low, uint32_t high) {
  if ((high & 0x7ff00000) == 0x7ff00000) {
    switch (bb_crt_math_special_entry(low, high)) {
    case 1:
      return 0x200;
    case 2:
      return 4;
    case 3:
      return 2;
    default:
      return 1;
    }
  }
  unsigned negative = high >> 31;
  if (compare_zero(bits(low, high), 0x4835b0) & 0x4000)
    return negative ? 0x20 : 0x40;
  if (!(high & 0x7ff00000) && ((high & 0xfffff) || low))
    return negative ? 0x10 : 0x80;
  return negative ? 8 : 0x100;
}
int32_t bb_crt_math_parity_entry(uint32_t low, uint32_t high) {
  if (bb_crt_math_classify_entry(low, high) & 0x90)
    return 0;
#ifdef BB_MODEL_RUNTIME
  double value = load_bits(bits(low, high));
  if (isnan(value))
    return 2; /* The original unordered comparison tests the equality bit. */
  if (value != load_bits(rounded_bits(bits(low, high))))
    return 0;
  double half = value / BB_DOUBLE(0x483500);
  uint64_t raw;
  memcpy(&raw, &half, sizeof raw);
  return half == load_bits(rounded_bits(raw)) ? 2 : 1;
#else
  uint64_t raw = bits(low, high), rounded = rounded_bits(raw), scratch;
  uint16_t status;
  __asm__ volatile("fldl %2;fcoml %3;fstpl %1;fnstsw %%ax"
                   : "=a"(status), "=m"(scratch)
                   : "m"(rounded), "m"(raw)
                   : "st");
  if (!(status & 0x4000))
    return 0;
  uint64_t half;
  __asm__ volatile("fldl %1;fdivl %2;fstpl %0"
                   : "=m"(half)
                   : "m"(raw), "m"(*(const double *)0x483500)
                   : "st");
  rounded = rounded_bits(half);
  __asm__ volatile("fldl %1;fldl %2;fdivl %3;fcompp;fnstsw %%ax"
                   : "=a"(status)
                   : "m"(rounded), "m"(raw), "m"(*(const double *)0x483500)
                   : "st", "st(1)");
  return status & 0x4000 ? 2 : 1;
#endif
}
static uint64_t install_exponent(uint64_t raw, uint32_t exponent) {
  uint16_t header =
      (uint16_t)((raw >> 48) & 0x800f) | ((uint16_t)((exponent + 1022) << 4));
  return (raw & UINT64_C(0x0000ffffffffffff)) | ((uint64_t)header << 48);
}
double bb_crt_math_exponent_entry(uint32_t low, uint32_t high,
                                  uint32_t exponent) {
  return load_bits(install_exponent(bits(low, high), exponent));
}
double bb_crt_math_fraction_entry(uint32_t low, uint32_t high,
                                  int32_t *exponent) {
  uint64_t raw = bits(low, high), fraction;
  int32_t power;
  if (compare_zero(raw, 0x4833b8) & 0x4000) {
    fraction = 0;
    power = 0;
  } else if (!(high & 0x7ff00000) && ((high & 0xfffff) || low)) {
    power = -1021;
    unsigned negative = !!(compare_zero(raw, 0x4833b8) & 0x100);
    while (!(raw & UINT64_C(0x0010000000000000))) {
      raw <<= 1;
      --power;
    }
    raw &= ~UINT64_C(0x0010000000000000);
    if (negative)
      raw |= UINT64_C(0x8000000000000000);
    fraction = install_exponent(raw, 0);
    /* The original set-exp result is stored to a double and loaded again. */
#ifndef BB_MODEL_RUNTIME
    __asm__ volatile("fldl %1;fstpl %0"
                     : "=m"(fraction)
                     : "m"(fraction)
                     : "st");
#endif
  } else {
    fraction = install_exponent(raw, 0);
#ifndef BB_MODEL_RUNTIME
    __asm__ volatile("fldl %1;fstpl %0"
                     : "=m"(fraction)
                     : "m"(fraction)
                     : "st");
#endif
    power = (int32_t)((high >> 20) & 0x7ff) - 1022;
  }
  BB_I32(exponent) = power;
  return load_bits(fraction);
}
