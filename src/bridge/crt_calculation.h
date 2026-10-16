/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_CALCULATION_H
#define BUMPKINS_CRT_CALCULATION_H
#include <stdint.h>
long double bb_crt_calculation_power_entry(uint32_t, uint32_t, uint32_t,
                                           uint32_t);
long double bb_crt_calculation_log_entry(uint32_t, uint32_t);
long double bb_crt_calculation_log10_entry(uint32_t, uint32_t);
long double bb_crt_calculation_exp_entry(uint32_t, uint32_t);
long double bb_crt_calculation_power_register_entry(void);
long double bb_crt_calculation_log_register_entry(void);
long double bb_crt_calculation_log10_register_entry(void);
long double bb_crt_calculation_exp_register_entry(void);
long double bb_crt_calculation_binary_memory_entry(uint32_t, uint32_t, uint32_t,
                                                   uint32_t);
long double bb_crt_calculation_unary_memory_entry(uint32_t, uint32_t);
long double bb_crt_calculation_binary_register_entry(void);
long double bb_crt_calculation_unary_register_entry(void);
long double bb_crt_calculation_dispatch_binary_entry(void);
long double bb_crt_calculation_dispatch_unary_entry(void);
long double bb_crt_calculation_finalize_memory_entry(void);
long double bb_crt_calculation_finalize_entry(void);
long double bb_crt_calculation_reduce_entry(void);
long double bb_crt_calculation_domain_entry(void);
/* Calculation targets use EBP/ECX/EDX and the live x87 stack. */
#define BB_CRT_CALCULATION_LEAVES(X)                                           \
  X(power_normal)                                                              \
  X(exp_normal) X(power_one) X(power_zero) X(power_singularity) X(log_checked) \
      X(log_zero) X(log_infinity) X(log10_normal) X(log_normal)                \
          X(power_underflow) X(power_overflow) X(exp_infinity)                 \
              X(power_special) X(nan_unary) X(nan_binary) X(nan_pair)          \
                  X(negate)
#define BB_CRT_LEAF_DECL(name)                                                 \
  long double bb_crt_calculation_##name##_entry(void);
BB_CRT_CALCULATION_LEAVES(BB_CRT_LEAF_DECL)
#undef BB_CRT_LEAF_DECL
#endif
