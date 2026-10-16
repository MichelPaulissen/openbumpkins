/* SPDX-License-Identifier: GPL-3.0-only */
#include "entity_query_models.h"
#include "interop.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
static void sort(uint32_t *r) {
  r[7] = bb_entity_sort((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
  r[5] = A(0);
  r[8] = (r[8] & ~0x8d5u) | 0x44u;
}
static void add(uint32_t *r) {
  uint32_t self = r[6];
  uint64_t v = bb_entity_occupancy_add_c((void *)(uintptr_t)self, r[7], r[5]);
  r[7] = v;
  r[5] = v >> 32;
  if (BB_U32(self + 0x1008))
    r[6] = 0;
  r[8] = (r[8] & ~0x8d5u) | 0x44u;
}
static void remove_entry(uint32_t *r) {
  uint32_t self = r[6];
  uint64_t v =
      bb_entity_occupancy_remove_c((void *)(uintptr_t)self, r[7], r[5]);
  r[7] = v;
  r[5] = v >> 32;
  if (BB_U32(self + 0x1008))
    r[6] = 0;
  r[8] = (r[8] & ~0x8d5u) | 0x44u;
}
static void dirty(uint32_t *r) {
  unsigned visible = BB_U8(r[6] + 0x128);
  uint32_t v = BB_U8(0x484454), result = (v + v) & 255;
  r[7] = bb_entity_force_dirty_c((void *)(uintptr_t)r[6], r[7]);
  uint32_t f = 0x44;
  if (visible)
    f = (!result ? 0x40u : 0) | (result & 128u ? 128u : 0) |
        (__builtin_parity(result) ? 0 : 4) | (result & 16u) | (v >= 128u) |
        (((v ^ result) & 128u) ? 0x800u : 0);
  r[8] = (r[8] & ~0x8d5u) | f;
}
static void model_bb_entity_at(uint32_t *r) {
  r[7] = bb_address_encode(bb_entity_at((void *)(uintptr_t)r[6], A(0), A(1)));
}
static void model_bb_entity_at_next(uint32_t *r) {
  r[7] = bb_address_encode(bb_entity_at_next(
      (void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0), A(1), A(2)));
}
static void model_bb_entity_outer_at(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_entity_outer_at((void *)(uintptr_t)r[6], A(0), A(1)));
}
static void model_bb_entity_near(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_entity_near((void *)(uintptr_t)r[6], A(0), A(1), A(2)));
}
static void model_bb_entity_near_next(uint32_t *r) {
  r[7] = bb_address_encode(bb_entity_near_next(
      (void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0), A(1), A(2), A(3)));
}
static void model_bb_entity_nearest(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_entity_nearest((void *)(uintptr_t)r[6], A(0), A(1), A(2), A(3)));
}
static void model_bb_entity_assign_target(uint32_t *r) {
  r[7] = bb_address_encode(bb_entity_assign_target((void *)(uintptr_t)r[6],
                                                   (void *)(uintptr_t)A(0)));
}
static void model_bb_entity_set_ability(uint32_t *r) {
  r[7] = bb_entity_set_ability((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_entity_has_ability(uint32_t *r) {
  r[7] = bb_entity_has_ability((void *)(uintptr_t)r[6], A(0));
}
const struct bb_model_target *bb_entity_query_model_find(void *unused,
                                                         uint32_t id) {
  (void)unused;
  static const struct bb_model_target targets[] = {
      {0x433d50, sort, 1, 0, 0},
      {0x435270, add, 0, 0, 0},
      {0x4352d0, remove_entry, 0, 0, 0},
      {0x437f10, dirty, 0, 0, 0},
      {0x433e20, model_bb_entity_at, 2, 0, 0},
      {0x433f10, model_bb_entity_at_next, 3, 0, 0},
      {0x434020, model_bb_entity_outer_at, 2, 0, 0},
      {0x4340b0, model_bb_entity_near, 3, 0, 0},
      {0x434120, model_bb_entity_near_next, 4, 0, 0},
      {0x4341a0, model_bb_entity_nearest, 4, 0, 0},
      {0x434220, model_bb_entity_assign_target, 1, 0, 0},
      {0x438220, model_bb_entity_set_ability, 1, 0, 0},
      {0x438290, model_bb_entity_has_ability, 1, 0, 0},
  };
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
