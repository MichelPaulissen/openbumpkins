/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_math_models.h"
#include "address.h"
#include "crt_calculation.h"
#include "crt_math_base.h"
#include "crt_math_errors.h"
#include "crt_math_helpers.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
#define P(n) ((void *)(uintptr_t)A(n))
static void integer(uint32_t *r) {
  uint64_t value = bb_crt_math_integer_entry();
  r[7] = value;
  r[5] = value >> 32;
}
static void quiet_nan(uint32_t *r) {
  bb_model_return_float(bb_crt_math_quiet_nan_entry(A(0), A(1), A(2), A(3)));
}
static void normalize(uint32_t *r) {
  r[7] = bb_crt_math_normalize_entry(A(0), P(1), A(2));
}
static void report(uint32_t *r) {
  bb_model_return_float(bb_crt_math_report_entry(A(0), A(1), A(2), A(3), A(4),
                                                 A(5), A(6), A(7), A(8)));
}
static void error_number(uint32_t *r) { r[7] = bb_crt_math_errno_entry(A(0)); }
static void name(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_math_name_entry(A(0)));
}
static void error_kind(uint32_t *r) {
  r[7] = bb_crt_math_error_kind_entry(A(0));
}
static void raise_status(uint32_t *r) { bb_crt_math_raise_status_entry(A(0)); }
static void power_infinity(uint32_t *r) {
  r[7] = bb_crt_math_power_infinity_entry(A(0), A(1), A(2), A(3), P(4));
}
// clang-format off
#define CALCULATIONS(X) \
  X(0x46ccd0, power, 4) \
  X(0x46ccda, log, 2) \
  X(0x46cce4, log10, 2) \
  X(0x46cceb, exp, 2) \
  X(0x46ccf2, power_register, 0) \
  X(0x46ccfc, log_register, 0) \
  X(0x46cd06, log10_register, 0) \
  X(0x46cd0d, exp_register, 0) \
  X(0x473220, binary_register, 0) \
  X(0x47325e, unary_register, 0) \
  X(0x473294, binary_memory, 4) \
  X(0x473405, unary_memory, 2) \
  X(0x4732ce, finalize_memory, 0) \
  X(0x4732d5, finalize, 0) \
  X(0x47ce60, dispatch_unary, 0) \
  X(0x47cec7, dispatch_binary, 0) \
  X(0x47316e, reduce, 0) \
  X(0x47d006, domain, 0) \
  X(0x473010, power_normal, 0) \
  X(0x473021, exp_normal, 0) \
  X(0x4730d8, power_one, 0) \
  X(0x4730dd, power_zero, 0) \
  X(0x473088, power_singularity, 0) \
  X(0x47309a, log_checked, 0) \
  X(0x4730b4, log_zero, 0) \
  X(0x4730c4, log_infinity, 0) \
  X(0x4730c9, log10_normal, 0) \
  X(0x4730cd, log_normal, 0) \
  X(0x473112, power_underflow, 0) \
  X(0x47314f, power_overflow, 0) \
  X(0x473161, exp_infinity, 0) \
  X(0x4731e5, power_special, 0) \
  X(0x47cf72, nan_unary, 0) \
  X(0x47cf9f, nan_binary, 0) \
  X(0x47cfc7, nan_pair, 0) \
  X(0x47d023, negate, 0)
// clang-format on
#define CALC_DECL(id, name, argc)                                              \
  extern void bb_crt_calculation_##name##_c(uint32_t *);
CALCULATIONS(CALC_DECL)
#undef CALC_DECL
// clang-format off
#define HELPERS(X) \
  X(0x47d370, extended_divide, 6) \
  X(0x473431, load, 2) \
  X(0x47db61, register_divide, 0) \
  X(0x4731b1, extended_parity, 0) \
  X(0x47cf58, discard_lower, 0) \
  X(0x47cf66, zero, 0) \
  X(0x47cf6b, one_pair, 0)
// clang-format on
#define MATH_DECL(id, name, argc)                                              \
  extern void bb_crt_math_##name##_c(uint32_t *);
HELPERS(MATH_DECL)
MATH_DECL(0, floor, 2)
MATH_DECL(0, exception, 7)
MATH_DECL(0, raise, 6)
MATH_DECL(0, x87_exception, 3)
#undef MATH_DECL
static const struct bb_model_target targets[] = {
    {0x46be64, integer, 0, 0, 0},
    {0x46f320, bb_crt_math_floor_c, 2, 256, 0},
    {0x477ea0, quiet_nan, 4, 0, 0},
    {0x477f10, bb_crt_math_exception_c, 7, 128, 0},
    {0x477fe0, bb_crt_math_raise_c, 6, 128, 0},
    {0x478530, normalize, 3, 0, 0},
    {0x4788c0, report, 9, 0, 0},
    {0x478980, error_number, 1, 0, 0},
    {0x4789e0, name, 1, 0, 0},
    {0x478a40, error_kind, 1, 0, 0},
    {0x478b50, raise_status, 1, 0, 0},
    {0x47e100, bb_crt_math_x87_exception_c, 3, 128, 0},
    {0x47d030, power_infinity, 5, 0, 0},
#define CALC_TARGET(id, name, argc)                                            \
  {id, bb_crt_calculation_##name##_c, argc, 2048, 0},
    CALCULATIONS(CALC_TARGET)
#undef CALC_TARGET
#define MATH_TARGET(id, name, argc) {id, bb_crt_math_##name##_c, argc, 128, 0},
    HELPERS(MATH_TARGET)
#undef MATH_TARGET
};
const struct bb_model_target *bb_crt_math_model_find(void *unused,
                                                     uint32_t id) {
  (void)unused;
  for (unsigned i = 0; i < sizeof targets / sizeof *targets; ++i)
    if (targets[i].identity == id)
      return &targets[i];
  return NULL;
}
static long double invoke(uint32_t id, const uint32_t *args, unsigned count) {
  struct bb_call_state state = {0};
  struct bb_model_result result;
  bb_model_invoke_result(&state, id, args, count, &result);
  return result.floating;
}
long double bb_crt_calculation_power_entry(uint32_t a, uint32_t b, uint32_t c,
                                           uint32_t d) {
  return invoke(0x46ccd0, (uint32_t[]){a, b, c, d}, 4);
}
long double bb_crt_calculation_log_entry(uint32_t a, uint32_t b) {
  return invoke(0x46ccda, (uint32_t[]){a, b}, 2);
}
long double bb_crt_calculation_log10_entry(uint32_t a, uint32_t b) {
  return invoke(0x46cce4, (uint32_t[]){a, b}, 2);
}
long double bb_crt_calculation_exp_entry(uint32_t a, uint32_t b) {
  return invoke(0x46cceb, (uint32_t[]){a, b}, 2);
}
