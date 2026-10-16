/* SPDX-License-Identifier: GPL-3.0-only */
#include "entity_lifecycle_models.h"
#include "interop.h"
void *BB_THISCALL bb_entity_construct(void *);
uint64_t bb_entity_pool_allocate_c(void *);
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
static void *self(uint32_t *r) { return (void *)(uintptr_t)r[6]; }
static void construct(uint32_t *r) {
  r[7] = bb_address_encode(bb_entity_construct(self(r)));
}
static void pool(uint32_t *r) {
  uint32_t object = r[6];
  uint64_t value = bb_entity_pool_allocate_c(self(r));
  r[7] = (uint32_t)value;
  r[5] = value >> 32;
  r[6] = BB_U32(object);
  r[8] = (r[8] & ~0x8d5u) | 0x44u;
}
static void clear(uint32_t *r) {
  r[7] = bb_entity_manager_clear(self(r));
  r[5] = 5000u * 314u;
  r[8] = (r[8] & ~0x8d5u) | 0x44u;
}
static void reset(uint32_t *r) { r[7] = bb_entity_manager_reset(self(r)); }
static void create(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_entity_create(self(r), A(0), A(1), A(2), A(3), A(4)));
}
static void remove_entry(uint32_t *r) {
  r[7] = bb_entity_remove(self(r), (void *)(uintptr_t)A(0));
}
static void remove_at(uint32_t *r) {
  if (BB_U32(r[6] + 0x1008))
    r[7] = bb_entity_remove_at(self(r), A(0), A(1));
  else
    r[8] = (r[8] & ~0x8d5u) | 0x44u;
}
static void destroy(uint32_t *r) {
  r[7] = bb_entity_destroy(self(r), (void *)(uintptr_t)A(0));
}
static void references(uint32_t *r) {
  r[7] = bb_entity_clear_references(self(r), (void *)(uintptr_t)A(0));
}
static void pending(uint32_t *r) { r[7] = bb_entity_remove_pending(self(r)); }
const struct bb_model_target *bb_entity_lifecycle_model_find(void *unused,
                                                             uint32_t id) {
  (void)unused;
  static const struct bb_model_target targets[] = {
      {0x4338a0, pool, 0, 0, 0},         {0x433910, reset, 0, 0, 0},
      {0x433b50, clear, 0, 0, 0},        {0x433ba0, create, 5, 0, 0},
      {0x4342c0, remove_entry, 1, 0, 0}, {0x4344b0, remove_at, 2, 0, 0},
      {0x434500, destroy, 1, 0, 0},      {0x434680, references, 1, 0, 0},
      {0x4347e0, pending, 0, 0, 0},      {0x435330, construct, 0, 0, 0},
  };
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
