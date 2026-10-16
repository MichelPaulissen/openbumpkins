/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_MATH_HELPERS_H
#define BUMPKINS_CRT_MATH_HELPERS_H
#include <stdint.h>
int32_t bb_crt_math_power_infinity_entry(uint32_t, uint32_t, uint32_t, uint32_t,
                                         void *);
long double bb_crt_math_load_entry(uint32_t, uint32_t);
uint32_t bb_crt_math_extended_parity_entry(void);
long double bb_crt_math_extended_divide_entry(uint32_t, uint32_t, uint32_t,
                                              uint32_t, uint32_t, uint32_t);
long double bb_crt_math_register_divide_entry(void);
long double bb_crt_math_discard_lower_entry(void);
long double bb_crt_math_zero_entry(void);
long double bb_crt_math_one_pair_entry(void);
#endif
