/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <string.h>

#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

static uint32_t entry_stack(const uint32_t *input) { return input[3] + 2052u; }
#ifndef BB_MODEL_RUNTIME
/* Host C math replaces the mixer CRT's floor, hypotenuse and integer conversion
 * graph. Only exceptional CRT reporting still crosses the temporary bridge.
 * Rounding/status-bit identity is unnecessary for audio; keep its values,
 * range checks, NaN classification and caller environment intact instead. */
union number {
  uint64_t bits;
  double value;
};
enum number_kind { FINITE, INFINITY_VALUE, QUIET_NAN, SIGNALING_NAN };
static enum number_kind number_kind(uint64_t bits) {
  if ((bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000))
    return FINITE;
  if (!(bits & UINT64_C(0x000fffffffffffff)))
    return INFINITY_VALUE;
  return (bits & UINT64_C(0x0008000000000000)) ? QUIET_NAN : SIGNALING_NAN;
}
static union number argument(const uint32_t *input, unsigned offset) {
  union number value;
  memcpy(&value.bits, (const void *)(uintptr_t)(entry_stack(input) + offset),
         8);
  return value;
}
static uint16_t control_word(void) {
  uint16_t value;
  __asm__ volatile("fnstcw %0" : "=m"(value));
  return value;
}
static void put_number(uint32_t *args, union number value) {
  args[0] = (uint32_t)value.bits;
  args[1] = (uint32_t)(value.bits >> 32);
}
static long double math_error(uint32_t *input, uintptr_t target,
                              uint32_t args[16]) {
  struct bb_call_state state = {input[7], input[6], input[5], input[8]};
  bb_qmix_call(&state, target, args, entry_stack(input) - 64u, input[0],
               input[1], input[2], input[4], 0);
  long double result;
  __asm__ volatile("fstpt %0" : "=m"(result) : : "st");
  input[7] = state.eax;
  input[6] = state.ecx;
  input[5] = state.edx;
  input[8] = state.flags;
  return result;
}
static long double floor_value(uint32_t *input, union number value) {
  uint16_t control = control_word();
  enum number_kind kind = number_kind(value.bits);
  int saved_errno = errno;
  fenv_t environment;
  feholdexcept(&environment);
  union number result = {.value = kind == SIGNALING_NAN ? value.value + 1.0
                                                        : floor(value.value)};
  unsigned changed = kind == FINITE && result.value != value.value;
  fesetenv(&environment);
  errno = saved_errno;
  uint32_t args[16] = {0};
  if (kind == QUIET_NAN) {
    args[0] = 11;
    put_number(args + 1, value);
    args[3] = control;
    return math_error(input, 0x100135f0u, args);
  }
  if (kind == SIGNALING_NAN || (changed && !(control & 0x20u))) {
    args[0] = kind == SIGNALING_NAN ? 8 : 0x10;
    args[1] = 11;
    put_number(args + 2, value);
    put_number(args + 4, result);
    args[6] = control;
    return math_error(input, 0x100136b0u, args);
  }
  return result.value;
}
static long double hypot_value(uint32_t *input, union number x, union number y,
                               uint32_t operation) {
  uint16_t control = control_word();
  enum number_kind a = number_kind(x.bits), b = number_kind(y.bits);
  int saved_errno = errno;
  fenv_t environment;
  feholdexcept(&environment);
  fesetround(FE_TONEAREST);
  union number result;
  unsigned error = 0;
  if (a == SIGNALING_NAN || b == SIGNALING_NAN) {
    result.value = x.value + y.value;
    error = 8;
  } else if (a == QUIET_NAN || b == QUIET_NAN) {
    /* The old CRT reports a quiet NaN even when the other input is infinity. */
    fesetenv(&environment);
    errno = saved_errno;
    uint32_t args[16] = {operation};
    put_number(args + 1, x);
    put_number(args + 3, y);
    args[5] = control;
    return math_error(input, 0x10013640u, args);
  } else if (a == INFINITY_VALUE || b == INFINITY_VALUE) {
    result.value = INFINITY;
  } else {
    x.bits &= UINT64_C(0x7fffffffffffffff);
    y.bits &= UINT64_C(0x7fffffffffffffff);
    /* Extended host storage avoids an intermediate double overflow. Keep the
     * CRT's scaled error-result convention for overflow and subnormal ranges.
     */
    long double magnitude = hypotl(x.value, y.value);
    int exponent;
    long double fraction = frexpl(magnitude, &exponent);
    if (magnitude != 0 && exponent > 1024) {
      result.value = (double)ldexpl(fraction, exponent - 1536);
      error = 0x11;
    } else if (magnitude != 0 && exponent < -1021) {
      result.value = (double)ldexpl(fraction, exponent + 1536);
      error = 0x12;
    } else {
      result.value = (double)magnitude;
      if (magnitude != 0 && !(control & 0x20u))
        error = 0x10;
    }
  }
  fesetenv(&environment);
  errno = saved_errno;
  if (error) {
    uint32_t args[16] = {error, operation};
    put_number(args + 2, x);
    put_number(args + 4, y);
    put_number(args + 6, result);
    args[8] = control;
    return math_error(input, 0x10013770u, args);
  }
  return result.value;
}
#endif
static void integer_result(uint32_t *input, long double value) {
  /* Avoid undefined C conversion for NaNs and out-of-range inputs. The old
   * conversion returns the signed 64-bit indefinite value in those cases. */
  int64_t result = INT64_MIN;
  if (isfinite(value) && value >= -0x1p63L && value < 0x1p63L)
    result = (int64_t)value;
  input[7] = (uint32_t)result;
  input[5] = (uint32_t)((uint64_t)result >> 32);
}

/* 0x10002790..0x100027ad: floor(float + 0.5), returned as an EDX:EAX pair. */
void bb_qmix_audio_round_c(uint32_t *input) {
  float value;
  memcpy(&value, BB_SPAN(entry_stack(input) + 4, 4), 4);
#ifdef BB_MODEL_RUNTIME
  /* Preserve audio rounding and defined invalid conversion without a legacy
   * x87 control word or its CRT reporting ABI. The caller keeps its C fenv. */
  int saved_errno = errno;
  fenv_t environment;
  feholdexcept(&environment);
  long double rounded = floor((double)value + 0.5);
  integer_result(input, rounded);
  fesetenv(&environment);
  errno = saved_errno;
#else
  union number shifted = {.value = (double)value + 0.5};
  integer_result(input, floor_value(input, shifted));
#endif
}
#ifdef BB_MODEL_RUNTIME
/* The spatial updater calls this boundary twice for its vector lengths.
 * Keep the floating result in the C call frame, as for other mixer methods. */
void bb_qmix_crt_hypot_c(uint32_t *input) {
  double x, y;
  memcpy(&x, BB_SPAN(entry_stack(input) + 4u, 8), 8);
  memcpy(&y, BB_SPAN(entry_stack(input) + 12u, 8), 8);
  int saved_errno = errno;
  fenv_t environment;
  feholdexcept(&environment);
  /* The old CRT returns NaN when either argument is NaN, even with infinity. */
  double result = isnan(x) || isnan(y) ? x + y : hypot(x, y);
  fesetenv(&environment);
  errno = saved_errno;
  bb_model_return_float(result);
}
#endif
#ifndef BB_MODEL_RUNTIME
/* 0x10011c4c..0x10011c73: original x87-input integer conversion boundary. */
void bb_qmix_float_integer_c(uint32_t *input) {
  long double value;
  __asm__ volatile("fstpt %0" : "=m"(value) : : "st");
  integer_result(input, value);
}
/* 0x10011c80..0x10011d95: CRT floor and exceptional-value routing. */
void bb_qmix_crt_floor_c(uint32_t *input) {
  *(long double *)(uintptr_t)input[9] = floor_value(input, argument(input, 4));
}
/* 0x100195e0..0x100195ff: public hypotenuse wrapper (operation 24). */
void bb_qmix_crt_hypot_c(uint32_t *input) {
  *(long double *)(uintptr_t)input[9] =
      hypot_value(input, argument(input, 4), argument(input, 12), 24);
}
/* 0x10019600..0x100198fc: shared hypotenuse calculation and error selector. */
void bb_qmix_crt_hypot_operation_c(uint32_t *input) {
  *(long double *)(uintptr_t)input[9] =
      hypot_value(input, argument(input, 4), argument(input, 12),
                  BB_U32(entry_stack(input) + 20));
}

/* Keep adapter symbol names separate from C bodies. */
uint32_t __attribute__((naked))
bb_qmix_audio_round_entry(uint32_t unused __attribute__((unused))) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_audio_round_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                 "2048(%esp),%esp;ret");
}
uint32_t __attribute__((naked)) bb_qmix_float_integer_entry(void) {
  __asm__ volatile(
      "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(
          bb_qmix_float_integer_c) ";leal 4(%esp),%esp;popal;popfl;leal "
                                   "2048(%esp),%esp;ret");
}
#define FLOAT_ENTRY(name, signature)                                           \
  double __attribute__((naked)) name##_entry signature {                       \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;leal 40(%esp),%eax;"              \
        "movl %eax,36(%esp);pushl %esp;call " BB_ASM(                          \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;"   \
                      "fldt -2044(%esp);ret");                                 \
  }
FLOAT_ENTRY(bb_qmix_crt_floor, (uint32_t a __attribute__((unused)),
                                uint32_t b __attribute__((unused))))
FLOAT_ENTRY(bb_qmix_crt_hypot, (uint32_t a __attribute__((unused)),
                                uint32_t b __attribute__((unused)),
                                uint32_t c __attribute__((unused)),
                                uint32_t d __attribute__((unused))))
FLOAT_ENTRY(bb_qmix_crt_hypot_operation, (uint32_t a __attribute__((unused)),
                                          uint32_t b __attribute__((unused)),
                                          uint32_t c __attribute__((unused)),
                                          uint32_t d __attribute__((unused)),
                                          uint32_t e __attribute__((unused))))
#undef FLOAT_ENTRY
#endif
