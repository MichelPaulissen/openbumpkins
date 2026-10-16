/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_NUMERIC_H
#define BUMPKINS_CRT_NUMERIC_H
#include <stdint.h>
void bb_crt_numeric_assign_entry(int32_t, void *, const char *);
uint32_t bb_crt_numeric_tail_zero_entry(const void *, int32_t);
uint32_t bb_crt_numeric_increment_entry(void *, int32_t);
uint32_t bb_crt_numeric_round_entry(void *, int32_t);
void bb_crt_numeric_copy_entry(void *, const void *);
void bb_crt_numeric_clear_entry(void *);
uint32_t bb_crt_numeric_zero_entry(const void *);
void bb_crt_numeric_shift_entry(void *, int32_t);
uint32_t bb_crt_numeric_binary_entry(const void *, void *, const int32_t *);
uint32_t bb_crt_numeric_double_entry(const void *, void *);
uint32_t bb_crt_numeric_float_entry(const void *, void *);
uint32_t bb_crt_numeric_parse_double_entry(void *, const char *);
uint32_t bb_crt_numeric_parse_float_entry(void *, const char *);
uintptr_t bb_crt_numeric_round_text_entry(char *, int32_t, void *);
void *bb_crt_numeric_decimal_entry(uint32_t, uint32_t);
void bb_crt_numeric_pack_entry(void *, const void *);
uint32_t bb_crt_numeric_add_word_entry(uint32_t, uint32_t, uint32_t *);
void bb_crt_numeric_add_entry(void *, const void *);
void bb_crt_numeric_left_entry(void *);
void bb_crt_numeric_right_entry(void *);
void bb_crt_numeric_accumulate_entry(const int8_t *, uint32_t, void *);
uint32_t bb_crt_numeric_parse_entry(void *, const char **, const char *,
                                    int32_t, int32_t, int32_t, int32_t);
/* Shared parser with a packed four-byte end-pointer output when requested.
 * The output is updated at its original point, before numeric-result storage. */
uint32_t bb_crt_numeric_parse_c(void *, const char **, const char *, int32_t,
                                int32_t, int32_t, int32_t, unsigned);
uint32_t bb_crt_numeric_generate_entry(uint32_t, uint32_t, uint32_t, int32_t,
                                       uint32_t, void *);
void bb_crt_numeric_multiply_entry(void *, const void *);
void bb_crt_numeric_scale_entry(void *, int32_t, int32_t);
#endif
