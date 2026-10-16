/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_SCRIPT_H
#define BUMPKINS_SCRIPT_H
#include <stdint.h>
#include "address.h"

void *BB_THISCALL bb_script_event_construct_entry(void *);
void *BB_THISCALL bb_script_block_construct_entry(void *);
void BB_THISCALL bb_script_block_clear_entry(void *);
void BB_THISCALL bb_script_event_add_entry(void *, void *);
void BB_THISCALL bb_script_event_remove_entry(void *, void *);
void BB_THISCALL bb_script_event_remove_index_entry(void *,
                                                                  uint32_t);
void BB_THISCALL bb_script_event_sort_entry(void *);
void BB_THISCALL bb_script_condition_add_entry(void *, void *);
void BB_THISCALL bb_script_condition_remove_entry(void *, void *);
void BB_THISCALL bb_script_condition_remove_index_entry(void *,
                                                                      uint32_t);
uint32_t BB_THISCALL bb_script_event_count_entry(void *);
uint32_t BB_THISCALL bb_script_condition_count_entry(void *);
void *BB_THISCALL bb_script_condition_construct_entry(void *);
void *BB_THISCALL bb_script_construct_entry(void *);
void BB_THISCALL bb_script_event_new_entry(void *, void *);
void BB_THISCALL bb_script_condition_new_entry(void *, uint32_t);
void BB_THISCALL bb_script_input_entry(void *, const char *);
uint32_t BB_THISCALL
bb_script_block_select_entry(void *, uint32_t, uint32_t);
void BB_THISCALL bb_script_player_select_entry(void *, uint32_t);
void BB_THISCALL bb_script_rewind_entry(void *);
void BB_THISCALL bb_script_clear_entry(void *);
void bb_script_time_text_entry(uint32_t, char *);
void BB_THISCALL bb_script_event_execute_entry(void *);
void BB_THISCALL bb_script_event_text_entry(void *, char *);
void BB_THISCALL bb_script_condition_text_entry(void *, char *);
uint32_t BB_THISCALL
bb_script_condition_evaluate_entry(void *, uint32_t, void *);
void BB_THISCALL bb_script_block_tick_entry(void *, uint32_t);
void BB_THISCALL bb_script_tick_entry(void *);
uint32_t BB_THISCALL bb_script_key_entry(void *, uint32_t);
void BB_THISCALL bb_script_block_draw_entry(void *, int32_t,
                                                          int32_t);
void BB_THISCALL bb_script_draw_entry(void *);
void BB_THISCALL bb_script_read_entry(void *, void *);
void BB_THISCALL bb_script_write_entry(void *, void *);

static inline uintptr_t bb_script_selected_block(void *object) {
  const unsigned char *self = BB_DATA(object);
  unsigned player = self[0x82];
  return bb_address_encode(object) + 0x88 + (player * 32 + self[0x83 + player]) * 28;
}
#endif
