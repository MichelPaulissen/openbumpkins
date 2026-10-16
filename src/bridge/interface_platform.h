/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_INTERFACE_PLATFORM_H
#define BUMPKINS_INTERFACE_PLATFORM_H
#include <stdint.h>
void BB_THISCALL bb_interface_audio_start_entry(void *);
void BB_THISCALL bb_interface_audio_stop_entry(void *);
void BB_THISCALL bb_interface_sound_disable_entry(void *);
void BB_THISCALL bb_interface_sound_enable_entry(void *);
void bb_interface_mouse_drain_entry(uint32_t);
void *BB_THISCALL bb_minimap_panel_initialize_entry(void *);
void BB_THISCALL bb_minimap_panel_draw_entry(void *);
void BB_THISCALL bb_minimap_panel_visibility_entry(void *,
                                                                 uint32_t);
void bb_minimap_panel_noop_entry(void);
uint32_t BB_THISCALL bb_minimap_panel_click_entry(void *, int32_t,
                                                                int32_t);
void bb_season_tick_entry(void);
uint32_t BB_STDCALL
bb_nearest_free_tile_entry(uint32_t, uint32_t, uint8_t *, uint8_t *, uint32_t);
void *BB_THISCALL bb_stock_panel_construct_entry(void *);
void BB_THISCALL bb_stock_panel_draw_entry(void *);
void bb_panel_mode_entry(uint32_t);
void *BB_THISCALL bb_mission_description_construct_entry(void *);
void BB_THISCALL bb_mission_description_clear_entry(void *);
void BB_THISCALL bb_mission_description_draw_entry(void *);
void BB_THISCALL bb_mission_description_click_entry(void *);
void BB_THISCALL bb_mission_description_tick_entry(void *);
void BB_THISCALL bb_mission_description_load_entry(void *,
                                                                 uint32_t);
void bb_game_resource_guard_entry(void);
#endif
