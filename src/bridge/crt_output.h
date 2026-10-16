/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_OUTPUT_H
#define BUMPKINS_CRT_OUTPUT_H
#include <stdint.h>
#include <stddef.h>
int32_t bb_crt_output_words(char *, uint32_t, const char *, const uint32_t *, size_t);
int32_t bb_crt_sprintf_entry(char *, const char *, ...);
int32_t bb_crt_snprintf_entry(char *, uint32_t, const char *, ...);
int32_t bb_crt_vsnprintf_entry(char *, uint32_t, const char *, const void *);
int32_t bb_crt_output_entry(void *, const char *, const void *);
void bb_crt_write_char_entry(int32_t, void *, uint32_t *);
void bb_crt_write_repeat_entry(int32_t, int32_t, void *, uint32_t *);
void bb_crt_write_string_entry(const char *, int32_t, void *, uint32_t *);
uint32_t bb_crt_argument_int_entry(uint32_t *);
uint64_t bb_crt_argument_wide_entry(uint32_t *);
uint32_t bb_crt_argument_short_entry(uint32_t *);
#endif
