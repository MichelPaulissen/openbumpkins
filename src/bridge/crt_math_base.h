/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_CRT_MATH_BASE_H
#define BUMPKINS_CRT_MATH_BASE_H
#include <stdint.h>
uint64_t bb_crt_math_integer_entry(void);
uint64_t BB_STDCALL bb_crt_math_divide_entry(uint32_t, uint32_t,
                                                           uint32_t, uint32_t);
uint64_t BB_STDCALL
bb_crt_math_remainder_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_crt_math_control_entry(uint32_t, uint32_t);
uint32_t bb_crt_math_control_safe_entry(uint32_t, uint32_t);
uint32_t bb_crt_math_decode_entry(uint32_t);
uint32_t bb_crt_math_encode_entry(uint32_t);
int32_t bb_crt_math_status_entry(void);
int32_t bb_crt_math_clear_entry(void);
int32_t bb_crt_math_word_entry(uint32_t, uint32_t);
double bb_crt_math_round_entry(uint32_t, uint32_t);
int32_t bb_crt_math_classify_entry(uint32_t, uint32_t);
int32_t bb_crt_math_special_entry(uint32_t, uint32_t);
int32_t bb_crt_math_parity_entry(uint32_t, uint32_t);
double bb_crt_math_exponent_entry(uint32_t, uint32_t, uint32_t);
double bb_crt_math_fraction_entry(uint32_t, uint32_t, int32_t *);
#endif
