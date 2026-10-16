/* SPDX-License-Identifier: GPL-3.0-only */
#include "world_models.h"
#include "interop.h"
#include "world.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
static void initialize(uint32_t *r) {
  (void)r;
  bb_world_initialize_entry();
}
static void clone(uint32_t *r) { bb_type_clone_entry(A(0), A(1)); }
static void blueprint(uint32_t *r) {
  bb_type_blueprint_entry(A(0), (const uint8_t *)(uintptr_t)A(1));
}
static void interface_noop(uint32_t *r) {
  (void)r;
  bb_world_interface_noop_entry();
}
static void costs(uint32_t *r) {
  (void)r;
  bb_world_costs_initialize_entry();
}
static void prepare(uint32_t *r) {
  (void)r;
  bb_terrain_prepare_entry();
}
static void prepare_tile(uint32_t *r) {
  bb_terrain_prepare_tile_entry(A(0), A(1));
}
static void icon(uint32_t *r) {
  bb_icon_panel_initialize_entry((void *)(uintptr_t)r[6]);
}
const struct bb_model_target *bb_world_model_find(void *unused, uint32_t id) {
  (void)unused;
  static const struct bb_model_target targets[] = {
      {0x40d020, initialize, 0, 0, 0},     {0x40cfa0, clone, 2, 0, 0},
      {0x4293e0, blueprint, 2, 0, 0},      {0x41e6c0, icon, 0, 0, 0},
      {0x420cb0, interface_noop, 0, 0, 0}, {0x42dfb0, costs, 0, 0, 0},
      {0x4186f0, prepare, 0, 0, 0},        {0x4186e0, prepare_tile, 2, 0, 0},
  };
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
