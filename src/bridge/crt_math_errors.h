/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_MATH_ERRORS_H
#define BUMPKINS_CRT_MATH_ERRORS_H
#include <stdint.h>
double bb_crt_math_floor_entry(uint32_t, uint32_t);
double bb_crt_math_quiet_nan_entry(uint32_t, uint32_t, uint32_t, uint32_t);
double bb_crt_math_exception_entry(uint32_t, uint32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t, uint32_t);
void bb_crt_math_raise_entry(void *, uint32_t *, uint32_t, uint32_t,
                             const void *, void *);
int32_t bb_crt_math_normalize_entry(uint32_t, void *, uint32_t);
double bb_crt_math_report_entry(int32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                uint32_t, uint32_t, uint32_t, uint32_t);
int32_t bb_crt_math_errno_entry(int32_t);
const char *bb_crt_math_name_entry(uint32_t);
uint32_t bb_crt_math_error_kind_entry(uint32_t);
void bb_crt_math_raise_status_entry(uint32_t);
void bb_crt_math_x87_exception_entry(uint32_t, void *, const uint16_t *);
#endif
