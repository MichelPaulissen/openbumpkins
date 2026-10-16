/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_DIALOG_H
#define BUMPKINS_DIALOG_H
#include <stdint.h>
void bb_mouse_map_position_entry(int32_t, int32_t, int32_t, int32_t, uint8_t *,
                                 uint8_t *);
void *BB_THISCALL bb_feedback_construct_entry(void *);
void BB_THISCALL bb_feedback_append_entry(void *, const char *);
void BB_THISCALL bb_feedback_draw_entry(void *);
void BB_THISCALL bb_feedback_tick_entry(void *);
void BB_THISCALL bb_dialog_draw_entry(void *);
void BB_THISCALL bb_dialog_tick_entry(void *);
uint32_t BB_THISCALL bb_dialog_key_entry(void *, uint32_t);
uint32_t BB_THISCALL bb_dialog_activate_entry(void *, int32_t,
                                                            int32_t);
uint32_t BB_THISCALL bb_dialog_hover_entry(void *, int32_t,
                                                         int32_t);
void BB_THISCALL bb_dialog_build_entry(void *, uint32_t);
void BB_THISCALL bb_dialog_select_entry(void *, uint32_t);
void BB_THISCALL bb_dialog_command_entry(void *);
void BB_THISCALL bb_dialog_visibility_entry(void *, uint32_t);
void bb_save_slots_refresh_entry(void);
void *BB_THISCALL bb_highlight_construct_entry(void *);
void BB_THISCALL bb_highlight_draw_entry(void *);
void BB_THISCALL bb_highlight_tick_entry(void *);
void BB_THISCALL
bb_highlight_region_entry(void *, int32_t, int32_t, int32_t, int32_t);
void BB_THISCALL bb_highlight_enabled_entry(void *, uint32_t);
void *BB_THISCALL bb_demo_construct_entry(void *);
void BB_THISCALL bb_demo_load_entry(void *, const char *);
void BB_THISCALL bb_demo_save_entry(void *, const char *);
void BB_THISCALL bb_demo_append_entry(void *, uint32_t, uint32_t,
                                                    uint32_t);
void BB_THISCALL bb_demo_pump_entry(void *);
void BB_THISCALL bb_demo_clear_entry(void *);
void BB_THISCALL bb_demo_rewind_entry(void *);
#endif
