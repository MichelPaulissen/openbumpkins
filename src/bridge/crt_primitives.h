/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_PRIMITIVES_H
#define BUMPKINS_CRT_PRIMITIVES_H
#include <stdint.h>
int32_t bb_crt_string_case_compare_entry(const char *, const char *);
char *bb_crt_string_find_entry(const char *, int32_t);
char *bb_crt_string_copy_count_entry(char *, const char *, uint32_t);
int32_t bb_crt_string_long_entry(const char *);
int32_t bb_crt_string_int_entry(const char *);
char *bb_crt_string_search_entry(const char *, const char *);
void *bb_crt_memory_copy_entry(void *, const void *, uint32_t);
void *bb_crt_memory_fill_entry(void *, int32_t, uint32_t);
char *bb_crt_string_copy_entry(char *, const char *);
char *bb_crt_string_append_entry(char *, const char *);
int32_t bb_crt_string_compare_entry(const char *, const char *);
char *bb_crt_string_find_last_entry(const char *, int32_t);
uint32_t bb_crt_string_length_entry(const char *);
int32_t bb_crt_string_compare_count_entry(const char *, const char *, uint32_t);
int32_t bb_crt_character_lower_entry(int32_t);
uint32_t bb_crt_character_type_entry(int32_t, uint32_t);
void *bb_crt_memory_move_entry(void *, const void *, uint32_t);
uint32_t bb_crt_string_count_entry(const char *, uint32_t);
uint32_t bb_crt_character_space_entry(int32_t);
uint32_t bb_crt_wide_length_entry(const uint16_t *);
uint32_t bb_crt_wide_count_entry(const uint16_t *, uint32_t);
uint32_t bb_crt_string_count_alt_entry(const char *, uint32_t);
#endif
