/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_LETTERS_H
#define BUMPKINS_LETTERS_H
#include <stdint.h>

void *BB_THISCALL bb_letters_construct_entry(void *);
void BB_THISCALL bb_letters_destroy_entry(void *);
void BB_THISCALL bb_letters_clear_entry(void *);
uint32_t BB_THISCALL
bb_letter_archive_text_entry(void *, const char *, char *);
char *BB_THISCALL bb_letter_resolve_entry(void *, const char *,
                                                        char *);
void BB_THISCALL bb_letter_post_entry(void *, uint32_t,
                                                    const char *);
void BB_THISCALL bb_letters_draw_entry(void *);
uint32_t BB_THISCALL bb_letters_count_entry(void *);
void *BB_THISCALL bb_letter_predecessor_entry(void *, void *);
void BB_THISCALL bb_letter_select_entry(void *, void *);
void BB_THISCALL bb_letter_select_index_entry(void *, uint32_t);
void BB_THISCALL bb_letters_tick_entry(void *);
uint32_t BB_THISCALL bb_letters_click_entry(void *, int32_t,
                                                          int32_t);
uint32_t BB_THISCALL bb_letters_noop_entry(void *, uint32_t,
                                                         uint32_t);
uint32_t BB_THISCALL bb_letters_hover_entry(void *, int32_t,
                                                          int32_t, char *);
void BB_THISCALL bb_letter_delete_entry(void *);
void BB_THISCALL bb_letters_read_entry(void *, void *);
void BB_THISCALL bb_letters_write_entry(void *, void *);
#endif
