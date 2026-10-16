/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_WORLD_H
#define BUMPKINS_WORLD_H
#include <stdint.h>
void bb_world_initialize_entry(void);
void bb_type_clone_entry(uint32_t source, uint32_t destination);
void bb_type_blueprint_entry(uint32_t type, const uint8_t *cells);
void bb_world_interface_noop_entry(void);
void bb_world_costs_initialize_entry(void);
void BB_THISCALL bb_cursor_initialize_entry(void *self);
void BB_THISCALL bb_build_menu_initialize_entry(void *self);
void BB_THISCALL bb_worker_menu_initialize_entry(void *self);
void BB_THISCALL bb_icon_panel_initialize_entry(void *self);
void bb_terrain_prepare_entry(void);
void bb_terrain_prepare_tile_entry(uint32_t x, uint32_t y);
#endif
