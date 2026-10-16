/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_FORMAT_H
#define BUMPKINS_CRT_FORMAT_H
#include <stdint.h>
void bb_crt_format_initialize_entry(void);
void bb_crt_format_decimal_entry(char *);
void bb_crt_format_trim_entry(char *);
uint32_t bb_crt_format_positive_entry(const double *);
char *bb_crt_format_scientific_entry(const void *, char *, int32_t, int32_t);
char *bb_crt_format_fixed_entry(const void *, char *, int32_t);
char *bb_crt_format_general_entry(const void *, char *, int32_t, int32_t);
char *bb_crt_format_scientific_cached_entry(const void *, char *, int32_t,
                                            int32_t);
char *bb_crt_format_fixed_cached_entry(const void *, char *, int32_t);
char *bb_crt_format_number_entry(const void *, char *, uint32_t, int32_t,
                                 int32_t);
void bb_crt_format_shift_entry(char *, uint32_t);
#endif
