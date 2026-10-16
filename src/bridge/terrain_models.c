/* SPDX-License-Identifier: GPL-3.0-only */
#include "terrain_models.h"
#include "interop.h"

/* Ordinary C leaves define semantic results explicitly. Their compiler's
 * incidental caller-saved registers/flags do not become part of the ABI.
 * Terrain's translated callers test/overwrite those before using them.
 */
static void distance_model(uint32_t *input) {
  uint32_t base = input[3] + 8;
  input[7] = bb_entity_distance(BB_U32(base), BB_U32(base + 4),
                                BB_U32(base + 8), BB_U32(base + 12));
}
static void reveal_model(uint32_t *input) {
  input[7] = bb_entity_reveal_c((void *)(uintptr_t)input[6],
                                BB_U32(input[3] + 8), input[7]);
}
static uint32_t logical_flags(uint32_t flags, uint32_t value) {
  uint32_t low = value & 255u;
  low ^= low >> 4;
  return (flags & ~0x8d5u) | (!value ? 0x40u : 0) |
         (value & 0x80000000u ? 0x80u : 0) |
         (((0x9669u >> (low & 15u)) & 1u) << 2);
}
static void random_model(uint32_t *input) {
  uint64_t result = bb_random_registers();
  input[7] = (uint32_t)result;
  input[6] = result >> 32;
  input[8] = logical_flags(input[8], input[7]);
}
static void dirty_model(uint32_t *input) {
  uint32_t base = input[3] + 8, column = BB_U32(base);
  uint64_t result = bb_terrain_dirty(column, BB_U32(base + 4));
  input[7] = (uint32_t)result;
  input[5] = result >> 32;
  input[6] = input[5] & 0x400000 ? BB_U8(0x484454) : (column & 255);
  input[8] = logical_flags(input[8], input[5] & 0x400000);
}

#define MODEL(name, count, reserve, local)                                     \
  {BB_MODEL_ID_##name##_entry, name##_c, count, reserve, local}
static const struct bb_model_target targets[] = {
    MODEL(bb_road_shape, 2, 0, 0),
    MODEL(bb_road_prune_near, 2, 0, 0),
    MODEL(bb_road_prune, 2, 0, 0),
    {BB_MODEL_ID_bb_terrain_dirty_entry, dirty_model, 2, 0, 0},
    MODEL(bb_terrain_fill, 3, 0, 0),
    MODEL(bb_terrain_radius, 1, 0, 0),
    MODEL(bb_terrain_open_all, 0, 0, 0),
    MODEL(bb_terrain_block_all, 0, 0, 0),
    MODEL(bb_terrain_blend, 2, 0, 0),
    MODEL(bb_terrain_blend_near, 2, 4, 1),
    MODEL(bb_terrain_blend_area, 2, 0, 0),
    MODEL(bb_terrain_set, 4, 0, 0),
    MODEL(bb_terrain_transition, 5, 0, 0),
    {BB_MODEL_ID_bb_entity_distance, distance_model, 4, 0, 0},
    {BB_MODEL_ID_bb_entity_reveal_entry, reveal_model, 1, 0, 0},
    {BB_MODEL_ID_bb_random, random_model, 0, 0, 0}};
#undef MODEL

const struct bb_model_target *bb_terrain_model_find(void *unused,
                                                    uint32_t identity) {
  (void)unused;
  size_t low = 0, high = sizeof(targets) / sizeof(*targets);
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (targets[mid].identity < identity)
      low = mid + 1;
    else if (targets[mid].identity > identity)
      high = mid;
    else
      return targets + mid;
  }
  return NULL;
}

static uint32_t invoke(uint32_t id, void *self, const uint32_t *args,
                       size_t words) {
  struct bb_call_state call = {0, bb_address_encode(self), 0, 0x202};
  bb_model_invoke(&call, id, args, words);
  return call.eax;
}
#define ID(name) BB_MODEL_ID_##name##_entry
uint32_t bb_road_shape_entry(uint32_t x, uint32_t y) {
  return invoke(ID(bb_road_shape), NULL, (uint32_t[]){x, y}, 2);
}
uint32_t bb_road_prune_near_entry(uint32_t x, uint32_t y) {
  return invoke(ID(bb_road_prune_near), NULL, (uint32_t[]){x, y}, 2);
}
uint32_t bb_road_prune_entry(uint32_t x, uint32_t y) {
  return invoke(ID(bb_road_prune), NULL, (uint32_t[]){x, y}, 2);
}
uint32_t BB_THISCALL bb_terrain_fill_entry(void *self, uint32_t x, uint32_t y,
                                           uint32_t type) {
  return invoke(ID(bb_terrain_fill), self, (uint32_t[]){x, y, type}, 3);
}
uint32_t BB_THISCALL bb_terrain_radius_entry(void *self, uint32_t radius) {
  return invoke(ID(bb_terrain_radius), self, &radius, 1);
}
void bb_terrain_open_all_entry(void) {
  (void)invoke(ID(bb_terrain_open_all), NULL, NULL, 0);
}
void bb_terrain_block_all_entry(void) {
  (void)invoke(ID(bb_terrain_block_all), NULL, NULL, 0);
}
#define ENTRY2(name)                                                           \
  uint32_t BB_THISCALL name##_entry(void *self, uint32_t x, uint32_t y) {      \
    return invoke(ID(name), self, (uint32_t[]){x, y}, 2);                      \
  }
ENTRY2(bb_terrain_blend)
ENTRY2(bb_terrain_blend_near)
ENTRY2(bb_terrain_blend_area)
#undef ENTRY2
uint32_t BB_THISCALL bb_terrain_set_entry(void *self, uint32_t x, uint32_t y,
                                          uint32_t type, uint32_t blend) {
  return invoke(ID(bb_terrain_set), self, (uint32_t[]){x, y, type, blend}, 4);
}
uint32_t BB_THISCALL bb_terrain_transition_entry(void *self, uint32_t x,
                                                 uint32_t y, uint32_t a,
                                                 uint32_t b, uint32_t c) {
  return invoke(ID(bb_terrain_transition), self, (uint32_t[]){x, y, a, b, c},
                5);
}
