/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_INTERFACE_CONTROLS_H
#define BUMPKINS_INTERFACE_CONTROLS_H
#include <stdint.h>
uint8_t bb_direction_entry(int32_t, int32_t);
uint32_t bb_direction_difference_entry(uint32_t, uint32_t);
void bb_selection_border_entry(uint32_t, uint32_t, uint32_t, uint32_t);
void *BB_THISCALL bb_cursor_construct_entry(void *);
void BB_STDCALL bb_cursor_select_entry(int32_t, int32_t,
                                                     uint32_t);
void *BB_THISCALL bb_toolbar_construct_entry(void *);
void BB_THISCALL bb_toolbar_initialize_entry(void *);
void BB_THISCALL bb_toolbar_draw_entry(void *);
void bb_toolbar_action_entry(uint32_t);
uint32_t BB_THISCALL bb_toolbar_click_entry(void *, int32_t,
                                                          int32_t);
void BB_THISCALL bb_toolbar_reset_entry(void *);
void bb_toolbar_tick_entry(void);
void BB_STDCALL bb_toolbar_help_entry(int32_t, int32_t, char *);
void *BB_THISCALL bb_build_menu_construct_entry(void *);
void BB_THISCALL bb_build_menu_draw_entry(void *);
uint32_t BB_THISCALL bb_build_menu_click_entry(void *, int32_t,
                                                             int32_t);
void BB_THISCALL bb_build_menu_tick_entry(void *);
void BB_THISCALL bb_build_menu_open_entry(void *);
void BB_THISCALL bb_build_menu_close_entry(void *);
void bb_interface_resources_refresh_entry(void);
void bb_observer_display_refresh_entry(void);
void bb_camera_focus_entity_entry(void *);
void bb_camera_focus_tile_entry(uint32_t, uint32_t);
#endif
