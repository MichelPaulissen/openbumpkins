/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_WORKER_RUNTIME_H
#define BUMPKINS_WORKER_RUNTIME_H
#include <stdint.h>
void bb_random_event_entry(void);
void bb_initial_buildings_entry(uint32_t, uint32_t);
void *BB_THISCALL bb_short_object_reset_entry(void *);
void BB_THISCALL bb_faction_end_state_entry(void *, uint32_t);
uint32_t bb_clock_text_parse_entry(const char *);
void BB_THISCALL bb_stock_panel_tick_entry(void *);
void BB_THISCALL bb_stock_panel_needs_entry(void *);
void BB_THISCALL bb_stock_panel_animation_entry(void *, uint32_t);
uint32_t BB_THISCALL bb_stock_panel_click_entry(void *, int32_t,
                                                              int32_t);
void bb_indexed_text_load_entry(const char *);
void bb_faction_music_reset_entry(void);
#endif
