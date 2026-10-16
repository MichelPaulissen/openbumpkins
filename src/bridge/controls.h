/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_CONTROLS_H
#define BUMPKINS_CONTROLS_H
#include <stdint.h>
void *BB_THISCALL bb_ambience_identity_entry(void *);
void BB_THISCALL bb_ambience_initialize_entry(void *);
void bb_ambience_tick_entry(void);
void bb_ambience_noop_entry(void);
void *BB_THISCALL bb_numeric_panel_construct_entry(void *);
void BB_THISCALL bb_numeric_panel_initialize_entry(void *);
void bb_numeric_panel_draw_entry(void);
uint32_t BB_THISCALL bb_numeric_panel_click_entry(void *, int32_t,
                                                                int32_t);
void BB_THISCALL bb_numeric_panel_tick_entry(void *);
void BB_THISCALL bb_numeric_panel_set_entry(void *, uint32_t);
void *BB_THISCALL bb_digit_construct_entry(void *);
void BB_THISCALL bb_digit_position_entry(void *, uint32_t,
                                                       uint32_t);
void BB_THISCALL bb_digit_set_entry(void *, uint32_t);
uint16_t BB_STDCALL bb_digit_direction_entry(uint32_t, int32_t);
void *BB_THISCALL bb_number_construct_entry(void *);
void bb_number_draw_entry(void);
void BB_THISCALL bb_number_initialize_entry(void *, uint32_t,
                                                          uint32_t);
void BB_THISCALL bb_number_set_entry(void *, uint32_t);
void BB_THISCALL bb_number_adjust_entry(void *, uint32_t);
uint32_t BB_THISCALL bb_number_click_entry(void *, int32_t,
                                                         int32_t);
void bb_editor_remove_entry(uint32_t, uint32_t, uint32_t);
void bb_editor_heights_copy_entry(void);
uint32_t bb_editor_height_probe_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_editor_height_apply_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_editor_height_change_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_editor_height_relax_entry(uint32_t, uint32_t, uint32_t);
void bb_minimap_draw_entry(void *, uint32_t, uint32_t, uint32_t, uint32_t,
                           uint32_t);
void *BB_THISCALL bb_building_stock_construct_entry(void *);
void BB_THISCALL bb_building_stock_set_entry(void *, uint32_t);
void BB_THISCALL bb_building_stock_draw_entry(void *);
void bb_building_stock_tick_entry(void);
uint32_t BB_THISCALL
bb_building_stock_click_entry(void *, int32_t, int32_t);
#endif
