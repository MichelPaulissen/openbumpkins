/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_WORLD_CONTROLS_H
#define BUMPKINS_WORLD_CONTROLS_H
#include <stdint.h>
uint32_t bb_direction_step_entry(uint32_t, uint32_t);
void bb_building_footprint_entry(uint32_t, uint32_t, uint32_t);
#define BB_LINE_DECL(name)                                                     \
  void BB_STDCALL name(void *, uint32_t, uint32_t, uint32_t,     \
                                     uint32_t, uint32_t, uint32_t)
BB_LINE_DECL(bb_line_right_down_x_entry);
BB_LINE_DECL(bb_line_right_down_y_entry);
BB_LINE_DECL(bb_line_left_down_y_entry);
BB_LINE_DECL(bb_line_left_down_x_entry);
BB_LINE_DECL(bb_line_right_up_x_entry);
BB_LINE_DECL(bb_line_right_up_y_entry);
BB_LINE_DECL(bb_line_left_up_y_entry);
BB_LINE_DECL(bb_line_left_up_x_entry);
#undef BB_LINE_DECL
uint32_t
    BB_THISCALL bb_display_line_entry(void *, void *, uint32_t,
                                                    uint32_t, uint32_t,
                                                    uint32_t, uint32_t);
void BB_THISCALL bb_entity_remove_kind_entry(void *, uint32_t);
void BB_THISCALL
bb_entity_select_rect_entry(void *, int32_t, int32_t, int32_t, int32_t);
void BB_THISCALL bb_entity_depth_check_entry(void *);
uint32_t BB_THISCALL bb_entity_pair_entry(void *, void *);
#endif
