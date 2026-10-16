/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_SELECTION_PANELS_H
#define BUMPKINS_SELECTION_PANELS_H
#include <stdint.h>
void *BB_THISCALL bb_action_panel_identity_entry(void *);
void BB_THISCALL bb_action_panel_initialize_entry(void *);
void BB_THISCALL bb_action_panel_draw_entry(void *);
void BB_THISCALL bb_action_panel_tick_entry(void *);
void BB_THISCALL bb_action_panel_visibility_entry(void *,
                                                                uint32_t);
void BB_THISCALL bb_action_panel_expand_entry(void *, uint32_t);
void BB_THISCALL bb_action_panel_button_entry(void *, uint32_t);
void BB_THISCALL bb_action_panel_spell_begin_entry(void *,
                                                                 uint32_t);
void BB_THISCALL
bb_action_panel_spell_cast_entry(void *, uint32_t, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_action_panel_click_entry(void *, int32_t,
                                                               int32_t);
void *BB_THISCALL bb_building_panel_initialize_entry(void *);
void BB_THISCALL bb_building_panel_set_entry(void *, void *);
void BB_THISCALL bb_building_panel_draw_entry(void *);
void BB_THISCALL bb_building_panel_noop_entry(void *);
uint32_t BB_THISCALL
bb_building_panel_click_entry(void *, int32_t, int32_t);
void BB_THISCALL bb_building_panel_left_entry(void *, uint32_t,
                                                            uint32_t);
void BB_THISCALL bb_building_panel_right_entry(void *, uint32_t,
                                                             uint32_t);
void *BB_THISCALL bb_tooltip_construct_entry(void *);
void BB_THISCALL bb_tooltip_initialize_entry(void *);
void BB_THISCALL bb_tooltip_draw_entry(void *);
#endif
