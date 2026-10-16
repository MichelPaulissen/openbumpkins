/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_TERRAIN_MODELS_H
#define BB_TERRAIN_MODELS_H
#include "model_runtime.h"

/* Stable logical identities, independent of the host function's address. */
enum {
  BB_MODEL_ID_bb_road_shape_entry = 0x4183c0,
  BB_MODEL_ID_bb_road_prune_near_entry = 0x41ab80,
  BB_MODEL_ID_bb_road_prune_entry = 0x41abe0,
  BB_MODEL_ID_bb_terrain_dirty_entry = 0x421590,
  BB_MODEL_ID_bb_terrain_fill_entry = 0x4215d0,
  BB_MODEL_ID_bb_terrain_radius_entry = 0x421670,
  BB_MODEL_ID_bb_terrain_open_all_entry = 0x421690,
  BB_MODEL_ID_bb_terrain_block_all_entry = 0x4216e0,
  BB_MODEL_ID_bb_terrain_blend_entry = 0x421760,
  BB_MODEL_ID_bb_terrain_blend_near_entry = 0x421870,
  BB_MODEL_ID_bb_terrain_blend_area_entry = 0x421920,
  BB_MODEL_ID_bb_terrain_set_entry = 0x421a80,
  BB_MODEL_ID_bb_terrain_transition_entry = 0x421b90,
  BB_MODEL_ID_bb_entity_distance = 0x434de0,
  BB_MODEL_ID_bb_entity_reveal_entry = 0x447a80,
  BB_MODEL_ID_bb_random = 0x46be20
};
const struct bb_model_target *bb_terrain_model_find(void *, uint32_t);
void bb_road_shape_c(uint32_t[9]);
void bb_road_prune_near_c(uint32_t[9]);
void bb_road_prune_c(uint32_t[9]);
void bb_terrain_fill_c(uint32_t[9]);
void bb_terrain_radius_c(uint32_t[9]);
void bb_terrain_open_all_c(uint32_t[9]);
void bb_terrain_block_all_c(uint32_t[9]);
void bb_terrain_blend_c(uint32_t[9]);
void bb_terrain_blend_near_c(uint32_t[10]);
void bb_terrain_blend_area_c(uint32_t[9]);
void bb_terrain_set_c(uint32_t[9]);
void bb_terrain_transition_c(uint32_t[9]);
#endif
