/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_STATISTICS_H
#define BUMPKINS_STATISTICS_H
#include <stdint.h>
void BB_THISCALL bb_mission_theme_entry(void *, uint32_t);
void BB_THISCALL bb_mission_popup_draw_entry(void *);
void BB_THISCALL bb_mission_popup_visibility_entry(void *,
                                                                 uint32_t);
void BB_THISCALL bb_faction_statistics_entry(void *);
void BB_THISCALL bb_statistics_graph_draw_entry(void *);
void BB_THISCALL bb_statistics_draw_entry(void *);
void BB_THISCALL bb_statistics_tick_entry(void *);
uint32_t BB_THISCALL bb_statistics_click_entry(void *, int32_t,
                                                             int32_t);
void BB_THISCALL bb_statistics_history_entry(void *);
void BB_THISCALL bb_economy_draw_entry(void *);
uint32_t BB_THISCALL bb_economy_click_entry(void *, int32_t,
                                                          int32_t);
void BB_THISCALL bb_economy_drag_entry(void *, int32_t, int32_t);
#endif
