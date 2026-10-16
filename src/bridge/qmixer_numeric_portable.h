/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_QMIXER_NUMERIC_PORTABLE_H
#define BB_QMIXER_NUMERIC_PORTABLE_H
#include <limits.h>
#include <math.h>

#define F32(a) BB_FLOAT(a)
#define F64(a) BB_DOUBLE(a)

/* Audio uses C arithmetic without changing host rounding/precision settings.
 * Preserve truncation, comparison and invalid-conversion intentions. Small
 * transcendental/rounding differences from x87 are permitted for audio.
 */
static void __attribute__((unused))
sample_float_to_int(struct bb_call_state *state, const long double *value) {
  int64_t result = INT64_MIN;
  if (isfinite(*value) && *value >= -0x1p63L && *value < 0x1p63L)
    result = (int64_t)truncl(*value);
  state->eax = (uint32_t)result;
  state->edx = (uint64_t)result >> 32;
  state->flags = logic_flags(state->flags, 15, 8);
}
static uint16_t __attribute__((unused)) sample_compare(const long double *a,
                                                       const long double *b) {
  return isnan(*a) || isnan(*b) ? 0x4500u
         : *a < *b              ? 0x0100u
         : *a == *b             ? 0x4000u
                                : 0;
}
static long double __attribute__((unused)) qmix_constant(unsigned kind) {
  return !kind ? 1.0L : kind == 1 ? logl(2.0L) : 1.0L / logl(2.0L);
}
static long double __attribute__((unused)) qmix_unary(const long double *x,
                                                      unsigned kind) {
  return !kind       ? sqrtl(*x)
         : kind == 1 ? cosl(*x)
         : kind == 2 ? rintl(*x)
                     : expm1l(*x * logl(2.0L));
}
static long double __attribute__((unused))
qmix_binary(const long double *y, const long double *x, unsigned kind) {
  return !kind ? atan2l(*y, *x) : *y * log2l(*x);
}
static long double __attribute__((unused))
qmix_scale(const long double *x, const long double *exponent) {
  if (isnan(*exponent))
    return NAN;
  int power = *exponent >= INT_MAX   ? INT_MAX
              : *exponent <= INT_MIN ? INT_MIN
                                     : (int)truncl(*exponent);
  return scalbnl(*x, power);
}
static uint16_t __attribute__((unused)) qmix_status(uint16_t comparison,
                                                    unsigned depth) {
  return comparison | (((8u - depth) & 7u) << 11);
}
static uint32_t __attribute__((unused)) qmix_integer(const long double *x) {
  long double rounded = rintl(*x);
  return !isfinite(rounded) || rounded < -0x1p31L || rounded >= 0x1p31L
             ? 0x80000000u
             : (uint32_t)(int32_t)rounded;
}
#endif
