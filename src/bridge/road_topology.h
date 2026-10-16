/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_ROAD_TOPOLOGY_H
#define BUMPKINS_ROAD_TOPOLOGY_H
#include <stdint.h>
void bb_junction_refresh_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_neighbour_kind_entry(uint32_t, uint32_t, uint32_t);
void bb_road_route_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
void bb_road_ensure_entry(uint32_t, uint32_t, uint32_t);
void bb_road_add_entry(uint32_t, uint32_t);
void bb_road_refresh_cardinal_entry(uint32_t, uint32_t);
uint64_t bb_building_road_connect_c(void *, uint32_t, uint32_t);
void BB_THISCALL bb_building_road_connect_entry(void *);
#endif
